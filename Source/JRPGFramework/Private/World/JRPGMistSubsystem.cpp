#include "World/JRPGMistSubsystem.h"
#include "World/JRPGMistVolume.h"
#include "World/JRPGMistDisturberComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

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
}

void UJRPGMistSubsystem::RegisterVolume(AJRPGMistVolume* Volume)
{
	if (Volume)
	{
		Volumes.AddUnique(Volume);
	}
}

void UJRPGMistSubsystem::UnregisterVolume(AJRPGMistVolume* Volume)
{
	Volumes.RemoveAll([Volume](const TWeakObjectPtr<AJRPGMistVolume>& V) { return !V.IsValid() || V.Get() == Volume; });

	// Último volume saindo (troca de mapa): limpa a MPC enquanto o mundo ainda é válido e
	// esquece o rastro, para o mapa seguinte não nascer com redemoinho velho
	if (Volumes.Num() == 0)
	{
		WriteSlots(Volume, TArray<FSlot>());
		Trail.Reset();
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
	Slots.Reserve(NumSlots);

	// 1) O pawn do jogador: posição atual + rastro. Detectado sozinho — não depende de
	// alguém lembrar de marcar o componente certo
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
	if (Slots.Num() < NumSlots)
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
			if (Slots.Num() >= NumSlots)
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

void UJRPGMistSubsystem::UpdateTrail(const UJRPGMistDisturberComponent* Owner, float DeltaTime, TArray<FSlot>& OutSlots)
{
	// Dono trocou (respawn, troca de personagem): o rastro antigo não é dele
	if (TrailOwner.Get() != Owner)
	{
		Trail.Reset();
		TimeSinceTrailPush = 0.0f;
		TrailOwner = Owner;
	}

	for (FTrailPoint& P : Trail)
	{
		P.Age += DeltaTime;
	}
	if (!Owner || !Owner->GetOwner())
	{
		Trail.Reset();
		return;
	}

	const float Lifetime = FMath::Max(Owner->TrailLifetime, 0.2f);
	Trail.RemoveAll([Lifetime](const FTrailPoint& P) { return P.Age >= Lifetime; });

	FVector Velocity;
	const float Strength = Owner->GetCurrentStrength(Velocity);
	const FVector Location = Owner->GetOwner()->GetActorLocation();
	const FVector2D Position(Location.X, Location.Y);
	const FVector2D Dir = FVector2D(Velocity.X, Velocity.Y).GetSafeNormal();

	// Posição atual
	{
		FSlot& S = OutSlots.AddDefaulted_GetRef();
		S.Slot = FLinearColor(Position.X, Position.Y, Owner->Radius, Strength);
		S.Dir = FLinearColor(Dir.X, Dir.Y, Owner->SwirlScale, 0.0f);
	}

	// Um ponto novo a cada Lifetime / (pontos) segundos andando: os pontos cobrem os
	// últimos Lifetime segundos, qualquer que seja a velocidade
	constexpr int32 MaxPoints = TrailSlots - 1;
	const float PushInterval = Lifetime / MaxPoints;
	TimeSinceTrailPush += DeltaTime;
	const bool bMoving = !Dir.IsZero() && Strength > Owner->IdleStrength;
	if (bMoving && TimeSinceTrailPush >= PushInterval)
	{
		TimeSinceTrailPush = 0.0f;
		Trail.Insert({ Position, Dir, 0.0f, Strength }, 0);
		if (Trail.Num() > MaxPoints)
		{
			Trail.SetNum(MaxPoints);
		}
	}

	// Pontos do rastro: enfraquecem e alargam com a idade (a esteira se espalha e fecha)
	for (const FTrailPoint& P : Trail)
	{
		const float Life = 1.0f - P.Age / Lifetime;
		FSlot& S = OutSlots.AddDefaulted_GetRef();
		S.Slot = FLinearColor(P.Position.X, P.Position.Y,
			Owner->Radius * (1.0f + Owner->TrailWidening * (1.0f - Life)), P.Strength * Life);
		S.Dir = FLinearColor(P.Direction.X, P.Direction.Y, Owner->SwirlScale, 0.0f);
	}
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

	for (int32 i = 0; i < NumSlots; ++i)
	{
		const FSlot Slot = Slots.IsValidIndex(i) ? Slots[i] : FSlot();
		Instance->SetVectorParameterValue(SlotParam(i), Slot.Slot);
		Instance->SetVectorParameterValue(DirParam(i), Slot.Dir);
	}
}
