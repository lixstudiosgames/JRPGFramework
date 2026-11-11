#include "World/JRPGMistBlocker.h"
#include "World/JRPGMistVolume.h"
#include "World/WorldStateSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	/** Ao entrar no mapa o save restaura as flags depois do BeginPlay: mudança até aqui não tem transição. */
	constexpr double SnapWindowSeconds = 2.0;

	/** Grade usada para achar o "centro" (a maior distância até a borda) de um spline. */
	constexpr int32 MaxInsideGrid = 40;
}

AJRPGMistBlocker::AJRPGMistBlocker()
{
	// O Tick só anda a transição: liga quando ela começa e desliga no fim
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	AreaCircle = CreateDefaultSubobject<USphereComponent>(TEXT("AreaCircle"));
	AreaCircle->SetupAttachment(Root);
	AreaBox = CreateDefaultSubobject<UBoxComponent>(TEXT("AreaBox"));
	AreaBox->SetupAttachment(Root);
	for (UShapeComponent* Area : { static_cast<UShapeComponent*>(AreaCircle), static_cast<UShapeComponent*>(AreaBox) })
	{
		Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Area->SetGenerateOverlapEvents(false);
		Area->SetCanEverAffectNavigation(false);
		Area->SetHiddenInGame(true);
		Area->ShapeColor = FColor(120, 230, 255);
		Area->SetLineThickness(3.0f);
	}

	// Spline: começa como um quadrado de 30 m de lado, com cantos retos
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
	AreaSpline->EditorUnselectedSplineSegmentColor = FLinearColor(0.47f, 0.9f, 1.0f);
#endif
}

void AJRPGMistBlocker::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	AreaCircle->SetSphereRadius(Radius);
	AreaBox->SetBoxExtent(FVector(BoxExtent.X, BoxExtent.Y, 200.0f));
	AreaCircle->SetVisibility(Shape == EJRPGMistBlockerShape::Circle);
	AreaBox->SetVisibility(Shape == EJRPGMistBlockerShape::Box);
	AreaSpline->SetVisibility(Shape == EJRPGMistBlockerShape::Spline);
	RebuildArea();

	// No editor: o preview (as condições não existem fora do jogo). No jogo o BeginPlay decide
	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		Progress = Target = bPreviewInEditor ? 1.0f : 0.0f;
		NotifyVolumes(true);
	}
}

void AJRPGMistBlocker::RebuildArea()
{
	SplineArea.Build(AreaSpline);

	switch (Shape)
	{
	case EJRPGMistBlockerShape::Box:
	{
		const FVector Extent = AreaBox->GetScaledBoxExtent();
		MaxInside = static_cast<float>(FMath::Min(Extent.X, Extent.Y));
		break;
	}
	case EJRPGMistBlockerShape::Spline:
		if (SplineArea.IsValid())
		{
			// O ponto mais longe da borda, numa grade: o "centro" de onde o Recede começa
			MaxInside = 1.0f;
			const FBox2D B = SplineArea.Bounds;
			const FVector2D Size = B.GetSize();
			for (int32 y = 0; y < MaxInsideGrid; ++y)
			{
				for (int32 x = 0; x < MaxInsideGrid; ++x)
				{
					const FVector2D P = B.Min + FVector2D((x + 0.5) / MaxInsideGrid * Size.X, (y + 0.5) / MaxInsideGrid * Size.Y);
					MaxInside = FMath::Max(MaxInside, GetSignedDistance(P));
				}
			}
			break;
		}
		// Menos de 3 pontos: vira círculo (GetSignedDistance faz o mesmo)
		[[fallthrough]];
	case EJRPGMistBlockerShape::Circle:
	default:
		MaxInside = AreaCircle->GetScaledSphereRadius();
		break;
	}
	MaxInside = FMath::Max(MaxInside, 1.0f);
}

float AJRPGMistBlocker::GetSignedDistance(const FVector2D& Point) const
{
	if (Shape == EJRPGMistBlockerShape::Box)
	{
		const FVector Extent = AreaBox->GetScaledBoxExtent();
		const FVector Local = AreaBox->GetComponentTransform().InverseTransformPositionNoScale(
			FVector(Point.X, Point.Y, AreaBox->GetComponentLocation().Z));
		const float Inside = static_cast<float>(FMath::Min(Extent.X - FMath::Abs(Local.X), Extent.Y - FMath::Abs(Local.Y)));
		return Inside > 0.0f ? Inside : -1.0f;
	}
	if (Shape == EJRPGMistBlockerShape::Spline && SplineArea.IsValid())
	{
		const FVector P3(Point.X, Point.Y, 0.0f);
		if (!SplineArea.Contains(P3))
		{
			return -1.0f;
		}
		return FMath::Max(SplineArea.DistanceInside(P3), 0.001f);
	}
	const FVector Center = AreaCircle->GetComponentLocation();
	return AreaCircle->GetScaledSphereRadius() - static_cast<float>(FVector2D::Distance(Point, FVector2D(Center.X, Center.Y)));
}

FBox2D AJRPGMistBlocker::GetAreaBounds() const
{
	if (Shape == EJRPGMistBlockerShape::Spline && SplineArea.IsValid())
	{
		return SplineArea.Bounds;
	}
	if (Shape == EJRPGMistBlockerShape::Box)
	{
		const FBox Box = AreaBox->CalcBounds(AreaBox->GetComponentTransform()).GetBox();
		return FBox2D(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y));
	}
	const FVector Center = AreaCircle->GetComponentLocation();
	const float R = AreaCircle->GetScaledSphereRadius();
	return FBox2D(FVector2D(Center.X - R, Center.Y - R), FVector2D(Center.X + R, Center.Y + R));
}

float AJRPGMistBlocker::GetBlockAmount(float SignedDistance) const
{
	if (SignedDistance <= 0.0f || Progress <= 0.0f || Strength <= 0.0f)
	{
		return 0.0f;
	}
	// A borda nunca é mais larga que a forma: senão o centro de uma área pequena não limpava
	const float Soft = FMath::Clamp(EdgeSoftness, 1.0f, MaxInside);
	const float Eased = FMath::SmoothStep(0.0f, 1.0f, Progress);
	float Amount;
	if (Transition == EJRPGMistBlockerTransition::Recede)
	{
		// A frente da limpeza anda do centro (MaxInside) até a borda (0)
		const float Front = (1.0f - Eased) * MaxInside;
		Amount = FMath::SmoothStep(0.0f, Soft, SignedDistance - Front);
	}
	else
	{
		Amount = Eased * FMath::SmoothStep(0.0f, Soft, SignedDistance);
	}
	return FMath::Clamp(Amount * Strength, 0.0f, 1.0f);
}

bool AJRPGMistBlocker::ShouldBlock() const
{
	if (RequiredFlagID.IsNone() && DisableFlagID.IsNone() && RevivalTreeID.IsNone())
	{
		return true;
	}
	const UGameInstance* GI = GetGameInstance();
	const UWorldStateSubsystem* WorldState = GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	if (!WorldState)
	{
		return false;
	}
	if (!RequiredFlagID.IsNone() && WorldState->GetEventFlagValue(RequiredFlagID) != bRequiredFlagValue)
	{
		return false;
	}
	if (!DisableFlagID.IsNone() && WorldState->GetEventFlagValue(DisableFlagID))
	{
		return false;
	}
	if (!RevivalTreeID.IsNone())
	{
		// Árvore sem estado registrado = Idle
		FRevivalTreeState State;
		const ERevivalTreeStage Stage = WorldState->GetRevivalTreeState(RevivalTreeID, State) ? State.Stage : ERevivalTreeStage::Idle;
		if (static_cast<uint8>(Stage) < static_cast<uint8>(RevivalTreeStage))
		{
			return false;
		}
	}
	return true;
}

void AJRPGMistBlocker::BeginPlay()
{
	Super::BeginPlay();

	SetActorTickEnabled(false);
	RebuildArea();

	// Já começa no estado certo, sem transição
	Progress = Target = ShouldBlock() ? 1.0f : 0.0f;
	SnapUntilTime = GetWorld()->GetTimeSeconds() + SnapWindowSeconds;
	NotifyVolumes(true);

	if (!RequiredFlagID.IsNone() || !DisableFlagID.IsNone() || !RevivalTreeID.IsNone())
	{
		GetWorldTimerManager().SetTimer(PollTimer, this, &AJRPGMistBlocker::PollState, FMath::Max(FlagPollSeconds, 0.1f), true);
	}
}

void AJRPGMistBlocker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PollTimer);
	// Troca de mapa: os volumes saem junto. Só um blocker destruído no meio do jogo devolve a névoa
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		Progress = Target = 0.0f;
		NotifyVolumes(true, true);
	}
	Super::EndPlay(EndPlayReason);
}

void AJRPGMistBlocker::Destroyed()
{
	// Apagado no editor: a névoa volta no viewport
	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		NotifyVolumes(true, true);
	}
	Super::Destroyed();
}

void AJRPGMistBlocker::PollState()
{
	RefreshState(false);
}

void AJRPGMistBlocker::RefreshState(bool bInstant)
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	Target = ShouldBlock() ? 1.0f : 0.0f;
	if (FMath::IsNearlyEqual(Progress, Target))
	{
		return;
	}
	if (bInstant || TransitionTime <= 0.0f || World->GetTimeSeconds() < SnapUntilTime)
	{
		Progress = Target;
		SetActorTickEnabled(false);
		NotifyVolumes(false);
		return;
	}
	SetActorTickEnabled(true);
}

void AJRPGMistBlocker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Progress = FMath::FInterpConstantTo(Progress, Target, DeltaSeconds, 1.0f / FMath::Max(TransitionTime, 0.01f));
	if (FMath::IsNearlyEqual(Progress, Target))
	{
		Progress = Target;
		SetActorTickEnabled(false);
	}
	NotifyVolumes(false);
}

void AJRPGMistBlocker::NotifyVolumes(bool bShapeChanged, bool bRemoving)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AJRPGMistVolume> It(World); It; ++It)
	{
		It->HandleBlockerChanged(this, bShapeChanged, bRemoving);
	}
}
