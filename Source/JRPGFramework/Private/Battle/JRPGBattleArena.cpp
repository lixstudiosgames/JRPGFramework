#include "Battle/JRPGBattleArena.h"
#include "Battle/JRPGBattlePawn.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Camera/CameraSubsystem.h"
#include "Core/CoreSubsystem.h"
#include "Field/JRPGFieldCharacter.h"
#include "Field/JRPGFieldPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Party/CharacterData.h"
#include "Party/PartySubsystem.h"
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

	/** DT_EquipVisuals (em /Game/Data, ou a do plugin). Cache fraco, como a DT_Enemies. */
	const UDataTable* GetEquipVisualTable()
	{
		static TWeakObjectPtr<const UDataTable> Cached;
		if (!Cached.IsValid())
		{
			Cached = UCoreSubsystem::ResolveDataTable(TEXT("DT_EquipVisuals"));
		}
		return Cached.Get();
	}

	FName EquippedIn(const FJRPGPartyMember& Member, EJRPGEquipSlot Slot)
	{
		const int32 Idx = static_cast<int32>(Slot);
		return Member.Equipment.IsValidIndex(Idx) ? Member.Equipment[Idx] : NAME_None;
	}

	/**
	 * O que o herói veste na luta: uma parte por seção (a do item equipado, a padrão se o item não
	 * tem malha ou o slot está vazio) e o modelo genérico de cada slot de acessório ocupado.
	 * Herói sem nenhuma parte padrão na DT_EquipVisuals fica com o BattleMesh inteiro.
	 */
	void AddEquipmentVisuals(FName Character, const FCharacterData& Row, const FJRPGPartyMember& Member, FJRPGBattleVisual& Out)
	{
		const EJRPGEquipSlot AccessorySlots[] = { EJRPGEquipSlot::Accessory, EJRPGEquipSlot::Accessory2, EJRPGEquipSlot::Accessory3 };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(AccessorySlots); ++Index)
		{
			if (Row.AccessoryVisuals.IsValidIndex(Index) && !Row.AccessoryVisuals[Index].Mesh.IsNull()
				&& !EquippedIn(Member, AccessorySlots[Index]).IsNone())
			{
				Out.Attachments.Add(Row.AccessoryVisuals[Index]);
			}
		}

		const UDataTable* Table = GetEquipVisualTable();
		if (!Table)
		{
			return;
		}
		const UEnum* PieceEnum = StaticEnum<EJRPGBattlePiece>();
		auto FindRow = [&](EJRPGBattlePiece Piece, const FString& Item) -> const FJRPGEquipVisualRow*
		{
			const FName RowName(*FString::Printf(TEXT("%s_%s_%s"), *Character.ToString(),
				*PieceEnum->GetNameStringByValue(static_cast<int64>(Piece)), *Item));
			return Table->FindRow<FJRPGEquipVisualRow>(RowName, TEXT("BattleArena"), false);
		};
		auto ItemFor = [&](EJRPGBattlePiece Piece) -> FName
		{
			switch (Piece)
			{
			case EJRPGBattlePiece::Body:   return EquippedIn(Member, EJRPGEquipSlot::Armor);
			case EJRPGBattlePiece::Head:   return EquippedIn(Member, EJRPGEquipSlot::Helmet);
			case EJRPGBattlePiece::Weapon: return EquippedIn(Member, EJRPGEquipSlot::Weapon);
			case EJRPGBattlePiece::Feet:   return EquippedIn(Member, EJRPGEquipSlot::Shoes);
			case EJRPGBattlePiece::RaSeru: return FName(*FString::Printf(TEXT("Lv%d"), Member.SeruLevel));
			default:                       return NAME_None;
			}
		};

		TArray<FJRPGBattlePieceMesh> Pieces;
		bool bHasDefaults = false;
		for (int32 Index = 0; Index < static_cast<int32>(EJRPGBattlePiece::MAX); ++Index)
		{
			const EJRPGBattlePiece Piece = static_cast<EJRPGBattlePiece>(Index);
			const FJRPGEquipVisualRow* Default = FindRow(Piece, TEXT("Default"));
			bHasDefaults |= Default != nullptr;

			const FName Item = ItemFor(Piece);
			const FJRPGEquipVisualRow* Equipped = Item.IsNone() ? nullptr : FindRow(Piece, Item.ToString());
			if (!Equipped && !Item.IsNone() && Default)
			{
				UE_LOG(LogTemp, Warning, TEXT("BattleArena: %s — sem malha de %s na DT_EquipVisuals; vai a parte padrão."),
					*Character.ToString(), *Item.ToString());
			}
			const FJRPGEquipVisualRow* Chosen = Equipped ? Equipped : Default;
			if (Chosen && !Chosen->Mesh.IsNull())
			{
				FJRPGBattlePieceMesh& Part = Pieces.AddDefaulted_GetRef();
				Part.Piece = Piece;
				Part.Mesh = Chosen->Mesh;
			}
		}
		if (bHasDefaults)
		{
			Out.Pieces = MoveTemp(Pieces);
		}
	}

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
	PawnClass = AJRPGBattlePawn::StaticClass();

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

void AJRPGBattleArena::Warn(const FString& Message) const
{
	const FString Line = FString::Printf(TEXT("BattleArena %s: %s"), *GetActorNameOrLabel(), *Message);
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Orange, Line);
	}
}

// ---------------------------------------------------------------------------------------------
// A luta montada aqui
// ---------------------------------------------------------------------------------------------

void AJRPGBattleArena::AssembleBattle(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants)
{
	if (bBattleAssembled)
	{
		TeardownBattle();
	}

	int32 PartyCount = 0;
	int32 EnemyCount = 0;
	for (const FJRPGCombatant& C : Combatants)
	{
		(C.Side == EJRPGBattleSide::Party ? PartyCount : EnemyCount)++;
	}

	// Primeiro some o Field: a cápsula do jogador não pode atrapalhar o chão dos lugares
	HideFieldActors();
	for (AActor* Actor : HideDuringBattle)
	{
		HideActor(Actor, false);
	}
	DisableFieldInput();

	const TArray<FJRPGArenaSeat> Seats = GetFormation(PartyCount, EnemyCount);
	UClass* SpawnClass = PawnClass ? PawnClass.Get() : AJRPGBattlePawn::StaticClass();

	for (const FJRPGCombatant& C : Combatants)
	{
		const int32 SeatIndex = C.Side == EJRPGBattleSide::Party ? C.SideIndex : PartyCount + C.SideIndex;
		if (!Seats.IsValidIndex(SeatIndex))
		{
			continue;
		}
		const FJRPGArenaSeat& Seat = Seats[SeatIndex];
		if (Seat.Status == EJRPGArenaSeatStatus::Blocked)
		{
			Warn(FString::Printf(TEXT("%s num lugar bloqueado: %s"), *C.Key.ToString(), *Seat.Problem));
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		AJRPGBattlePawn* Pawn = GetWorld()->SpawnActor<AJRPGBattlePawn>(SpawnClass, Seat.Transform, Params);
		if (!Pawn)
		{
			Warn(FString::Printf(TEXT("não consegui criar o combatente %s"), *C.Key.ToString()));
			continue;
		}

		FJRPGBattleVisual Visual;
		FString Why;
		if (!ResolveVisual(C, Visual, Why))
		{
			Warn(FString::Printf(TEXT("%s sem modelo de batalha (%s) — só a cápsula"), *C.Key.ToString(), *Why));
		}
		Pawn->InitCombatant(C, Visual, Seat.Transform);
		BattlePawns.Add(Pawn);
	}

	FrameCamera();
	bBattleAssembled = true;
	Print(FString::Printf(TEXT("luta montada: %d herói(s), %d inimigo(s)"), PartyCount, EnemyCount), FColor::Cyan);
}

void AJRPGBattleArena::StopFieldForBattle()
{
	DisableFieldInput();
	for (TActorIterator<AJRPGFieldCharacter> It(GetWorld()); It; ++It)
	{
		// O seguidor anda por MoveTo: parar o caminho e a velocidade, senão ele desliza até parar
		if (AController* Controller = It->GetController())
		{
			Controller->StopMovement();
		}
		It->GetCharacterMovement()->StopMovementImmediately();
	}
}

void AJRPGBattleArena::TeardownBattle()
{
	for (AJRPGBattlePawn* Pawn : BattlePawns)
	{
		if (IsValid(Pawn))
		{
			Pawn->Destroy();
		}
	}
	BattlePawns.Reset();

	RestoreHiddenActors();
	RestoreFieldInput();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UCameraSubsystem* Camera = GI->GetSubsystem<UCameraSubsystem>())
		{
			Camera->RestoreToPlayerCamera(TransitionTime > 0.0f ? 0.0f : 0.5f);
		}
	}
	if (bBattleAssembled)
	{
		Print(TEXT("luta desmontada"), FColor::Cyan);
	}
	bBattleAssembled = false;
}

TArray<AJRPGBattlePawn*> AJRPGBattleArena::GetBattlePawns() const
{
	TArray<AJRPGBattlePawn*> Out;
	for (AJRPGBattlePawn* Pawn : BattlePawns)
	{
		if (IsValid(Pawn))
		{
			Out.Add(Pawn);
		}
	}
	return Out;
}

AJRPGBattlePawn* AJRPGBattleArena::GetPawnForSlot(int32 SlotIndex) const
{
	for (AJRPGBattlePawn* Pawn : BattlePawns)
	{
		if (IsValid(Pawn) && Pawn->GetCombatant().SlotIndex == SlotIndex)
		{
			return Pawn;
		}
	}
	return nullptr;
}

void AJRPGBattleArena::HideFieldActors()
{
	// O personagem controlado e os seguidores da party (todos AJRPGFieldCharacter)
	if (const APlayerController* PC = GetPlayerController())
	{
		HideActor(PC->GetPawn(), true);
	}
	for (TActorIterator<AJRPGFieldCharacter> It(GetWorld()); It; ++It)
	{
		HideActor(*It, true);
	}
}

void AJRPGBattleArena::HideActor(AActor* Actor, bool bFreeze)
{
	if (!IsValid(Actor))
	{
		return;
	}
	for (const FHiddenActor& Hidden : HiddenActors)
	{
		if (Hidden.Actor.Get() == Actor)
		{
			return;
		}
	}
	FHiddenActor& Entry = HiddenActors.AddDefaulted_GetRef();
	Entry.Actor = Actor;
	Entry.bWasHidden = Actor->IsHidden();
	Entry.bHadCollision = Actor->GetActorEnableCollision();
	Entry.TimeDilation = Actor->CustomTimeDilation;

	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	if (bFreeze)
	{
		// Parado de verdade: movimento, animação e o MoveTo dos seguidores
		Actor->CustomTimeDilation = 0.0f;
	}
}

void AJRPGBattleArena::RestoreHiddenActors()
{
	for (const FHiddenActor& Hidden : HiddenActors)
	{
		if (AActor* Actor = Hidden.Actor.Get())
		{
			Actor->SetActorHiddenInGame(Hidden.bWasHidden);
			Actor->SetActorEnableCollision(Hidden.bHadCollision);
			Actor->CustomTimeDilation = Hidden.TimeDilation;
		}
	}
	HiddenActors.Reset();
}

void AJRPGBattleArena::DisableFieldInput()
{
	if (bFieldInputOff)
	{
		return; // já tirado no toque (StopFieldForBattle): não regravar o estado "desligado"
	}
	bFieldInputOff = true;
	APlayerController* PC = GetPlayerController();
	InputController = PC;
	bRestoreFieldInput = false;
	bRestoreMoveInput = false;
	if (!PC)
	{
		return;
	}
	if (AJRPGFieldPlayerController* FieldPC = Cast<AJRPGFieldPlayerController>(PC))
	{
		bRestoreFieldInput = FieldPC->IsFieldInputEnabled();
		FieldPC->SetFieldInputEnabled(false);
	}
	else
	{
		PC->SetIgnoreMoveInput(true);
		bRestoreMoveInput = true;
	}
}

void AJRPGBattleArena::RestoreFieldInput()
{
	if (APlayerController* PC = InputController.Get())
	{
		if (bRestoreFieldInput)
		{
			if (AJRPGFieldPlayerController* FieldPC = Cast<AJRPGFieldPlayerController>(PC))
			{
				FieldPC->SetFieldInputEnabled(true);
			}
		}
		if (bRestoreMoveInput)
		{
			PC->SetIgnoreMoveInput(false);
		}
	}
	InputController = nullptr;
	bFieldInputOff = false;
	bRestoreFieldInput = false;
	bRestoreMoveInput = false;
}

void AJRPGBattleArena::FrameCamera()
{
	UGameInstance* GI = GetGameInstance();
	UCameraSubsystem* Camera = GI ? GI->GetSubsystem<UCameraSubsystem>() : nullptr;
	if (!Camera)
	{
		return;
	}
	// Com a tela preta a câmera corta; sem fade, desliza
	const float Blend = TransitionTime > 0.0f ? 0.0f : 0.75f;

	if (IsValid(BattleCamera))
	{
		Camera->FocusOnLevelCamera(BattleCamera, Blend, false, 0.0f);
		return;
	}

	// O preset gira em volta do primeiro do grupo e olha para o rosto dele: um inimigo primeiro
	// põe a câmera atrás da party
	const EJRPGBattleSide First = bCameraBehindParty ? EJRPGBattleSide::Enemy : EJRPGBattleSide::Party;
	TArray<AActor*> Group;
	for (AJRPGBattlePawn* Pawn : GetBattlePawns())
	{
		if (Pawn->GetCombatant().Side == First)
		{
			Group.Add(Pawn);
		}
	}
	for (AJRPGBattlePawn* Pawn : GetBattlePawns())
	{
		if (Pawn->GetCombatant().Side != First)
		{
			Group.Add(Pawn);
		}
	}
	if (Group.Num() > 0)
	{
		Camera->FrameGroup(Group, Blend, CameraPadding, CameraPreset, false);
	}
}

bool AJRPGBattleArena::ResolveVisual(const FJRPGCombatant& Combatant, FJRPGBattleVisual& OutVisual, FString& OutWhy) const
{
	if (Combatant.Side == EJRPGBattleSide::Party)
	{
		const UGameInstance* GI = GetGameInstance();
		const UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
		const UDataTable* Table = Party ? Party->CharacterTable.Get() : nullptr;
		const FCharacterData* Row = Table ? Table->FindRow<FCharacterData>(Combatant.Key, TEXT("BattleArena"), false) : nullptr;
		if (!Row)
		{
			OutWhy = TEXT("fora do DT_Characters");
			return false;
		}
		if (Row->BattleMesh.IsNull())
		{
			OutWhy = TEXT("BattleMesh vazio no DT_Characters");
			return false;
		}
		OutVisual.Mesh = Row->BattleMesh;
		OutVisual.IdleAnim = Row->BattleAnims.Idle;
		FJRPGPartyMember Member;
		if (Party->GetMember(Combatant.Key, Member))
		{
			AddEquipmentVisuals(Combatant.Key, *Row, Member, OutVisual);
		}
		return true;
	}

	FJRPGEnemyData Data;
	if (!UJRPGEncounterLibrary::GetEnemyData(Combatant.Key, Data))
	{
		OutWhy = TEXT("fora do DT_Enemies");
		return false;
	}
	if (Data.Mesh.IsNull())
	{
		OutWhy = TEXT("Mesh vazio no DT_Enemies");
		return false;
	}
	OutVisual.Mesh = Data.Mesh;
	OutVisual.IdleAnim = Data.Anims.Idle;
	return true;
}

APlayerController* AJRPGBattleArena::GetPlayerController() const
{
	return GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
}
