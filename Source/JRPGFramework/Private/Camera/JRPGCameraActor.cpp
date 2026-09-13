#include "Camera/JRPGCameraActor.h"
#include "Camera/CameraComponent.h"

AJRPGCameraActor::AJRPGCameraActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	// Depois do movimento dos atores, antes da avaliação final da câmera —
	// o follow nunca fica um frame atrás do alvo
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	// ACameraActor trava aspect ratio por padrão (barras pretas) — não queremos
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->bConstrainAspectRatio = false;
	}
}

void AJRPGCameraActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	switch (Mode)
	{
	case EManagedCameraMode::Focus:
	case EManagedCameraMode::Orbit:
	{
		if (Mode == EManagedCameraMode::Orbit)
		{
			OrbitYawAccum += OrbitSpeedDegPerSec * DeltaSeconds;
		}

		FVector DesiredPos;
		FRotator DesiredRot;
		if (!ComputeFocusView(DesiredPos, DesiredRot))
		{
			// Alvo destruído no meio do foco — avisa o subsystem e fica inerte
			ClearMode();
			OnFollowTargetLost.Broadcast();
			return;
		}

		// Shot estático (bFollowTarget = false): só valida o alvo, não re-enquadra
		if (ActivePreset.bFollowTarget || Mode == EManagedCameraMode::Orbit)
		{
			ApplyView(DesiredPos, DesiredRot, DeltaSeconds, ActivePreset.FollowLagSpeed);
		}
		break;
	}

	case EManagedCameraMode::Group:
	{
		FVector DesiredPos;
		FRotator DesiredRot;
		if (!ComputeGroupView(DesiredPos, DesiredRot))
		{
			ClearMode();
			OnFollowTargetLost.Broadcast();
			return;
		}
		ApplyView(DesiredPos, DesiredRot, DeltaSeconds, 3.0f);
		break;
	}

	case EManagedCameraMode::Idle:
	default:
		break;
	}
}

void AJRPGCameraActor::SetupFocus(AActor* Target, const FCameraPresetRow& Preset)
{
	Mode = EManagedCameraMode::Focus;
	ActivePreset = Preset;
	FollowTarget = Target;
	GroupTargets.Empty();
	OrbitSpeedDegPerSec = 0.0f;
	OrbitYawAccum = 0.0f;

	ApplyLensSettings(Preset);

	// Snap para a pose final — a transição visual fica por conta do
	// SetViewTargetWithBlend do subsystem
	FVector DesiredPos;
	FRotator DesiredRot;
	if (ComputeFocusView(DesiredPos, DesiredRot))
	{
		SetActorLocationAndRotation(DesiredPos, DesiredRot);
	}
}

void AJRPGCameraActor::SetupGroup(const TArray<AActor*>& Targets, const FCameraPresetRow& BasePreset, float Padding)
{
	Mode = EManagedCameraMode::Group;
	ActivePreset = BasePreset;
	FollowTarget = nullptr;
	OrbitSpeedDegPerSec = 0.0f;
	OrbitYawAccum = 0.0f;
	GroupPadding = FMath::Max(Padding, 1.0f);

	GroupTargets.Empty();
	for (AActor* Actor : Targets)
	{
		if (IsValid(Actor))
		{
			GroupTargets.Add(Actor);
		}
	}

	ApplyLensSettings(BasePreset);

	FVector DesiredPos;
	FRotator DesiredRot;
	if (ComputeGroupView(DesiredPos, DesiredRot))
	{
		SetActorLocationAndRotation(DesiredPos, DesiredRot);
	}
}

void AJRPGCameraActor::StartOrbit(float DegPerSec)
{
	if (!FollowTarget.IsValid())
	{
		return;
	}
	Mode = EManagedCameraMode::Orbit;
	OrbitSpeedDegPerSec = DegPerSec;
}

void AJRPGCameraActor::StopOrbit()
{
	if (Mode == EManagedCameraMode::Orbit)
	{
		// Congela no ângulo atual mantendo o foco (o OrbitYawAccum fica somado)
		Mode = EManagedCameraMode::Focus;
		OrbitSpeedDegPerSec = 0.0f;
	}
}

void AJRPGCameraActor::ClearMode()
{
	Mode = EManagedCameraMode::Idle;
	FollowTarget = nullptr;
	GroupTargets.Empty();
	OrbitSpeedDegPerSec = 0.0f;
	OrbitYawAccum = 0.0f;
}

bool AJRPGCameraActor::ComputeFocusView(FVector& OutPos, FRotator& OutRot) const
{
	const AActor* Target = FollowTarget.Get();
	if (!Target)
	{
		return false;
	}

	const FTransform TargetTransform = Target->GetActorTransform();
	const FVector FocusPoint = TargetTransform.TransformPosition(ActivePreset.FocusOffset);

	const float CamYaw = TargetTransform.Rotator().Yaw + ActivePreset.YawDeg + OrbitYawAccum;
	const FRotator CamRot(-ActivePreset.PitchDeg, CamYaw, 0.0f);

	OutPos = FocusPoint - CamRot.Vector() * ActivePreset.Distance;

	// Mira no ponto de foco deslocado pelo LookAtOffset (composição)
	const FVector LookAtWorld = FocusPoint + TargetTransform.TransformVector(ActivePreset.LookAtOffset);
	OutRot = (LookAtWorld - OutPos).Rotation();
	return true;
}

bool AJRPGCameraActor::ComputeGroupView(FVector& OutPos, FRotator& OutRot)
{
	// Poda mortos e junta os bounds dos vivos numa caixa só
	FBox CombinedBounds(ForceInit);
	const AActor* FirstAlive = nullptr;

	for (int32 i = GroupTargets.Num() - 1; i >= 0; --i)
	{
		const AActor* Actor = GroupTargets[i].Get();
		if (!Actor)
		{
			GroupTargets.RemoveAt(i);
			continue;
		}
		if (!FirstAlive)
		{
			FirstAlive = Actor;
		}

		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(true, Origin, Extent);
		CombinedBounds += FBox(Origin - Extent, Origin + Extent);
	}

	if (!FirstAlive)
	{
		return false;
	}

	const FVector Center = CombinedBounds.GetCenter();
	const float Radius = FMath::Max(CombinedBounds.GetExtent().Size(), 50.0f);

	// Distância para a esfera do grupo caber no FOV com o padding pedido
	const float HalfFOVRad = FMath::DegreesToRadians(ActivePreset.FOV * 0.5f);
	const float FitDistance = (Radius * GroupPadding) / FMath::Max(FMath::Tan(HalfFOVRad), 0.01f);
	const float Distance = FMath::Max(FitDistance, ActivePreset.Distance * 0.5f);

	// Yaw relativo ao facing do primeiro ator vivo (âncora do enquadramento)
	const float CamYaw = FirstAlive->GetActorRotation().Yaw + ActivePreset.YawDeg;
	const FRotator CamRot(-ActivePreset.PitchDeg, CamYaw, 0.0f);

	OutPos = Center - CamRot.Vector() * Distance;
	OutRot = (Center - OutPos).Rotation();
	return true;
}

void AJRPGCameraActor::ApplyLensSettings(const FCameraPresetRow& Preset)
{
	UCameraComponent* Camera = GetCameraComponent();
	if (!Camera)
	{
		return;
	}

	Camera->SetFieldOfView(Preset.FOV);

	// DOF cinematográfico: foco na distância exata do personagem (Distance),
	// abertura baixa desfoca o fundo (estilo cine camera aperture 1.2)
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP.bOverride_DepthOfFieldFstop = Preset.bEnableDepthOfField;
	PP.bOverride_DepthOfFieldFocalDistance = Preset.bEnableDepthOfField;
	if (Preset.bEnableDepthOfField)
	{
		PP.DepthOfFieldFstop = Preset.Aperture;
		PP.DepthOfFieldFocalDistance = Preset.Distance;
	}
}

void AJRPGCameraActor::ApplyView(const FVector& Pos, const FRotator& Rot, float DeltaSeconds, float LagSpeed)
{
	if (LagSpeed > 0.0f)
	{
		const FVector NewPos = FMath::VInterpTo(GetActorLocation(), Pos, DeltaSeconds, LagSpeed);
		const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), Rot, DeltaSeconds, LagSpeed);
		SetActorLocationAndRotation(NewPos, NewRot);
	}
	else
	{
		SetActorLocationAndRotation(Pos, Rot);
	}
}
