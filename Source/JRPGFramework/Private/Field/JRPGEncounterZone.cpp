#include "Field/JRPGEncounterZone.h"
#include "Battle/JRPGBattleArena.h"
#include "Battle/JRPGBattleSceneSubsystem.h"
#include "Battle/BattleSubsystem.h"
#include "Field/JRPGRandomEncounterSubsystem.h"
#include "World/WorldStateSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Tentativas de achar um ponto de chão dentro da forma. */
	constexpr int32 PointAttempts = 20;
	/** Sorteios de um ponto de spawn (PickSpawnPoint) antes de ficar com o menos ruim. */
	constexpr int32 SpawnAttempts = 16;
	/** De quanto em quanto tempo a zona confere as flags. */
	constexpr float FlagPollSeconds = 1.0f;
}

AJRPGEncounterZone::AJRPGEncounterZone()
{
	// Só no editor, para o preview; o BeginPlay desliga
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	AreaSphere = CreateDefaultSubobject<USphereComponent>(TEXT("AreaSphere"));
	AreaSphere->SetupAttachment(Root);
	AreaBox = CreateDefaultSubobject<UBoxComponent>(TEXT("AreaBox"));
	AreaBox->SetupAttachment(Root);
	for (UShapeComponent* Area : { static_cast<UShapeComponent*>(AreaSphere), static_cast<UShapeComponent*>(AreaBox) })
	{
		Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Area->SetGenerateOverlapEvents(false);
		Area->SetHiddenInGame(true);
		Area->ShapeColor = FColor(255, 60, 60);
		Area->SetLineThickness(3.0f);
	}

	// Spline: começa como um quadrado de 20 m de lado, com cantos retos
	AreaSpline = CreateDefaultSubobject<USplineComponent>(TEXT("AreaSpline"));
	AreaSpline->SetupAttachment(Root);
	AreaSpline->SetSplinePoints({ FVector(1000.0f, 1000.0f, 0.0f), FVector(1000.0f, -1000.0f, 0.0f),
		FVector(-1000.0f, -1000.0f, 0.0f), FVector(-1000.0f, 1000.0f, 0.0f) }, ESplineCoordinateSpace::Local, false);
	for (int32 i = 0; i < AreaSpline->GetNumberOfSplinePoints(); ++i)
	{
		AreaSpline->SetSplinePointType(i, ESplinePointType::Linear, false);
	}
	AreaSpline->SetClosedLoop(true, true);
	AreaSpline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AreaSpline->SetHiddenInGame(true);
#if WITH_EDITORONLY_DATA
	AreaSpline->EditorUnselectedSplineSegmentColor = FLinearColor(1.0f, 0.24f, 0.24f);
#endif

	EnemyClass = AJRPGFieldEnemy::StaticClass();

#if WITH_EDITORONLY_DATA
	EditorIcon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(TEXT("/JRPGFramework/Editor/T_EncounterZoneIcon.T_EncounterZoneIcon"));
		EditorIcon->Sprite = Icon.Get();
		EditorIcon->SetupAttachment(Root);
		EditorIcon->SetHiddenInGame(true);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}

void AJRPGEncounterZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SplineArea.Build(AreaSpline);
	AreaSphere->SetSphereRadius(Radius);
	AreaBox->SetBoxExtent(BoxExtent);
	AreaSphere->SetVisibility(Shape == EJRPGEncounterZoneShape::Sphere);
	AreaBox->SetVisibility(Shape == EJRPGEncounterZoneShape::Box);
	AreaSpline->SetVisibility(Shape == EJRPGEncounterZoneShape::Spline);
#if WITH_EDITOR
	RebuildPreview();
#endif
}

bool AJRPGEncounterZone::ShouldTickIfViewportsOnly() const
{
	return true;
}

void AJRPGEncounterZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld() || !bPreviewSpawns || (bPreviewOnlyWhenSelected && !IsSelectedInEditor()))
	{
		return;
	}
	float Radius = 40.0f;
	float HalfHeight = 90.0f;
	GetSpawnCapsule(nullptr, Radius, HalfHeight);
	const int32 Always = FMath::Min(MinAlive, MaxAlive);
	for (int32 i = 0; i < PreviewPoints.Num(); ++i)
	{
		const FVector& Floor = PreviewPoints[i];
		const FColor Color = PreviewImperfect.IsValidIndex(i) && PreviewImperfect[i] ? FColor(255, 210, 0)
			: i < Always ? FColor(60, 220, 90) : FColor(80, 160, 255);
		DrawDebugCapsule(World, Floor + FVector(0.0, 0.0, HalfHeight), HalfHeight, Radius, FQuat::Identity, Color, false, -1.0f, 0, 2.0f);
		if (MinSpacing > 0.0f)
		{
			DrawDebugCircle(World, Floor + FVector(0.0, 0.0, 3.0), MinSpacing * 0.5f, 32, Color, false, -1.0f, 0, 2.0f,
				FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		}
	}
#endif
}

void AJRPGEncounterZone::SnapAreaToGround()
{
	FJRPGSplineArea::SnapSplineToGround(AreaSpline, FloorSearchDistance, this);
	SplineArea.Build(AreaSpline);
}

EJRPGEncounterZoneShape AJRPGEncounterZone::GetEffectiveShape() const
{
	if (Shape == EJRPGEncounterZoneShape::Spline && !SplineArea.IsValid())
	{
		return EJRPGEncounterZoneShape::Sphere;
	}
	return Shape;
}

void AJRPGEncounterZone::BeginPlay()
{
	Super::BeginPlay();

	SetActorTickEnabled(false);   // o Tick é só o preview do editor
	SplineArea.Build(AreaSpline);
	if (Shape == EJRPGEncounterZoneShape::Spline && !SplineArea.IsValid())
	{
		Print(TEXT("Shape = Spline com menos de 3 pontos — usando o Radius como esfera"), FColor::Orange);
	}

	DesiredAlive = FMath::RandRange(FMath::Min(MinAlive, MaxAlive), FMath::Max(MinAlive, MaxAlive));
	if (GetEncounterSetup().Enemies.Num() == 0)
	{
		Print(TEXT("sem Enemy escolhido — os inimigos vão encostar e não fazer nada"), FColor::Orange);
	}

	bZoneActive = IsZoneActive();
	if (bZoneActive)
	{
		FillToDesired();
		Print(FString::Printf(TEXT("%d inimigo(s) criados (de %d..%d)"), Enemies.Num(), MinAlive, MaxAlive), FColor::Cyan);
	}
	else
	{
		Print(FString::Printf(TEXT("desligada pelas flags (Required %s, Disable %s)"), *RequiredFlagID.ToString(), *DisableFlagID.ToString()));
	}
	// As flags mudam no meio do jogo (a história): a zona confere e liga / desliga sozinha
	if (!RequiredFlagID.IsNone() || !DisableFlagID.IsNone())
	{
		GetWorldTimerManager().SetTimer(FlagPollTimer, this, &AJRPGEncounterZone::PollFlags, FlagPollSeconds, true);
	}
	// Os passos do jogador dentro dela: quem conta é o subsistema do mundo (um contador para todas as zonas)
	if (bRandomEncounters)
	{
		if (UJRPGRandomEncounterSubsystem* Random = GetWorld()->GetSubsystem<UJRPGRandomEncounterSubsystem>())
		{
			Random->RegisterZone(this);
		}
		if (GetEncounterSetup().Enemies.Num() == 0)
		{
			Print(TEXT("bRandomEncounters sem Enemy escolhido — o contador dispara e nenhuma luta sai"), FColor::Orange);
		}
	}
}

void AJRPGEncounterZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UJRPGRandomEncounterSubsystem* Random = World->GetSubsystem<UJRPGRandomEncounterSubsystem>())
		{
			Random->UnregisterZone(this);
		}
	}
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

bool AJRPGEncounterZone::IsZoneActive() const
{
	if (RequiredFlagID.IsNone() && DisableFlagID.IsNone())
	{
		return true;
	}
	const UGameInstance* GI = GetGameInstance();
	const UWorldStateSubsystem* World = GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (!World)
	{
		return false;
	}
	if (!RequiredFlagID.IsNone() && World->GetEventFlagValue(RequiredFlagID) != bRequiredFlagValue)
	{
		return false;
	}
	return DisableFlagID.IsNone() || !World->GetEventFlagValue(DisableFlagID);
}

void AJRPGEncounterZone::PollFlags()
{
	// Durante uma luta o Field está guardado: a troca fica para quando ele voltar
	const UWorld* World = GetWorld();
	const UJRPGBattleSceneSubsystem* Scene = World ? World->GetSubsystem<UJRPGBattleSceneSubsystem>() : nullptr;
	if (Scene && Scene->IsFieldHidden())
	{
		return;
	}
	const bool bNow = IsZoneActive();
	if (bNow == bZoneActive)
	{
		return;
	}
	bZoneActive = bNow;
	if (!bZoneActive)
	{
		// Desligou: todos somem, e ninguém volta sozinho
		for (const TPair<TWeakObjectPtr<AJRPGFieldEnemy>, FTimerHandle>& Pair : RespawnTimers)
		{
			FTimerHandle Handle = Pair.Value;
			GetWorldTimerManager().ClearTimer(Handle);
		}
		RespawnTimers.Reset();
		for (AJRPGFieldEnemy* Enemy : GetEnemies())
		{
			Enemy->HideForZone();
		}
		Print(TEXT("desligou pelas flags — os inimigos sumiram"), FColor::Orange);
		return;
	}
	// Religou: os sumidos voltam agora, e os que faltam nascem
	for (AJRPGFieldEnemy* Enemy : GetEnemies())
	{
		if (Enemy->GetEnemyState() == EJRPGFieldEnemyState::Hidden)
		{
			Respawn(Enemy);
		}
	}
	FillToDesired();
	Print(TEXT("ligou pelas flags — os inimigos voltaram"), FColor::Cyan);
}

void AJRPGEncounterZone::FillToDesired()
{
	const int32 Missing = DesiredAlive - Enemies.Num();
	for (int32 i = 0; i < Missing; ++i)
	{
		SpawnEnemy();
	}
}

bool AJRPGEncounterZone::ContainsPoint(const FVector& Point) const
{
	if (DistanceOutside(Point) > 0.0f)
	{
		return false;
	}
	// A faixa de altura: a mesma em que a zona procura o chão (mapas com andares, pontes)
	const float Height = GetEffectiveShape() == EJRPGEncounterZoneShape::Box ? FMath::Max(BoxExtent.Z, 100.0f) : FloorSearchDistance;
	return FMath::Abs(Point.Z - GetActorLocation().Z) <= Height;
}

int32 AJRPGEncounterZone::GetEffectiveEncounterRate() const
{
	if (!bRandomEncounters || !bZoneActive)
	{
		return 0;
	}
	bool bLowRate = false;
	if (!LowRateFlagID.IsNone())
	{
		const UGameInstance* GI = GetGameInstance();
		const UWorldStateSubsystem* WorldState = GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
		bLowRate = WorldState && WorldState->GetEventFlagValue(LowRateFlagID);
	}
	return UJRPGEncounterLibrary::ScaleEncounterRate(EncounterRate, bLowRate, LowRatePercent);
}

bool AJRPGEncounterZone::StartRandomEncounter(APawn* Player)
{
	const TArray<FJRPGEncounterCombatant> Rolled = UJRPGEncounterLibrary::RollEncounter(GetEncounterSetup());
	if (Rolled.Num() == 0)
	{
		Print(TEXT("encontro aleatório, mas a lista está vazia (Enemy / Custom Encounter) — nada a fazer"), FColor::Red);
		return false;
	}
	const UGameInstance* GI = GetGameInstance();
	UBattleSubsystem* Battle = GI ? GI->GetSubsystem<UBattleSubsystem>() : nullptr;
	if (!Battle || !Battle->TryStartEncounter(Rolled))
	{
		return false;
	}

	const FVector Where = Player ? Player->GetActorLocation() : GetActorLocation();
	FJRPGFieldEncounter Info;
	Info.Enemies = Rolled;
	Info.Zone = this;
	Info.ContactLocation = Where;
	AJRPGBattleArena* ZoneArena = GetBattleArena();
	Info.Arena = ZoneArena ? ZoneArena : AJRPGBattleArena::FindArenaFor(this, Where);

	TArray<FString> Names;
	for (const FJRPGEncounterCombatant& C : Rolled)
	{
		FJRPGEnemyData Data;
		Names.Add(UJRPGEncounterLibrary::GetEnemyData(C.EnemyKey, Data)
			? FString::Printf(TEXT("%s Lv%d"), *Data.DisplayName.ToString(), C.Level)
			: FString::Printf(TEXT("%s Lv%d (fora do DT_Enemies!)"), *C.EnemyKey.ToString(), C.Level));
	}
	Print(FString::Printf(TEXT("ENCONTRO ALEATÓRIO! %s%s"), *FString::Join(Names, TEXT(", ")),
		Info.Arena ? *FString::Printf(TEXT(" — arena %s"), *Info.Arena->GetActorNameOrLabel()) : TEXT(" — sem arena, montada onde o jogador está")),
		FColor::Red);
	OnRandomEncounter.Broadcast(Info);

	FJRPGBattleSetup Setup;
	Setup.Enemies = Rolled;
	Setup.bScriptedFight = UJRPGEncounterLibrary::ResolveScriptedFight(Rolled);
	Setup.Location = Where;
	Setup.Arena = ZoneArena;
	// Sem arena, a party olha para onde o jogador olhava
	Setup.FacingYaw = Player ? Player->GetActorRotation().Yaw : GetActorRotation().Yaw;
	return Battle->BeginBattle(Setup);
}

AJRPGBattleArena* AJRPGEncounterZone::GetBattleArena() const
{
	AJRPGBattleArena* Arena = BattleArena.Get();
	return IsValid(Arena) && Arena->bEnabled && Arena->GetWorld() == GetWorld() ? Arena : nullptr;
}

FJRPGEncounterSetup AJRPGEncounterZone::GetEncounterSetup() const
{
	return Encounter.Enemies.Num() > 0 ? Encounter : UJRPGEncounterLibrary::MakeSingleEnemyEncounter(EnemyKey, LevelMin, LevelMax);
}

void AJRPGEncounterZone::SpawnEnemy()
{
	UClass* Class = EnemyClass ? EnemyClass.Get() : AJRPGFieldEnemy::StaticClass();
	const AJRPGFieldEnemy* ClassCDO = Class->GetDefaultObject<AJRPGFieldEnemy>();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	FVector Floor;
	if (!PickSpawnPoint(nullptr, Player, ClassCDO->DetectRadius + 200.0f, Floor))
	{
		Print(FString::Printf(TEXT("não achei um ponto livre dentro da zona para criar um inimigo (chão a %.0f acima e abaixo do ator — FloorSearchDistance; ou tudo dentro de objetos)"),
			GetEffectiveShape() == EJRPGEncounterZoneShape::Box ? FMath::Max(BoxExtent.Z, 100.0f) : FloorSearchDistance), FColor::Red);
		return;
	}

	// Nasce com os pés no chão; o inimigo reajusta a cápsula ao tamanho da malha no BeginPlay
	const AJRPGFieldEnemy* CDO = Class->GetDefaultObject<AJRPGFieldEnemy>();
	const float Half = CDO->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform Spawn(FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f), Floor + FVector(0.0f, 0.0f, Half));

	AJRPGFieldEnemy* Enemy = GetWorld()->SpawnActorDeferred<AJRPGFieldEnemy>(Class, Spawn, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Enemy)
	{
		return;
	}
	Enemy->InitFromZone(this);
	Enemy->bPrintDebug = bPrintDebug;
	Enemy->FinishSpawning(Spawn);
	Enemies.Add(Enemy);
}

void AJRPGEncounterZone::NotifyEnemyHidden(AJRPGFieldEnemy* Enemy)
{
	if (!bZoneActive)
	{
		return;   // a zona desligada traz de volta ao religar (PollFlags)
	}
	const float Delay = FMath::FRandRange(RespawnDelayMin, FMath::Max(RespawnDelayMin, RespawnDelayMax));
	FTimerHandle& Handle = RespawnTimers.FindOrAdd(Enemy);
	GetWorldTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateUObject(this, &AJRPGEncounterZone::Respawn, TWeakObjectPtr<AJRPGFieldEnemy>(Enemy)),
		Delay, false);
	Print(FString::Printf(TEXT("%s volta em %.0fs"), *Enemy->GetActorNameOrLabel(), Delay));
}

void AJRPGEncounterZone::Respawn(TWeakObjectPtr<AJRPGFieldEnemy> Enemy)
{
	if (FTimerHandle* Handle = RespawnTimers.Find(Enemy))
	{
		GetWorldTimerManager().ClearTimer(*Handle);
		RespawnTimers.Remove(Enemy);
	}
	if (!Enemy.IsValid() || !bZoneActive)
	{
		return;
	}
	// Longe do jogador (reaparecer em cima dele começaria outra luta na hora), espaçado dos outros e fora de objetos
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	FVector Floor;
	if (!PickSpawnPoint(Enemy.Get(), Player, Enemy->DetectRadius + 200.0f, Floor))
	{
		NotifyEnemyHidden(Enemy.Get());
		return;
	}
	Enemy->RespawnAtFloor(Floor);
}

void AJRPGEncounterZone::GetSpawnCapsule(const AJRPGFieldEnemy* Self, float& OutRadius, float& OutHalfHeight) const
{
	const AJRPGFieldEnemy* From = Self;
	if (!From)
	{
		UClass* Class = EnemyClass ? EnemyClass.Get() : AJRPGFieldEnemy::StaticClass();
		From = Class->GetDefaultObject<AJRPGFieldEnemy>();
	}
	const UCapsuleComponent* Capsule = From ? From->GetCapsuleComponent() : nullptr;
	OutRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 40.0f;
	OutHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
}

bool AJRPGEncounterZone::PickSpawnPoint(const AJRPGFieldEnemy* Self, const AActor* AvoidActor, float MinDistance, FVector& OutFloor) const
{
	// Os outros da zona à vista: o espaçamento conta deles
	TArray<FVector> Others;
	for (const TObjectPtr<AJRPGFieldEnemy>& Other : Enemies)
	{
		if (Other && Other != Self && Other->GetEnemyState() != EJRPGFieldEnemyState::Hidden && !Other->IsHidden())
		{
			Others.Add(Other->GetActorLocation());
		}
	}
	float Penalty = 0.0f;
	if (!PickPointAmong(Others, Self, AvoidActor, MinDistance, OutFloor, Penalty))
	{
		return false;
	}
	if (Penalty > 0.0f)
	{
		Print(FString::Printf(TEXT("sem ponto perfeito para nascer: ficou o que passa %.0f cm das distâncias (zona pequena para o MinSpacing / MaxSpacing?)"), Penalty), FColor::Yellow);
	}
	return true;
}

bool AJRPGEncounterZone::PickPointAmong(const TArray<FVector>& Others, const AJRPGFieldEnemy* Self, const AActor* AvoidActor, float MinDistance,
	FVector& OutFloor, float& OutPenalty) const
{
	OutPenalty = 0.0f;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	float Radius = 40.0f;
	float HalfHeight = 90.0f;
	GetSpawnCapsule(Self, Radius, HalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGEncounterZoneSpawn), false, this);
	if (Self)
	{
		Params.AddIgnoredActor(Self);
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius + ObstacleClearance, HalfHeight + ObstacleClearance);
	// A cápsula sobe um pouco do chão: o próprio chão (e uma rampa leve) não conta como obstáculo
	const float Lift = HalfHeight + ObstacleClearance + Radius * 0.5f + 5.0f;

	bool bFound = false;
	float BestPenalty = TNumericLimits<float>::Max();
	for (int32 Attempt = 0; Attempt < SpawnAttempts; ++Attempt)
	{
		FVector Point;
		if (!GetRandomFloorPoint(Point))
		{
			continue;
		}
		if (bAvoidObstacles && World->OverlapAnyTestByObjectType(Point + FVector(0.0f, 0.0f, Lift), FQuat::Identity, Objects, Capsule, Params))
		{
			continue;   // dentro de algo: nunca serve
		}

		// O quanto passa das distâncias (0 = perfeito)
		float Penalty = 0.0f;
		if (AvoidActor)
		{
			Penalty += FMath::Max(0.0f, MinDistance - static_cast<float>(FVector::Dist2D(Point, AvoidActor->GetActorLocation())));
		}
		if (Others.Num() > 0)
		{
			float Nearest = TNumericLimits<float>::Max();
			for (const FVector& Other : Others)
			{
				Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Point, Other)));
			}
			Penalty += FMath::Max(0.0f, MinSpacing - Nearest);
			if (MaxSpacing > 0.0f)
			{
				Penalty += FMath::Max(0.0f, Nearest - FMath::Max(MaxSpacing, MinSpacing));
			}
		}
		if (Penalty <= 0.0f)
		{
			OutFloor = Point;
			return true;
		}
		if (Penalty < BestPenalty)
		{
			BestPenalty = Penalty;
			OutFloor = Point;
			bFound = true;
		}
	}
	OutPenalty = bFound ? BestPenalty : 0.0f;
	return bFound;
}

void AJRPGEncounterZone::RerollPreview()
{
#if WITH_EDITOR
	RebuildPreview();
#endif
}

#if WITH_EDITOR
void AJRPGEncounterZone::ClearPreviewMeshes()
{
	for (USkeletalMeshComponent* Mesh : PreviewMeshes)
	{
		if (IsValid(Mesh))
		{
			Mesh->DestroyComponent();
		}
	}
	PreviewMeshes.Reset();
}

void AJRPGEncounterZone::RebuildPreview()
{
	ClearPreviewMeshes();
	PreviewPoints.Reset();
	PreviewImperfect.Reset();
	PreviewMissing = 0;
	UWorld* World = GetWorld();
	if (!World || World->IsGameWorld() || IsTemplate() || !bPreviewSpawns)
	{
		return;
	}

	// Os pontos, um por vez, com as regras do jogo: cada um conta os anteriores como "os outros"
	const int32 Count = FMath::Max(MinAlive, MaxAlive);
	for (int32 i = 0; i < Count; ++i)
	{
		FVector Floor;
		float Penalty = 0.0f;
		if (!PickPointAmong(PreviewPoints, nullptr, nullptr, 0.0f, Floor, Penalty))
		{
			++PreviewMissing;
			continue;
		}
		PreviewPoints.Add(Floor);
		PreviewImperfect.Add(Penalty > 0.0f);
	}
	if (PreviewMissing > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("EncounterZone %s: %d de %d inimigo(s) do preview sem lugar (sem chão na zona, ou tudo dentro de objetos)"),
			*GetActorNameOrLabel(), PreviewMissing, Count);
	}

	if (!bPreviewMeshes)
	{
		return;
	}
	// Os modelos: a lista Visuals, senão o do DT_Enemies de cada inimigo do encontro que tenha um, um por ponto
	struct FPreviewVisual
	{
		USkeletalMesh* Mesh = nullptr;
		float Scale = 1.0f;
	};
	TArray<FPreviewVisual> Models;
	for (const FJRPGFieldEnemyVisual& Visual : Visuals)
	{
		if (USkeletalMesh* Mesh = Visual.Mesh.LoadSynchronous())
		{
			Models.Add({ Mesh, Visual.Scale });
		}
	}
	if (Models.Num() == 0)
	{
		for (const FJRPGEncounterEnemy& Line : GetEncounterSetup().Enemies)
		{
			FJRPGEnemyData Data;
			if (UJRPGEncounterLibrary::GetEnemyData(Line.EnemyKey, Data))
			{
				if (USkeletalMesh* Mesh = Data.Mesh.LoadSynchronous())
				{
					Models.Add({ Mesh, 1.0f });
				}
			}
		}
	}
	if (Models.Num() == 0)
	{
		return;
	}
	UClass* Class = EnemyClass ? EnemyClass.Get() : AJRPGFieldEnemy::StaticClass();
	const float MeshYaw = Class->GetDefaultObject<AJRPGFieldEnemy>()->MeshYawOffset;
	for (int32 i = 0; i < PreviewPoints.Num(); ++i)
	{
		const FPreviewVisual& Model = Models[i % Models.Num()];
		// Componente de construção, transitório e só do editor: nunca é salvo nem vai para o jogo
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient | RF_TextExportTransient);
		Mesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Mesh->bIsEditorOnly = true;
		Mesh->SetHiddenInGame(true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetSkeletalMeshAsset(Model.Mesh);
		Mesh->SetupAttachment(Root);
		Mesh->RegisterComponent();
		// Os pés no chão do ponto (como o ApplyVisual do inimigo: a base dos bounds da malha na sola)
		const FBoxSphereBounds Bounds = Model.Mesh->GetBounds();
		const double MinZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Model.Scale;
		const float Yaw = static_cast<float>(i) * 137.5f + MeshYaw;
		Mesh->SetWorldTransform(FTransform(FRotator(0.0f, Yaw, 0.0f), PreviewPoints[i] - FVector(0.0, 0.0, MinZ), FVector(Model.Scale)));
		PreviewMeshes.Add(Mesh);
	}
}
#endif

bool AJRPGEncounterZone::GetRandomFloorPointAway(const FVector& Avoid, float MinDistance, FVector& OutFloor) const
{
	// Sorteia até achar um ponto a MinDistance (no plano) de Avoid; numa zona pequena demais, o mais longe dos sorteados
	bool bFound = false;
	float BestDistance = -1.0f;
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		FVector Point;
		if (!GetRandomFloorPoint(Point))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Point, Avoid));
		if (Distance >= MinDistance)
		{
			OutFloor = Point;
			return true;
		}
		if (Distance > BestDistance)
		{
			BestDistance = Distance;
			OutFloor = Point;
			bFound = true;
		}
	}
	return bFound;
}

bool AJRPGEncounterZone::GetRandomFloorPoint(FVector& OutFloor) const
{
	const UWorld* World = GetWorld();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const FTransform& T = GetActorTransform();
	const EJRPGEncounterZoneShape Effective = GetEffectiveShape();
	const float SearchZ = Effective == EJRPGEncounterZoneShape::Box ? FMath::Max(BoxExtent.Z, 100.0f) : FloorSearchDistance;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGEncounterZoneFloor), false, this);

	// Chão achado mas fora do navmesh: serve se nenhum ponto cair no navmesh (mapa sem navmesh,
	// ou ele ainda não foi gerado) — o inimigo anda reto quando não há caminho
	TOptional<FVector> FloorWithoutNav;

	for (int32 i = 0; i < PointAttempts; ++i)
	{
		FVector Candidate;
		if (Effective == EJRPGEncounterZoneShape::Spline)
		{
			// O contorno já está em espaço de mundo; a altura é a do ator
			FVector2D P;
			if (!SplineArea.RandomPoint(P))
			{
				continue;
			}
			Candidate = FVector(P.X, P.Y, T.GetLocation().Z);
		}
		else if (Effective == EJRPGEncounterZoneShape::Sphere)
		{
			const FVector2D P = FMath::RandPointInCircle(Radius);
			Candidate = T.TransformPosition(FVector(P.X, P.Y, 0.0f));
		}
		else
		{
			Candidate = T.TransformPosition(FVector(FMath::FRandRange(-BoxExtent.X, BoxExtent.X), FMath::FRandRange(-BoxExtent.Y, BoxExtent.Y), 0.0f));
		}

		// O chão embaixo (ou em cima) do ponto: a zona pode estar no ar ou meio enterrada
		FHitResult Hit;
		const bool bFloor = World->LineTraceSingleByChannel(Hit, Candidate + FVector(0.0f, 0.0f, SearchZ),
			Candidate - FVector(0.0f, 0.0f, SearchZ), ECC_Visibility, Params);

		if (Nav)
		{
			// Com chão achado, projeta perto dele; sem, procura o navmesh na altura toda
			const FVector From = bFloor ? FVector(Hit.ImpactPoint) : Candidate;
			const FVector Extent(50.0f, 50.0f, bFloor ? 150.0f : SearchZ);
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(From, OnNav, Extent) && DistanceOutside(OnNav.Location) <= 0.0f)
			{
				OutFloor = OnNav.Location;
				return true;
			}
			if (bFloor && !FloorWithoutNav.IsSet())
			{
				FloorWithoutNav = FVector(Hit.ImpactPoint);
			}
			continue;
		}

		if (bFloor)
		{
			OutFloor = Hit.ImpactPoint;
			return true;
		}
	}

	if (FloorWithoutNav.IsSet())
	{
		if (!bWarnedNoNavmesh)
		{
			bWarnedNoNavmesh = true;
			// Aviso sempre, com ou sem bPrintDebug: é problema do mapa, não da zona
			const FString Line = FString::Printf(TEXT("EncounterZone %s: o navmesh não cobre a zona (Build > Build Paths, ou falta NavMeshBoundsVolume) — os inimigos nascem no chão e andam reto"), *GetActorNameOrLabel());
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Line);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Orange, Line);
			}
		}
		OutFloor = FloorWithoutNav.GetValue();
		return true;
	}
	return false;
}

float AJRPGEncounterZone::DistanceOutside(const FVector& Point) const
{
	const EJRPGEncounterZoneShape Effective = GetEffectiveShape();
	if (Effective == EJRPGEncounterZoneShape::Spline)
	{
		return SplineArea.DistanceOutside(Point);
	}
	if (Effective == EJRPGEncounterZoneShape::Sphere)
	{
		// A esfera desenhada (e os pontos sorteados) crescem com a escala do ator
		const FVector Scale = GetActorScale3D();
		const float ScaledRadius = Radius * FMath::Max(FMath::Abs(Scale.X), FMath::Abs(Scale.Y));
		return FMath::Max(0.0f, FVector::Dist2D(Point, GetActorLocation()) - ScaledRadius);
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Point);
	const float Dx = FMath::Max(0.0f, FMath::Abs(Local.X) - BoxExtent.X);
	const float Dy = FMath::Max(0.0f, FMath::Abs(Local.Y) - BoxExtent.Y);
	return FMath::Sqrt(Dx * Dx + Dy * Dy);
}

TArray<AJRPGFieldEnemy*> AJRPGEncounterZone::GetEnemies() const
{
	TArray<AJRPGFieldEnemy*> Out;
	for (const TObjectPtr<AJRPGFieldEnemy>& E : Enemies)
	{
		if (E)
		{
			Out.Add(E.Get());
		}
	}
	return Out;
}

void AJRPGEncounterZone::Print(const FString& Message, const FColor& Color) const
{
	if (!bPrintDebug)
	{
		return;
	}
	const FString Line = FString::Printf(TEXT("EncounterZone %s: %s"), *GetActorNameOrLabel(), *Message);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Line);
	}
}
