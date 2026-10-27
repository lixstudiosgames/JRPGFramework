#include "Field/JRPGEncounterZone.h"
#include "World/WorldStateSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Tentativas de achar um ponto de chão dentro da forma. */
	constexpr int32 PointAttempts = 20;
}

AJRPGEncounterZone::AJRPGEncounterZone()
{
	PrimaryActorTick.bCanEverTick = false;

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

	SplineArea.Build(AreaSpline);
	if (Shape == EJRPGEncounterZoneShape::Spline && !SplineArea.IsValid())
	{
		Print(TEXT("Shape = Spline com menos de 3 pontos — usando o Radius como esfera"), FColor::Orange);
	}

	if (!IsUnlocked())
	{
		Print(FString::Printf(TEXT("travada pelo flag %s"), *RequiredFlagID.ToString()));
		return;
	}
	if (Encounter.Enemies.Num() == 0)
	{
		Print(TEXT("sem inimigos no Encounter — os inimigos vão encostar e não fazer nada"), FColor::Orange);
	}
	for (int32 i = 0; i < MaxAlive; ++i)
	{
		SpawnEnemy();
	}
	Print(FString::Printf(TEXT("%d inimigo(s) criados"), Enemies.Num()), FColor::Cyan);
}

void AJRPGEncounterZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

bool AJRPGEncounterZone::IsUnlocked() const
{
	if (RequiredFlagID.IsNone())
	{
		return true;
	}
	const UGameInstance* GI = GetGameInstance();
	const UWorldStateSubsystem* World = GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	return World && World->GetEventFlagValue(RequiredFlagID) == bRequiredFlagValue;
}

void AJRPGEncounterZone::SpawnEnemy()
{
	UClass* Class = EnemyClass ? EnemyClass.Get() : AJRPGFieldEnemy::StaticClass();
	FVector Floor;
	if (!GetRandomFloorPoint(Floor))
	{
		Print(TEXT("não achei chão dentro da zona para criar um inimigo"), FColor::Red);
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
	const float Delay = FMath::FRandRange(RespawnDelayMin, FMath::Max(RespawnDelayMin, RespawnDelayMax));
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateUObject(this, &AJRPGEncounterZone::Respawn, TWeakObjectPtr<AJRPGFieldEnemy>(Enemy)),
		Delay, false);
	Print(FString::Printf(TEXT("%s volta em %.0fs"), *Enemy->GetActorNameOrLabel(), Delay));
}

void AJRPGEncounterZone::Respawn(TWeakObjectPtr<AJRPGFieldEnemy> Enemy)
{
	if (!Enemy.IsValid())
	{
		return;
	}
	if (!IsUnlocked())
	{
		// O flag mudou enquanto ele estava sumido: tenta de novo mais tarde
		NotifyEnemyHidden(Enemy.Get());
		return;
	}
	FVector Floor;
	if (!GetRandomFloorPoint(Floor))
	{
		NotifyEnemyHidden(Enemy.Get());
		return;
	}
	Enemy->RespawnAtFloor(Floor);
}

bool AJRPGEncounterZone::GetRandomFloorPoint(FVector& OutFloor) const
{
	const UWorld* World = GetWorld();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const FTransform& T = GetActorTransform();
	const EJRPGEncounterZoneShape Effective = GetEffectiveShape();
	const float SearchZ = Effective == EJRPGEncounterZoneShape::Box ? FMath::Max(BoxExtent.Z, 100.0f) : FloorSearchDistance;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGEncounterZoneFloor), false, this);

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
			continue;
		}

		if (bFloor)
		{
			OutFloor = Hit.ImpactPoint;
			return true;
		}
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
		return FMath::Max(0.0f, FVector::Dist2D(Point, GetActorLocation()) - Radius);
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
