#include "VFX/JRPGVFXRecipe.h"
#include "Curves/CurveFloat.h"

float JRPGVFX::EvaluateEase(EJRPGVFXEase Ease, float Alpha)
{
	const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	switch (Ease)
	{
	case EJRPGVFXEase::EaseIn:    return A * A;
	case EJRPGVFXEase::EaseOut:   return 1.0f - (1.0f - A) * (1.0f - A);
	case EJRPGVFXEase::EaseInOut: return A * A * (3.0f - 2.0f * A);
	case EJRPGVFXEase::Pulse:     return FMath::Sin(A * UE_PI);
	default:                      return A;
	}
}

float FJRPGVFXScalarTrack::Evaluate(float Alpha) const
{
	const float Blend = Curve ? Curve->GetFloatValue(FMath::Clamp(Alpha, 0.0f, 1.0f)) : JRPGVFX::EvaluateEase(Ease, Alpha);
	return FMath::Lerp(From, To, Blend);
}

FLinearColor FJRPGVFXColorTrack::Evaluate(float Alpha, const FLinearColor& ContextColor) const
{
	const FLinearColor Value = FMath::Lerp(From, To, JRPGVFX::EvaluateEase(Ease, Alpha));
	return bTintWithContextColor ? Value * ContextColor : Value;
}

float UJRPGVFXRecipe::GetLength() const
{
	float Length = Duration;
	if (Length <= 0.0f)
	{
		for (const FJRPGVFXLayer& Layer : Layers)
		{
			// Uma camada sem duração própria não estica o efeito: só o começo dela conta
			Length = FMath::Max(Length, Layer.StartTime + FMath::Max(Layer.Duration, 0.0f));
		}
	}
	return FMath::Max(Length, 0.01f);
}
