#include "World/JRPGMistSubsystem.h"
#include "World/JRPGMistVolume.h"
#include "World/JRPGMistDisturberComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "RHITypes.h"

namespace
{
	// Nomes dos parâmetros na MPC_JRPGMist: Slot0..Slot7 e Dir0..Dir7
	const FName& SlotParam(int32 Index)
	{
		static const FName Names[UJRPGMistSubsystem::NumSlots] = {
			TEXT("Slot0"), TEXT("Slot1"), TEXT("Slot2"), TEXT("Slot3"),
			TEXT("Slot4"), TEXT("Slot5"), TEXT("Slot6"), TEXT("Slot7") };
		return Names[Index];
	}

	const FName& DirParam(int32 Index)
	{
		static const FName Names[UJRPGMistSubsystem::NumSlots] = {
			TEXT("Dir0"), TEXT("Dir1"), TEXT("Dir2"), TEXT("Dir3"),
			TEXT("Dir4"), TEXT("Dir5"), TEXT("Dir6"), TEXT("Dir7") };
		return Names[Index];
	}

	/** Distância de P até o segmento A-B, no plano. */
	float DistToSegment2D(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double Len2 = AB.SizeSquared();
		const double T = Len2 > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Len2, 0.0, 1.0) : 0.0;
		return float(FVector2D::Distance(P, A + AB * T));
	}

	/** Resolução padrão da textura do rastro, antes de saber qual é o componente do jogador. */
	constexpr int32 DefaultTrailResolution = 192;

	/** Pulo maior que isto num frame é teleporte: não pinta a ligação. */
	constexpr float TeleportDistance = 1000.0f;

	/**
	 * Quanto o rastro abre a D uu do caminho (0..1), com raio R e força S.
	 * Hardness 0 = gaussiana saturada, min(S × exp(-D²/R²), 1): borda suave e longa.
	 * Hardness 1 = faixa aberta por igual até onde a gaussiana passa por 0.5, com a borda
	 * em 15% desse raio: mesma largura, borda marcada. No meio, mistura as duas.
	 */
	float TrailOpening(float D, float R, float S, float Hardness)
	{
		const float Gauss = FMath::Min(S * FMath::Exp(-(D * D) / (R * R)), 1.0f);
		const float H = FMath::Clamp(Hardness, 0.0f, 1.0f);
		if (H <= 0.0f || S <= 0.5f)
		{
			return Gauss;   // força abaixo de 0.5 nunca abre metade: não há borda para marcar
		}
		const float Edge = R * FMath::Sqrt(FMath::Loge(2.0f * S));
		const float Width = FMath::Max(Edge * 0.15f, 1.0f);
		const float Band = 1.0f - FMath::SmoothStep(Edge - Width, Edge + Width, D);
		return FMath::Lerp(Gauss, Band, H);
	}
}

void UJRPGMistSubsystem::RegisterVolume(AJRPGMistVolume* Volume)
{
	if (!Volume)
	{
		return;
	}
	Volumes.AddUnique(Volume);

	if (!TrailTexture)
	{
		EnsureTrailTexture(DefaultTrailResolution);
	}
	Volume->SetTrailTexture(TrailTexture);
}

void UJRPGMistSubsystem::UnregisterVolume(AJRPGMistVolume* Volume)
{
	Volumes.RemoveAll([Volume](const TWeakObjectPtr<AJRPGMistVolume>& V) { return !V.IsValid() || V.Get() == Volume; });

	// Último volume saindo (troca de mapa): limpa a MPC enquanto o mundo ainda é válido e
	// esquece o rastro, para o mapa seguinte não nascer com redemoinho velho
	if (Volumes.Num() == 0)
	{
		ResetTrail();
		UploadTrail();
		WriteSlots(Volume, TArray<FSlot>());
		TrailOwner.Reset();
	}
}

void UJRPGMistSubsystem::RegisterDisturber(UJRPGMistDisturberComponent* Disturber)
{
	if (Disturber)
	{
		Disturbers.AddUnique(Disturber);
	}
}

void UJRPGMistSubsystem::UnregisterDisturber(UJRPGMistDisturberComponent* Disturber)
{
	Disturbers.RemoveAll([Disturber](const TWeakObjectPtr<UJRPGMistDisturberComponent>& D) { return !D.IsValid() || D.Get() == Disturber; });
}

bool UJRPGMistSubsystem::IsTickable() const
{
	// O CDO também é um FTickableGameObject — nunca tica
	return !IsTemplate() && Volumes.Num() > 0;
}

UWorld* UJRPGMistSubsystem::GetTickableGameObjectWorld() const
{
	const AJRPGMistVolume* Volume = GetFirstVolume();
	return Volume ? Volume->GetWorld() : nullptr;
}

TStatId UJRPGMistSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UJRPGMistSubsystem, STATGROUP_Tickables);
}

AJRPGMistVolume* UJRPGMistSubsystem::GetFirstVolume() const
{
	for (const TWeakObjectPtr<AJRPGMistVolume>& V : Volumes)
	{
		if (V.IsValid())
		{
			return V.Get();
		}
	}
	return nullptr;
}

void UJRPGMistSubsystem::Tick(float DeltaTime)
{
	Disturbers.RemoveAll([](const TWeakObjectPtr<UJRPGMistDisturberComponent>& D) { return !D.IsValid(); });

	TArray<FSlot> Slots;
	Slots.Reserve(DisturberSlots);

	// 1) O pawn do jogador: posição atual (slot), e a área do rastro anda com ele. Detectado
	// sozinho — não depende de alguém lembrar de marcar o componente certo. O rastro de todos
	// (jogador e NPCs com bLeavesTrail) é pintado na textura
	const UJRPGMistDisturberComponent* Owner = nullptr;
	for (const TWeakObjectPtr<UJRPGMistDisturberComponent>& D : Disturbers)
	{
		const APawn* Pawn = Cast<APawn>(D->GetOwner());
		if (D->bLeavesTrail && Pawn && Pawn->IsPlayerControlled())
		{
			Owner = D.Get();
			break;
		}
	}
	UpdateTrail(Owner, DeltaTime, Slots);

	// 2) Os outros, mais próximos da câmera primeiro
	if (Slots.Num() < DisturberSlots)
	{
		const AJRPGMistVolume* Volume = GetFirstVolume();
		const UWorld* World = Volume ? Volume->GetWorld() : nullptr;
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const FVector CameraLocation = (PC && PC->PlayerCameraManager)
			? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;

		struct FCandidate { const UJRPGMistDisturberComponent* Comp; float Strength; FVector Velocity; float Dist2; };
		TArray<FCandidate, TInlineAllocator<16>> Candidates;
		for (const TWeakObjectPtr<UJRPGMistDisturberComponent>& D : Disturbers)
		{
			if (D.Get() == Owner || !D->GetOwner())
			{
				continue;
			}
			FVector Velocity;
			const float Strength = D->GetCurrentStrength(Velocity);
			if (Strength > 0.0f)
			{
				const float Dist2 = FVector::DistSquared(D->GetOwner()->GetActorLocation(), CameraLocation);
				Candidates.Add({ D.Get(), Strength, Velocity, Dist2 });
			}
		}
		Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Dist2 < B.Dist2; });

		for (const FCandidate& C : Candidates)
		{
			if (Slots.Num() >= DisturberSlots)
			{
				break;
			}
			const FVector Location = C.Comp->GetOwner()->GetActorLocation();
			const FVector2D Dir = FVector2D(C.Velocity.X, C.Velocity.Y).GetSafeNormal();
			FSlot& S = Slots.AddDefaulted_GetRef();
			S.Slot = FLinearColor(Location.X, Location.Y, C.Comp->Radius, C.Strength);
			S.Dir = FLinearColor(Dir.X, Dir.Y, C.Comp->SwirlScale, 0.0f);
		}
	}

	WriteSlots(GetFirstVolume(), Slots);
}

void UJRPGMistSubsystem::ResetTrail()
{
	for (FTrailTexel& T : TrailTexels)
	{
		T = FTrailTexel();
	}
	bTrailHasContent = false;
	bTrailOriginValid = false;
	TrailLastPositions.Reset();
}

void UJRPGMistSubsystem::EnsureTrailTexture(int32 Resolution)
{
	const int32 Res = FMath::Clamp(Resolution, 32, 512);
	if (TrailTexture && TrailRes == Res)
	{
		return;
	}

	// G8 linear: o shader lê .r direto, sem conversão de cor. Transient — não vira asset
	UTexture2D* NewTexture = UTexture2D::CreateTransient(Res, Res, PF_G8, TEXT("JRPGMistTrail"));
	if (!NewTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("JRPGMistSubsystem: não foi possível criar a textura do rastro."));
		return;
	}
	NewTexture->SRGB = false;
	NewTexture->Filter = TF_Bilinear;
	NewTexture->AddressX = TA_Clamp;
	NewTexture->AddressY = TA_Clamp;
	NewTexture->UpdateResource();

	TrailTexture = NewTexture;
	TrailRes = Res;
	TrailTexels.SetNum(Res * Res);
	TrailScratch.SetNum(Res * Res);
	TrailPixels.SetNumZeroed(Res * Res);
	ResetTrail();

	// Começa zerada (CreateTransient não limpa a memória)
	bTrailHasContent = true;
	UploadTrail();

	for (const TWeakObjectPtr<AJRPGMistVolume>& V : Volumes)
	{
		if (V.IsValid())
		{
			V->SetTrailTexture(TrailTexture);
		}
	}
}

void UJRPGMistSubsystem::RecenterTrail(const FVector2D& Center, float AreaSize)
{
	if (TrailRes <= 0)
	{
		return;
	}
	const float Area = FMath::Max(AreaSize, 500.0f);
	const double Texel = double(Area) / TrailRes;
	const FVector2D Snapped(
		FMath::FloorToDouble((Center.X - Area * 0.5) / Texel) * Texel,
		FMath::FloorToDouble((Center.Y - Area * 0.5) / Texel) * Texel);

	if (!bTrailOriginValid || !FMath::IsNearlyEqual(TrailArea, Area))
	{
		for (FTrailTexel& T : TrailTexels)
		{
			T = FTrailTexel();
		}
		TrailArea = Area;
		TrailOrigin = Snapped;
		bTrailOriginValid = true;
		return;
	}

	// Só move quando o jogador sai do meio da área: menos cópias, e o rastro atrás dele fica
	const FVector2D Middle = TrailOrigin + FVector2D(Area * 0.5, Area * 0.5);
	if (FMath::Abs(Center.X - Middle.X) < Area * 0.25 && FMath::Abs(Center.Y - Middle.Y) < Area * 0.25)
	{
		return;
	}

	const int32 DX = FMath::RoundToInt32((Snapped.X - TrailOrigin.X) / Texel);
	const int32 DY = FMath::RoundToInt32((Snapped.Y - TrailOrigin.Y) / Texel);
	TrailOrigin += FVector2D(DX * Texel, DY * Texel);
	if (DX == 0 && DY == 0)
	{
		return;
	}

	// Desloca o que já foi pintado: texel novo (X, Y) = texel velho (X + DX, Y + DY)
	const int32 Res = TrailRes;
	for (int32 Y = 0; Y < Res; ++Y)
	{
		const int32 SrcY = Y + DY;
		for (int32 X = 0; X < Res; ++X)
		{
			const int32 SrcX = X + DX;
			TrailScratch[Y * Res + X] = (SrcX >= 0 && SrcX < Res && SrcY >= 0 && SrcY < Res)
				? TrailTexels[SrcY * Res + SrcX] : FTrailTexel();
		}
	}
	Swap(TrailTexels, TrailScratch);
}

float UJRPGMistSubsystem::TrailValue(int32 Index) const
{
	const FTrailTexel& T = TrailTexels[Index];
	if (T.Time < 0.0f)
	{
		return 0.0f;
	}
	const float K = (TrailClock - T.Time) / T.Lifetime;
	if (K >= 1.0f)
	{
		return 0.0f;
	}
	// A esteira alarga com a idade. Fica aberta por inteiro até TrailHold da vida e só então
	// fecha, suave, zerando em exatamente Lifetime: fechando desde o começo, a névoa trazida
	// pelo vento já ia enchendo o caminho logo atrás do jogador
	const float R = T.Radius * (1.0f + T.Widening * K);
	const float Fade = 1.0f - FMath::SmoothStep(T.Hold, 1.0f, K);
	return TrailOpening(T.Dist, R, T.Strength, T.Hardness) * Fade;
}

void UJRPGMistSubsystem::PaintTrail(const FVector2D& A, const FVector2D& B, const UJRPGMistDisturberComponent& Owner, float Strength)
{
	if (Strength <= 0.0f || TrailRes <= 0)
	{
		return;
	}
	const double Texel = double(TrailArea) / TrailRes;
	const float R = FMath::Max(Owner.Radius, 1.0f);
	// Até onde Força × gaussiana passa de 0.01 (com Força 1, 2.15 × Raio), já com o raio que o
	// rastro alcança alargando (TrailWidening) — o texel guarda a distância agora e abre quando
	// a esteira chega nele. Limitado a 1/4 da área: o custo cresce com o quadrado do alcance
	const float Spread = 1.0f + FMath::Max(Owner.TrailWidening, 0.0f);
	const float Reach = FMath::Min(
		R * Spread * FMath::Sqrt(FMath::Max(FMath::Loge(FMath::Max(Strength, 1.0f) / 0.01f), 1.0f)),
		TrailArea * 0.25f);

	const int32 MinX = FMath::Max(0, FMath::FloorToInt32((FMath::Min(A.X, B.X) - Reach - TrailOrigin.X) / Texel));
	const int32 MaxX = FMath::Min(TrailRes - 1, FMath::CeilToInt32((FMath::Max(A.X, B.X) + Reach - TrailOrigin.X) / Texel));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt32((FMath::Min(A.Y, B.Y) - Reach - TrailOrigin.Y) / Texel));
	const int32 MaxY = FMath::Min(TrailRes - 1, FMath::CeilToInt32((FMath::Max(A.Y, B.Y) + Reach - TrailOrigin.Y) / Texel));

	// A config de quem pinta vai junto em cada texel
	FTrailTexel Painted;
	Painted.Time = TrailClock;
	Painted.Strength = Strength;
	Painted.Radius = R;
	Painted.Lifetime = FMath::Max(Owner.TrailLifetime, 0.2f);
	Painted.Widening = FMath::Max(Owner.TrailWidening, 0.0f);
	Painted.Hardness = FMath::Clamp(Owner.TrailEdgeHardness, 0.0f, 1.0f);
	Painted.Hold = FMath::Clamp(Owner.TrailHold, 0.0f, 0.98f);

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const FVector2D P = TrailOrigin + FVector2D((X + 0.5) * Texel, (Y + 0.5) * Texel);
			const float D = DistToSegment2D(P, A, B);
			if (D > Reach)
			{
				continue;
			}
			// Só troca de passada se a nova abre mais que a antiga abre agora: passar de novo,
			// ou dois passando no mesmo lugar, não acumula
			const int32 Index = Y * TrailRes + X;
			if (TrailOpening(D, R, Strength, Painted.Hardness) >= TrailValue(Index))
			{
				Painted.Dist = D;
				TrailTexels[Index] = Painted;
				bTrailHasContent = true;
			}
		}
	}
}

void UJRPGMistSubsystem::UploadTrail()
{
	// Nada pintado (e a textura já foi zerada): não sobe nada
	if (!bTrailHasContent || !TrailTexture || !TrailTexture->GetResource() || TrailRes <= 0)
	{
		return;
	}

	// Calcula o valor de cada texel e expira o que fechou
	const int32 Count = TrailRes * TrailRes;
	bool bAny = false;
	for (int32 i = 0; i < Count; ++i)
	{
		float Value = 0.0f;
		if (TrailTexels[i].Time >= 0.0f)
		{
			Value = TrailValue(i);
			if (Value <= 0.0f && TrailClock - TrailTexels[i].Time >= TrailTexels[i].Lifetime)
			{
				TrailTexels[i] = FTrailTexel();
			}
		}
		TrailPixels[i] = uint8(FMath::Clamp(FMath::RoundToInt32(Value * 255.0f), 0, 255));
		bAny |= TrailTexels[i].Time >= 0.0f;
	}
	// Ainda sobe esta vez (com o que acabou de zerar); a partir do próximo frame, nada
	bTrailHasContent = bAny;

	// O render thread copia depois: a cópia e a região são dele até o cleanup
	uint8* Data = new uint8[Count];
	FMemory::Memcpy(Data, TrailPixels.GetData(), Count);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, TrailRes, TrailRes);
	TrailTexture->UpdateTextureRegions(0, 1, Region, TrailRes, 1, Data,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			delete[] SrcData;
			delete Regions;
		});
}

void UJRPGMistSubsystem::PaintDisturberTrail(const UJRPGMistDisturberComponent& Disturber)
{
	const AActor* Actor = Disturber.GetOwner();
	if (!Actor)
	{
		return;
	}
	FVector Velocity;
	const float Strength = Disturber.GetCurrentStrength(Velocity);
	const FVector Location = Actor->GetActorLocation();
	const FVector2D Position(Location.X, Location.Y);

	// Pinta do ponto do frame anterior até o atual: o caminho exato, sem buracos entre frames
	// por mais rápido que ande. Parado não pinta. "Andando" vem da velocidade, não da força:
	// com IdleStrength igual ao Strength a força nunca passava da parada e nada era pintado
	const FVector2D* Last = TrailLastPositions.Find(&Disturber);
	const bool bTeleported = Last && FVector2D::DistSquared(Position, *Last) > FMath::Square(TeleportDistance);
	const bool bMoving = FVector2D(Velocity.X, Velocity.Y).Size() >= FMath::Max(Disturber.MinSpeed, 1.0f);
	if (bMoving)
	{
		PaintTrail((Last && !bTeleported) ? *Last : Position, Position, Disturber, Strength);
	}
	TrailLastPositions.Add(&Disturber, Position);
}

void UJRPGMistSubsystem::UpdateTrail(const UJRPGMistDisturberComponent* Player, float DeltaTime, TArray<FSlot>& OutSlots)
{
	TrailClock += DeltaTime;

	// A área do rastro acompanha o jogador; sem jogador, o primeiro que deixa rastro
	const UJRPGMistDisturberComponent* AreaOwner = Player;
	if (!AreaOwner)
	{
		for (const TWeakObjectPtr<UJRPGMistDisturberComponent>& D : Disturbers)
		{
			if (D->bLeavesTrail && D->GetOwner())
			{
				AreaOwner = D.Get();
				break;
			}
		}
	}

	// Quem a área acompanha trocou (respawn, troca de personagem, troca de mapa): recomeça
	if (TrailOwner.Get() != AreaOwner)
	{
		ResetTrail();
		bTrailHasContent = true;   // sobe uma vez zerada
		TrailOwner = AreaOwner;
	}

	const AActor* AreaActor = AreaOwner ? AreaOwner->GetOwner() : nullptr;
	if (!AreaActor)
	{
		TrailLastPositions.Reset();
		UploadTrail();
		return;
	}

	EnsureTrailTexture(AreaOwner->TrailResolution);

	// Posição atual do jogador: abre a névoa e gira em volta. O rastro na textura só abre, não
	// gira. Os NPCs ganham o slot deles na disputa por proximidade da câmera (Tick)
	if (Player && Player->GetOwner())
	{
		FVector Velocity;
		const float Strength = Player->GetCurrentStrength(Velocity);
		const FVector Location = Player->GetOwner()->GetActorLocation();
		const FVector2D VelocityDir = FVector2D(Velocity.X, Velocity.Y).GetSafeNormal();
		FSlot& S = OutSlots.AddDefaulted_GetRef();
		S.Slot = FLinearColor(Location.X, Location.Y, Player->Radius, Strength);
		S.Dir = FLinearColor(VelocityDir.X, VelocityDir.Y, Player->SwirlScale, 0.0f);
	}

	const FVector AreaLocation = AreaActor->GetActorLocation();
	RecenterTrail(FVector2D(AreaLocation.X, AreaLocation.Y), AreaOwner->TrailAreaSize);

	// Todos que deixam rastro pintam na mesma textura (o que estiver fora da área não aparece)
	for (auto It = TrailLastPositions.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Key()->bLeavesTrail)
		{
			It.RemoveCurrent();
		}
	}
	for (const TWeakObjectPtr<UJRPGMistDisturberComponent>& D : Disturbers)
	{
		if (D->bLeavesTrail)
		{
			PaintDisturberTrail(*D);
		}
	}

	UploadTrail();

#if ENABLE_DRAW_DEBUG
	if (AreaOwner->bDebugDrawTrail)
	{
		DrawTrailDebug(AreaOwner->GetWorld(), AreaLocation.Z + 10.0f);
	}
#endif
}

void UJRPGMistSubsystem::DrawTrailDebug(const UWorld* World, float Z) const
{
#if ENABLE_DRAW_DEBUG
	if (!World || TrailRes <= 0 || !bTrailOriginValid)
	{
		return;
	}

	// Contorno da área que a textura cobre (acompanha o jogador)
	const FVector Center(TrailOrigin.X + TrailArea * 0.5, TrailOrigin.Y + TrailArea * 0.5, Z);
	DrawDebugBox(World, Center, FVector(TrailArea * 0.5, TrailArea * 0.5, 1.0), FColor::Cyan, false, -1.0f, 0, 2.0f);

	// O rastro como o shader lê: um ponto por texel pintado. Verde = aberto, vermelho = fechando
	const double Texel = double(TrailArea) / TrailRes;
	for (int32 Y = 0; Y < TrailRes; ++Y)
	{
		for (int32 X = 0; X < TrailRes; ++X)
		{
			const float V = TrailPixels[Y * TrailRes + X] / 255.0f;
			if (V < 0.05f)
			{
				continue;
			}
			const FVector P(TrailOrigin.X + (X + 0.5) * Texel, TrailOrigin.Y + (Y + 0.5) * Texel, Z);
			const FColor Color = FLinearColor::LerpUsingHSV(FLinearColor::Red, FLinearColor::Green, FMath::Clamp(V, 0.0f, 1.0f)).ToFColor(true);
			DrawDebugPoint(World, P, 4.0f, Color, false, -1.0f);
		}
	}
#endif
}

void UJRPGMistSubsystem::WriteSlots(const AJRPGMistVolume* Volume, const TArray<FSlot>& Slots)
{
	UWorld* World = Volume ? Volume->GetWorld() : nullptr;
	UMaterialParameterCollection* MPC = Volume ? Volume->GetParameterCollection() : nullptr;
	UMaterialParameterCollectionInstance* Instance = (World && MPC) ? World->GetParameterCollectionInstance(MPC) : nullptr;
	if (!Instance)
	{
		if (!bWarnedMissingMPC)
		{
			bWarnedMissingMPC = true;
			UE_LOG(LogTemp, Warning, TEXT("JRPGMistSubsystem: MPC_JRPGMist não encontrada — a névoa não vai reagir a ninguém."));
		}
		return;
	}

	for (int32 i = 0; i < DisturberSlots; ++i)
	{
		const FSlot Slot = Slots.IsValidIndex(i) ? Slots[i] : FSlot();
		Instance->SetVectorParameterValue(SlotParam(i), Slot.Slot);
		Instance->SetVectorParameterValue(DirParam(i), Slot.Dir);
	}

	// Slot7 = mapeamento da textura do rastro: canto (texel 0,0) em uu, lado em uu, ligado.
	// Ver JRPGMistTrail no shader
	const bool bTrailOn = bTrailOriginValid && TrailTexture;
	Instance->SetVectorParameterValue(SlotParam(DisturberSlots),
		bTrailOn ? FLinearColor(TrailOrigin.X, TrailOrigin.Y, TrailArea, 1.0f) : FLinearColor::Transparent);
	Instance->SetVectorParameterValue(DirParam(DisturberSlots), FLinearColor::Transparent);
}
