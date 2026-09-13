#include "Camera/JRPGCameraShakeModifier.h"
#include "Camera/CameraSubsystem.h"
#include "Camera/CameraTypes.h"

void UJRPGCameraShakeModifier::AddShake(const FJRPGCameraShakeParams& Params, float Scale, ECameraShakePreset SourcePreset)
{
	FActiveShake Shake;
	Shake.Params = Params;
	Shake.Scale = FMath::Max(Scale, 0.0f);
	Shake.SourcePreset = SourcePreset;
	for (int32 i = 0; i < 7; ++i)
	{
		Shake.Seeds[i] = FMath::FRandRange(0.0f, 1000.0f);
	}
	ActiveShakes.Add(Shake);
}

void UJRPGCameraShakeModifier::StopAll(bool bImmediate)
{
	if (bImmediate)
	{
		TArray<ECameraShakePreset> Finished;
		Finished.Reserve(ActiveShakes.Num());
		for (const FActiveShake& Shake : ActiveShakes)
		{
			Finished.Add(Shake.SourcePreset);
		}
		ActiveShakes.Empty();

		for (ECameraShakePreset Preset : Finished)
		{
			if (OwnerSubsystem.IsValid())
			{
				OwnerSubsystem->NotifyShakeFinished(Preset);
			}
		}
		return;
	}

	// Suave: cada shake faz fade pelo próprio BlendOutTime a partir de agora
	for (FActiveShake& Shake : ActiveShakes)
	{
		if (!Shake.bStopping)
		{
			Shake.bStopping = true;
			Shake.StopStartTime = Shake.Elapsed;
		}
	}
}

bool UJRPGCameraShakeModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	if (ActiveShakes.Num() == 0)
	{
		return false;
	}

	// Terminados são notificados DEPOIS do loop — o callback BP pode disparar
	// novos shakes (AddShake) sem invalidar a iteração
	TArray<ECameraShakePreset> Finished;

	for (int32 i = ActiveShakes.Num() - 1; i >= 0; --i)
	{
		FActiveShake& Shake = ActiveShakes[i];
		Shake.Elapsed += DeltaTime;

		float Envelope = Shake.Scale;

		// Rampa de entrada
		if (Shake.Params.BlendInTime > 0.0f)
		{
			Envelope *= FMath::Clamp(Shake.Elapsed / Shake.Params.BlendInTime, 0.0f, 1.0f);
		}

		// Rampa de saída / fim de vida
		bool bFinished = false;
		if (Shake.bStopping)
		{
			// Fade-out de parada (StopAll suave)
			if (Shake.Params.BlendOutTime > 0.0f)
			{
				const float OutAlpha = 1.0f - (Shake.Elapsed - Shake.StopStartTime) / Shake.Params.BlendOutTime;
				bFinished = OutAlpha <= 0.0f;
				Envelope *= FMath::Max(OutAlpha, 0.0f);
			}
			else
			{
				bFinished = true;
			}
		}
		else if (Shake.Params.Duration > 0.0f)
		{
			// Vida finita: fade-out automático no fim da duração
			if (Shake.Elapsed >= Shake.Params.Duration)
			{
				bFinished = true;
			}
			else if (Shake.Params.BlendOutTime > 0.0f)
			{
				const float Remaining = Shake.Params.Duration - Shake.Elapsed;
				if (Remaining < Shake.Params.BlendOutTime)
				{
					Envelope *= Remaining / Shake.Params.BlendOutTime;
				}
			}
		}
		// Duration == 0 e sem bStopping = loop infinito

		if (bFinished)
		{
			Finished.Add(Shake.SourcePreset);
			ActiveShakes.RemoveAt(i);
			continue;
		}

		if (Envelope <= 0.0f)
		{
			continue;
		}

		// Perlin por eixo com seed próprio: suave e sem sensação de metrônomo
		const float T = Shake.Elapsed;
		const FJRPGCameraShakeParams& P = Shake.Params;

		const FVector LocOffset(
			P.LocationAmplitude.X * FMath::PerlinNoise1D(Shake.Seeds[0] + T * P.LocationFrequency),
			P.LocationAmplitude.Y * FMath::PerlinNoise1D(Shake.Seeds[1] + T * P.LocationFrequency),
			P.LocationAmplitude.Z * FMath::PerlinNoise1D(Shake.Seeds[2] + T * P.LocationFrequency));

		// Offset em ESPAÇO DA CÂMERA — o tranco acompanha para onde ela olha
		InOutPOV.Location += InOutPOV.Rotation.RotateVector(LocOffset * Envelope);

		InOutPOV.Rotation.Pitch += P.RotationAmplitude.Pitch * FMath::PerlinNoise1D(Shake.Seeds[3] + T * P.RotationFrequency) * Envelope;
		InOutPOV.Rotation.Yaw   += P.RotationAmplitude.Yaw   * FMath::PerlinNoise1D(Shake.Seeds[4] + T * P.RotationFrequency) * Envelope;
		InOutPOV.Rotation.Roll  += P.RotationAmplitude.Roll  * FMath::PerlinNoise1D(Shake.Seeds[5] + T * P.RotationFrequency) * Envelope;

		if (P.FOVAmplitude > 0.0f)
		{
			InOutPOV.FOV += P.FOVAmplitude * FMath::PerlinNoise1D(Shake.Seeds[6] + T * P.FOVFrequency) * Envelope;
		}
	}

	for (ECameraShakePreset Preset : Finished)
	{
		if (OwnerSubsystem.IsValid())
		{
			OwnerSubsystem->NotifyShakeFinished(Preset);
		}
	}

	// false = não desabilita os demais modifiers da câmera
	return false;
}
