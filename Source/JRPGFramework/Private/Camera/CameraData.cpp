#include "Camera/CameraData.h"

namespace JRPGCameraPresets
{

FCameraPresetRow GetBuiltIn(ECameraFramingPreset Preset)
{
	FCameraPresetRow Row; // parte dos defaults do struct (== Default)

	switch (Preset)
	{
	case ECameraFramingPreset::Default:
		// Distance 600 | Pitch 15 | Yaw 180 | FocusZ 120 | FOV 60 | follow lag 5
		break;

	case ECameraFramingPreset::CloseUp:
		Row.Distance = 180.0f;
		Row.PitchDeg = 5.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 155.0f);
		Row.FOV = 45.0f;
		Row.FollowLagSpeed = 8.0f;
		break;

	case ECameraFramingPreset::Conversation:
		Row.Distance = 320.0f;
		Row.PitchDeg = 8.0f;
		Row.YawDeg = 160.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 140.0f);
		Row.LookAtOffset = FVector(0.0f, 25.0f, 0.0f);
		Row.FOV = 50.0f;
		Row.FollowLagSpeed = 6.0f;
		break;

	case ECameraFramingPreset::OverShoulder:
		Row.Distance = 240.0f;
		Row.PitchDeg = 6.0f;
		Row.YawDeg = 145.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 145.0f);
		Row.LookAtOffset = FVector(0.0f, 35.0f, 0.0f);
		Row.FOV = 45.0f;
		Row.FollowLagSpeed = 8.0f;
		break;

	case ECameraFramingPreset::ShopFront:
		Row.Distance = 350.0f;
		Row.PitchDeg = 10.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 130.0f);
		Row.FOV = 55.0f;
		Row.FollowLagSpeed = 6.0f;
		break;

	case ECameraFramingPreset::ThreeQuarter:
		Row.Distance = 420.0f;
		Row.PitchDeg = 12.0f;
		Row.YawDeg = 135.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 130.0f);
		Row.FOV = 55.0f;
		break;

	case ECameraFramingPreset::TopDown:
		Row.Distance = 900.0f;
		Row.PitchDeg = 70.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 100.0f);
		Row.FollowLagSpeed = 4.0f;
		break;

	case ECameraFramingPreset::Wide:
		Row.Distance = 1200.0f;
		Row.PitchDeg = 18.0f;
		Row.FOV = 70.0f;
		Row.bFollowTarget = false;
		Row.FollowLagSpeed = 0.0f;
		break;

	case ECameraFramingPreset::BattleWide:
		Row.Distance = 1000.0f;
		Row.PitchDeg = 22.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 110.0f);
		Row.FOV = 65.0f;
		Row.bFollowTarget = false;
		Row.FollowLagSpeed = 0.0f;
		break;

	// Convenção de lado (validada no jogo): yaw < 180 = esquerdo do NPC,
	// yaw negativo (> 180) = direito
	case ECameraFramingPreset::Side45Left:
		Row.Distance = 350.0f;
		Row.PitchDeg = 10.0f;
		Row.YawDeg = 135.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 130.0f);
		Row.FOV = 55.0f;
		Row.FollowLagSpeed = 6.0f;
		break;

	case ECameraFramingPreset::Side45Right:
		Row.Distance = 350.0f;
		Row.PitchDeg = 10.0f;
		Row.YawDeg = -135.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 130.0f);
		Row.FOV = 55.0f;
		Row.FollowLagSpeed = 6.0f;
		break;

	// Portrait: shot fechado teleobjetiva com DOF — réplica da config aprovada
	// no jogo (spring arm 165°/-165°, arm 140, cine FOV 22, aperture 1.2)
	case ECameraFramingPreset::PortraitLeft:
		Row.Distance = 140.0f;
		Row.PitchDeg = 2.0f;
		Row.YawDeg = 165.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 140.0f);
		Row.FOV = 22.0f;
		Row.FollowLagSpeed = 6.0f;
		Row.bEnableDepthOfField = true;
		Row.Aperture = 1.2f;
		break;

	case ECameraFramingPreset::PortraitRight:
		Row.Distance = 140.0f;
		Row.PitchDeg = 2.0f;
		Row.YawDeg = -165.0f;
		Row.FocusOffset = FVector(0.0f, 0.0f, 140.0f);
		Row.FOV = 22.0f;
		Row.FollowLagSpeed = 6.0f;
		Row.bEnableDepthOfField = true;
		Row.Aperture = 1.2f;
		break;

	default:
		break;
	}

	return Row;
}

FJRPGCameraShakeParams GetShakePreset(ECameraShakePreset Preset)
{
	FJRPGCameraShakeParams P; // defaults do struct (== Medium)

	switch (Preset)
	{
	case ECameraShakePreset::Light:
		P.Duration = 0.3f;
		P.LocationAmplitude = FVector(1.0f, 1.0f, 1.0f);
		P.LocationFrequency = 18.0f;
		P.RotationAmplitude = FRotator(0.2f, 0.2f, 0.1f);
		P.RotationFrequency = 15.0f;
		P.BlendOutTime = 0.15f;
		break;

	case ECameraShakePreset::Medium:
		// Duration 0.5 | Loc (3,3,2)@14Hz | Rot (0.6,0.6,0.3)@12Hz | in/out 0.05/0.2
		break;

	case ECameraShakePreset::Heavy:
		P.Duration = 0.8f;
		P.LocationAmplitude = FVector(7.0f, 7.0f, 5.0f);
		P.LocationFrequency = 11.0f;
		P.RotationAmplitude = FRotator(1.5f, 1.5f, 0.8f);
		P.RotationFrequency = 10.0f;
		P.BlendOutTime = 0.3f;
		break;

	case ECameraShakePreset::Explosion:
		P.Duration = 1.0f;
		P.LocationAmplitude = FVector(12.0f, 12.0f, 8.0f);
		P.LocationFrequency = 9.0f;
		P.RotationAmplitude = FRotator(3.0f, 3.0f, 1.5f);
		P.RotationFrequency = 8.0f;
		P.BlendInTime = 0.02f;
		P.BlendOutTime = 0.5f;
		P.FOVAmplitude = 2.0f;
		P.FOVFrequency = 9.0f;
		break;

	case ECameraShakePreset::Earthquake:
		P.Duration = 4.0f;
		P.LocationAmplitude = FVector(6.0f, 6.0f, 10.0f);
		P.LocationFrequency = 5.0f;
		P.RotationAmplitude = FRotator(1.0f, 1.0f, 2.0f);
		P.RotationFrequency = 4.0f;
		P.BlendInTime = 0.5f;
		P.BlendOutTime = 1.0f;
		break;

	case ECameraShakePreset::HitImpact:
		P.Duration = 0.2f;
		P.LocationAmplitude = FVector(4.0f, 2.0f, 1.0f);
		P.LocationFrequency = 25.0f;
		P.RotationAmplitude = FRotator(1.2f, 0.6f, 0.4f);
		P.RotationFrequency = 22.0f;
		P.BlendInTime = 0.0f;
		P.BlendOutTime = 0.15f;
		break;

	case ECameraShakePreset::Rumble:
		P.Duration = 0.0f; // loop até StopAllCameraShakes
		P.LocationAmplitude = FVector(2.0f, 2.0f, 3.0f);
		P.LocationFrequency = 7.0f;
		P.RotationAmplitude = FRotator(0.4f, 0.4f, 0.6f);
		P.RotationFrequency = 6.0f;
		P.BlendInTime = 0.3f;
		P.BlendOutTime = 0.5f;
		break;

	case ECameraShakePreset::Handheld:
		// Deriva lenta estilo câmera na mão (documentário) — combina com foco
		// em NPC/diálogo para dar vida à câmera parada
		P.Duration = 0.0f; // loop até StopAllCameraShakes
		P.LocationAmplitude = FVector(1.5f, 1.5f, 1.0f);
		P.LocationFrequency = 1.8f;
		P.RotationAmplitude = FRotator(0.35f, 0.35f, 0.15f);
		P.RotationFrequency = 1.4f;
		P.BlendInTime = 0.6f;
		P.BlendOutTime = 0.6f;
		break;

	default:
		break;
	}

	return P;
}

FName FramingPresetToName(ECameraFramingPreset Preset)
{
	const UEnum* Enum = StaticEnum<ECameraFramingPreset>();
	return Enum ? FName(*Enum->GetNameStringByValue(static_cast<int64>(Preset))) : NAME_None;
}

} // namespace JRPGCameraPresets
