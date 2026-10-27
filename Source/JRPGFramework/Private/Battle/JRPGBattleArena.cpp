#include "Battle/JRPGBattleArena.h"
#include "Components/BillboardComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#endif

namespace
{
	/**
	 * Os lugares de batalha do jogo original, em unidades do PS1: (x, z) com a party virada para
	 * +Z e os monstros para -Z (o chão é plano, y = 0). Tabelas estáticas do SCUS_942.54 lidas
	 * por FUN_800513F0 — party em 0x800775C8 (linha = quantos heróis), monstros em 0x80077608
	 * (linha = quantos monstros; +4 = família alternada). Fonte: legend-of-legaia-re,
	 * docs/subsystems/battle.md ("Stage seats") e crates/engine-battle/src/battle_seats.rs.
	 * Índice 0 da party é o líder, no centro.
	 */
	struct FOriginalSeat { int32 X; int32 Z; };

	const FOriginalSeat PartySeatRows[3][3] = {
		{ { 0, -800 } },
		{ { 300, -800 }, { -300, -800 } },
		{ { 0, -825 }, { 600, -775 }, { -600, -775 } },
	};

	const FOriginalSeat EnemySeatRows[4][4] = {
		{ { 0, 800 } },
		{ { -300, 800 }, { 300, 800 } },
		{ { -600, 825 }, { 0, 750 }, { 600, 825 } },
		{ { -900, 900 }, { -300, 800 }, { 300, 800 }, { 900, 900 } },
	};

	const FOriginalSeat EnemySeatRowsAlt[4][4] = {
		{ { 0, 800 } },
		{ { -300, 800 }, { 300, 800 } },
		{ { 0, 900 }, { -600, 700 }, { 600, 700 } },
		{ { 0, 1000 }, { -600, 800 }, { 600, 800 }, { 0, 600 } },
	};

	const FColor PartyColor(60, 140, 255);
	const FColor EnemyColor(255, 140, 30);

	FColor StatusColor(EJRPGArenaSeatStatus Status)
	{
		switch (Status)
		{
		case EJRPGArenaSeatStatus::Uneven:  return FColor(255, 210, 40);
		case EJRPGArenaSeatStatus::Blocked: return FColor(255, 40, 40);
		default:                            return FColor(60, 220, 90);
		}
	}
}

AJRPGBattleArena::AJRPGBattleArena()
{
	// Tick só no editor: o desenho da formação acompanha o terreno ao vivo
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// A área começa como um quadrado de 30 m de lado em volta da formação, com cantos retos
	AreaSpline = CreateDefaultSubobject<USplineComponent>(TEXT("AreaSpline"));
	AreaSpline->SetupAttachment(Root);
	AreaSpline->SetSplinePoints({ FVector(1500.0f, 1500.0f, 0.0f), FVector(1500.0f, -1500.0f, 0.0f),
		FVector(-1500.0f, -1500.0f, 0.0f), FVector(-1500.0f, 1500.0f, 0.0f) }, ESplineCoordinateSpace::Local, false);
	for (int32 i = 0; i < AreaSpline->GetNumberOfSplinePoints(); ++i)
	{
		AreaSpline->SetSplinePointType(i, ESplinePointType::Linear, false);
	}
	AreaSpline->SetClosedLoop(true, true);
	AreaSpline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AreaSpline->SetHiddenInGame(true);
#if WITH_EDITORONLY_DATA
	AreaSpline->EditorUnselectedSplineSegmentColor = FLinearColor(0.2f, 0.7f, 1.0f);

	EditorIcon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint"));
		EditorIcon->Sprite = Icon.Get();
		EditorIcon->SetupAttachment(Root);
		EditorIcon->SetHiddenInGame(true);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}

void AJRPGBattleArena::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SplineArea.Build(AreaSpline);
	AreaSpline->SetVisibility(bUseArea);
#if WITH_EDITOR
	RebuildPreviewMeshes();
#endif
}

void AJRPGBattleArena::BeginPlay()
{
	Super::BeginPlay();

	SetActorTickEnabled(false);
	SplineArea.Build(AreaSpline);
	if (bUseArea && !SplineArea.IsValid())
	{
		Print(TEXT("a área tem menos de 3 pontos — só vale como a mais próxima"), FColor::Orange);
	}
}

bool AJRPGBattleArena::ShouldTickIfViewportsOnly() const
{
	return true;
}

void AJRPGBattleArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return;
	}
#if WITH_EDITOR
	if (bPreviewOnlyWhenSelected && !IsSelectedInEditor())
	{
		return;
	}
#endif
	DrawFormation(GetFormation(PreviewPartyCount, PreviewEnemyCount), false, -1.0f);
}

// ---------------------------------------------------------------------------------------------
// Escolha da arena
// ---------------------------------------------------------------------------------------------

AJRPGBattleArena* AJRPGBattleArena::FindArenaFor(const UObject* WorldContextObject, const FVector& Point)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	AJRPGBattleArena* Inside = nullptr;
	AJRPGBattleArena* Nearest = nullptr;
	double InsideDist = TNumericLimits<double>::Max();
	double NearestDist = TNumericLimits<double>::Max();
	for (TActorIterator<AJRPGBattleArena> It(World); It; ++It)
	{
		AJRPGBattleArena* Arena = *It;
		if (!Arena->bEnabled)
		{
			continue;
		}
		const double Dist = FVector::DistSquared(Point, Arena->GetActorLocation());
		if (Arena->ContainsPoint(Point))
		{
			if (!Inside || Arena->Priority > Inside->Priority || (Arena->Priority == Inside->Priority && Dist < InsideDist))
			{
				Inside = Arena;
				InsideDist = Dist;
			}
		}
		else if (Arena->bUseAsFallback && Dist < NearestDist)
		{
			Nearest = Arena;
			NearestDist = Dist;
		}
	}

	AJRPGBattleArena* Chosen = Inside ? Inside : Nearest;
	if (Chosen)
	{
		Chosen->Print(Inside ? TEXT("escolhida: o toque foi dentro da área")
			: TEXT("escolhida: a mais próxima (o toque foi fora de todas as áreas)"), FColor::Cyan);
	}
	return Chosen;
}

bool AJRPGBattleArena::ContainsPoint(const FVector& Point) const
{
	if (!bUseArea || !SplineArea.IsValid())
	{
		return false;
	}
	if (AreaHeightLimit > 0.0f && FMath::Abs(Point.Z - GetActorLocation().Z) > AreaHeightLimit)
	{
		return false;
	}
	return SplineArea.Contains(Point);
}

// ---------------------------------------------------------------------------------------------
// Formação
// ---------------------------------------------------------------------------------------------

FTransform AJRPGBattleArena::GetLocalSeat(EJRPGBattleSide Side, int32 Index, int32 Count) const
{
	const bool bParty = Side == EJRPGBattleSide::Party;
	if (bManualSeats)
	{
		const TArray<FTransform>& Seats = bParty ? PartySeats : EnemySeats;
		if (Seats.IsValidIndex(Index))
		{
			return Seats[Index];
		}
	}

	FTransform Original;
	if (Formation == EJRPGArenaFormation::Original && GetOriginalSeat(Side, Index, Count, Original))
	{
		return Original;
	}

	// Arco em volta do centro: heróis atrás (180°), inimigos à frente (0°), cada lado virado para o outro
	const float Radius = bParty ? PartyRadius : EnemyRadius;
	const float Arc = bParty ? PartyArcDegrees : EnemyArcDegrees;
	const float T = Count > 1 ? static_cast<float>(Index) / (Count - 1) : 0.5f;
	const float Angle = FMath::DegreesToRadians((bParty ? 180.0f : 0.0f) + (T - 0.5f) * Arc);
	const FVector Location(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
	return FTransform(FRotator(0.0f, bParty ? 0.0f : 180.0f, 0.0f), Location);
}

bool AJRPGBattleArena::GetOriginalSeat(EJRPGBattleSide Side, int32 Index, int32 Count, FTransform& OutLocal) const
{
	const bool bParty = Side == EJRPGBattleSide::Party;
	if (Count < 1 || Index < 0 || Index >= Count || Count > (bParty ? 3 : 4))
	{
		return false;
	}
	const FOriginalSeat& Seat = bParty ? PartySeatRows[Count - 1][Index]
		: (bAlternateEnemyRows ? EnemySeatRowsAlt : EnemySeatRows)[Count - 1][Index];

	// PS1 -> arena: o z (frente da party) vira +X, o x (direita de quem olha para +Z) vira +Y
	OutLocal = FTransform(FRotator(0.0f, bParty ? 0.0f : 180.0f, 0.0f),
		FVector(Seat.Z * OriginalCmPerUnit, Seat.X * OriginalCmPerUnit, 0.0f));
	return true;
}

FTransform AJRPGBattleArena::GetSeatTransform(EJRPGBattleSide Side, int32 Index, int32 Count) const
{
	const FTransform World = GetLocalSeat(Side, Index, Count) * GetActorTransform();
	// Só o yaw: um lugar torto (manual, ou arena girada) não deita o combatente
	return FTransform(FRotator(0.0f, World.Rotator().Yaw, 0.0f), World.GetLocation());
}

FJRPGArenaSeat AJRPGBattleArena::ResolveSeat(EJRPGBattleSide Side, int32 Index, int32 Count) const
{
	return ResolveSeatAgainst(Side, Index, Count, GetReferenceFloorZ());
}

TArray<FJRPGArenaSeat> AJRPGBattleArena::GetFormation(int32 PartyCount, int32 EnemyCount) const
{
	const TOptional<double> ReferenceZ = GetReferenceFloorZ();
	TArray<FJRPGArenaSeat> Seats;
	Seats.Reserve(PartyCount + EnemyCount);
	for (int32 i = 0; i < PartyCount; ++i)
	{
		Seats.Add(ResolveSeatAgainst(EJRPGBattleSide::Party, i, PartyCount, ReferenceZ));
	}
	for (int32 i = 0; i < EnemyCount; ++i)
	{
		Seats.Add(ResolveSeatAgainst(EJRPGBattleSide::Enemy, i, EnemyCount, ReferenceZ));
	}
	return Seats;
}

FJRPGArenaSeat AJRPGBattleArena::ResolveSeatAgainst(EJRPGBattleSide Side, int32 Index, int32 Count, const TOptional<double>& ReferenceZ) const
{
	FJRPGArenaSeat Seat;
	Seat.Side = Side;
	Seat.Index = Index;
	Seat.Transform = GetSeatTransform(Side, Index, Count);

	FHitResult Hit;
	if (!TraceGround(Seat.Transform.GetLocation(), Hit))
	{
		Seat.Status = EJRPGArenaSeatStatus::Blocked;
		Seat.Problem = TEXT("sem chão embaixo");
		return Seat;
	}
	Seat.Transform.SetLocation(Hit.ImpactPoint);

	const double Slope = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.0, 1.0)));
	if (Slope > MaxSlopeDegrees)
	{
		Seat.Status = EJRPGArenaSeatStatus::Blocked;
		Seat.Problem = FString::Printf(TEXT("chão inclinado (%.0f°)"), Slope);
		return Seat;
	}

	// Parede/objeto onde o combatente fica: a cápsula um pouco acima do chão, com folga para a
	// inclinação aceita não contar como colisão
	const UWorld* World = GetWorld();
	const float Lift = SeatRadius * FMath::Tan(FMath::DegreesToRadians(MaxSlopeDegrees)) + 5.0f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGBattleArenaSeat), false, this);
	if (World->OverlapAnyTestByObjectType(FVector(Hit.ImpactPoint) + FVector(0.0f, 0.0f, SeatHalfHeight + Lift), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeCapsule(SeatRadius, SeatHalfHeight), Params))
	{
		Seat.Status = EJRPGArenaSeatStatus::Blocked;
		Seat.Problem = TEXT("dentro de parede/objeto");
		return Seat;
	}

	if (ReferenceZ.IsSet())
	{
		const double Diff = Hit.ImpactPoint.Z - ReferenceZ.GetValue();
		if (FMath::Abs(Diff) > MaxHeightDifference)
		{
			Seat.Status = EJRPGArenaSeatStatus::Uneven;
			Seat.Problem = FString::Printf(TEXT("%.0f cm %s do chão do centro"), FMath::Abs(Diff), Diff > 0.0 ? TEXT("acima") : TEXT("abaixo"));
		}
	}
	return Seat;
}

bool AJRPGBattleArena::TraceGround(const FVector& At, FHitResult& OutHit) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// Só WorldStatic: landscape e cenário, nunca o jogador ou os inimigos em volta
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGBattleArenaGround), false, this);
	return World->LineTraceSingleByObjectType(OutHit, At + FVector(0.0f, 0.0f, GroundSearchUp),
		At - FVector(0.0f, 0.0f, GroundSearchDown), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}

TOptional<double> AJRPGBattleArena::GetReferenceFloorZ() const
{
	FHitResult Hit;
	if (TraceGround(GetActorLocation(), Hit))
	{
		return Hit.ImpactPoint.Z;
	}
	return {};
}

// ---------------------------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------------------------

void AJRPGBattleArena::DrawDebugFormation(int32 PartyCount, int32 EnemyCount, float Duration) const
{
	DrawFormation(GetFormation(PartyCount, EnemyCount), false, Duration);
}

void AJRPGBattleArena::DrawFormation(const TArray<FJRPGArenaSeat>& Seats, bool bPersistent, float Duration) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (const FJRPGArenaSeat& Seat : Seats)
	{
		const FVector Feet = Seat.Transform.GetLocation();
		const FVector Forward = Seat.Transform.GetRotation().GetForwardVector();
		const FColor Side = Seat.Side == EJRPGBattleSide::Party ? PartyColor : EnemyColor;

		// Cápsula na cor do chão; anel e seta na cor do lado
		DrawDebugCapsule(World, Feet + FVector(0.0f, 0.0f, SeatHalfHeight), SeatHalfHeight, SeatRadius, FQuat::Identity,
			StatusColor(Seat.Status), bPersistent, Duration, 0, 1.5f);
		DrawDebugCircle(World, Feet + FVector(0.0f, 0.0f, 2.0f), SeatRadius + 10.0f, 24, Side, bPersistent, Duration, 0, 3.0f,
			FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), false);
		DrawDebugDirectionalArrow(World, Feet + FVector(0.0f, 0.0f, 20.0f), Feet + FVector(0.0f, 0.0f, 20.0f) + Forward * (SeatRadius + 80.0f),
			40.0f, Side, bPersistent, Duration, 0, 4.0f);
	}
}

// ---------------------------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------------------------

TArray<FString> AJRPGBattleArena::CollectProblems() const
{
	TArray<FString> Problems;
	for (const FJRPGArenaSeat& Seat : GetFormation(PreviewPartyCount, PreviewEnemyCount))
	{
		if (Seat.Status != EJRPGArenaSeatStatus::Ok)
		{
			Problems.Add(FString::Printf(TEXT("%s %d: %s"),
				Seat.Side == EJRPGBattleSide::Party ? TEXT("herói") : TEXT("inimigo"), Seat.Index + 1, *Seat.Problem));
		}
	}
	if (bUseArea && !SplineArea.IsValid())
	{
		Problems.Add(TEXT("a área tem menos de 3 pontos"));
	}
	return Problems;
}

void AJRPGBattleArena::ValidateGround()
{
#if WITH_EDITOR
	FMessageLog Log("MapCheck");
	const TArray<FString> Problems = CollectProblems();
	if (Problems.Num() == 0)
	{
		Log.Info()->AddToken(FUObjectToken::Create(this))->AddToken(FTextToken::Create(FText::FromString(TEXT("todos os lugares estão ok"))));
	}
	for (const FString& Problem : Problems)
	{
		Log.Warning()->AddToken(FUObjectToken::Create(this))->AddToken(FTextToken::Create(FText::FromString(Problem)));
	}
	Log.Open(EMessageSeverity::Info, true);
#endif
}

void AJRPGBattleArena::CopyFormationToManualSeats()
{
	Modify();
	// A formação automática, apoiada no chão, guardada relativa à arena
	bManualSeats = false;
	PartySeats.Reset();
	EnemySeats.Reset();
	for (const FJRPGArenaSeat& Seat : GetFormation(PreviewPartyCount, PreviewEnemyCount))
	{
		(Seat.Side == EJRPGBattleSide::Party ? PartySeats : EnemySeats).Add(Seat.Transform.GetRelativeTransform(GetActorTransform()));
	}
	bManualSeats = true;
#if WITH_EDITOR
	RebuildPreviewMeshes();
#endif
}

void AJRPGBattleArena::SnapAreaToGround()
{
	FJRPGSplineArea::SnapSplineToGround(AreaSpline, GroundSearchDown, this);
	SplineArea.Build(AreaSpline);
}

#if WITH_EDITOR
void AJRPGBattleArena::CheckForErrors()
{
	Super::CheckForErrors();
	for (const FString& Problem : CollectProblems())
	{
		FMessageLog("MapCheck").Warning()->AddToken(FUObjectToken::Create(this))->AddToken(FTextToken::Create(FText::FromString(Problem)));
	}
}

void AJRPGBattleArena::RebuildPreviewMeshes()
{
	for (USkeletalMeshComponent* Mesh : PreviewMeshes)
	{
		if (IsValid(Mesh))
		{
			Mesh->DestroyComponent();
		}
	}
	PreviewMeshes.Reset();

	UWorld* World = GetWorld();
	if (!World || World->IsGameWorld() || IsTemplate() || (!PreviewPartyMesh && !PreviewEnemyMesh))
	{
		return;
	}
	for (const FJRPGArenaSeat& Seat : GetFormation(PreviewPartyCount, PreviewEnemyCount))
	{
		USkeletalMesh* Asset = Seat.Side == EJRPGBattleSide::Party ? PreviewPartyMesh.Get() : PreviewEnemyMesh.Get();
		if (!Asset)
		{
			continue;
		}
		// Componente de construção, transitório e só do editor: nunca é salvo nem vai para o jogo
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient | RF_TextExportTransient);
		Mesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Mesh->bIsEditorOnly = true;
		Mesh->SetHiddenInGame(true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetSkeletalMeshAsset(Asset);
		Mesh->SetupAttachment(Root);
		Mesh->RegisterComponent();
		Mesh->SetWorldTransform(FTransform(FRotator(0.0f, Seat.Transform.Rotator().Yaw + PreviewMeshYaw, 0.0f), Seat.Transform.GetLocation()));
		PreviewMeshes.Add(Mesh);
	}
}
#endif

void AJRPGBattleArena::Print(const FString& Message, const FColor& Color) const
{
	if (!bPrintDebug)
	{
		return;
	}
	const FString Line = FString::Printf(TEXT("BattleArena %s: %s"), *GetActorNameOrLabel(), *Message);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Line);
	}
}
