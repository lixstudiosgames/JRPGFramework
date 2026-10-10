#include "Field/JRPGFieldTypes.h"

FJRPGFieldCameraSettings FJRPGFieldCameraSettings::Blend(const FJRPGFieldCameraSettings& A, const FJRPGFieldCameraSettings& B, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	FJRPGFieldCameraSettings Out;
	Out.ArmLength = FMath::Lerp(A.ArmLength, B.ArmLength, T);
	Out.Pitch = FMath::Lerp(A.Pitch, B.Pitch, T);
	// Pelo lado mais curto: de 350 para 10 gira 20 graus, não 340
	Out.Yaw = A.Yaw + FRotator::NormalizeAxis(B.Yaw - A.Yaw) * T;
	Out.FOV = FMath::Lerp(A.FOV, B.FOV, T);
	Out.LagSpeed = FMath::Lerp(A.LagSpeed, B.LagSpeed, T);
	Out.FocusOffset = FMath::Lerp(A.FocusOffset, B.FocusOffset, T);
	return Out;
}
