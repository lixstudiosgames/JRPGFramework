// Testes do VFX: só a parte que é dado puro (as faixas e a duração da receita), sem mapa nem assets.
// Session Frontend › Automation, filtro "JRPG.VFX".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VFX/JRPGVFXRecipe.h"
#include "VFX/JRPGVFXTypes.h"
#include "UObject/Package.h"

#define JRPG_VFX_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGVFXTracksTest, "JRPG.VFX.Tracks", JRPG_VFX_TEST_FLAGS)
bool FJRPGVFXTracksTest::RunTest(const FString& Parameters)
{
	// Todo Ease começa em 0; o Pulse volta a 0 no fim, os outros chegam a 1
	for (const EJRPGVFXEase Ease : { EJRPGVFXEase::Linear, EJRPGVFXEase::EaseIn, EJRPGVFXEase::EaseOut, EJRPGVFXEase::EaseInOut, EJRPGVFXEase::Pulse })
	{
		TestEqual(TEXT("Ease em 0"), JRPGVFX::EvaluateEase(Ease, 0.0f), 0.0f, 1e-4f);
		TestEqual(TEXT("Ease em 1"), JRPGVFX::EvaluateEase(Ease, 1.0f), Ease == EJRPGVFXEase::Pulse ? 0.0f : 1.0f, 1e-4f);
	}
	TestEqual(TEXT("Pulse no meio"), JRPGVFX::EvaluateEase(EJRPGVFXEase::Pulse, 0.5f), 1.0f, 1e-4f);
	TestEqual(TEXT("Fora de 0..1 é preso"), JRPGVFX::EvaluateEase(EJRPGVFXEase::Linear, 3.0f), 1.0f, 1e-4f);

	FJRPGVFXScalarTrack Scalar;
	Scalar.From = 2.0f;
	Scalar.To = 6.0f;
	TestEqual(TEXT("Escalar linear no meio"), Scalar.Evaluate(0.5f), 4.0f, 1e-4f);
	Scalar.Ease = EJRPGVFXEase::Pulse;
	TestEqual(TEXT("Escalar em Pulse volta ao From"), Scalar.Evaluate(1.0f), 2.0f, 1e-3f);

	FJRPGVFXColorTrack Color;
	Color.From = FLinearColor::Black;
	Color.To = FLinearColor::White;
	Color.bTintWithContextColor = true;
	const FLinearColor Tinted = Color.Evaluate(1.0f, FLinearColor(1.0f, 0.5f, 0.0f, 1.0f));
	TestEqual(TEXT("Cor com tint do contexto (G)"), Tinted.G, 0.5f, 1e-4f);
	TestEqual(TEXT("Cor com tint do contexto (B)"), Tinted.B, 0.0f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGVFXRecipeLengthTest, "JRPG.VFX.RecipeLength", JRPG_VFX_TEST_FLAGS)
bool FJRPGVFXRecipeLengthTest::RunTest(const FString& Parameters)
{
	UJRPGVFXRecipe* Recipe = NewObject<UJRPGVFXRecipe>(GetTransientPackage());
	TestTrue(TEXT("Receita vazia tem duração mínima, nunca 0"), Recipe->GetLength() > 0.0f);

	FJRPGVFXLayer Burst;
	Burst.StartTime = 0.2f;
	Burst.Duration = 0.5f;
	FJRPGVFXLayer Open;   // sem duração própria: não estica o efeito além do começo dela
	Open.StartTime = 1.0f;
	Recipe->Layers = { Burst, Open };
	TestEqual(TEXT("Duração = o fim da última camada"), Recipe->GetLength(), 1.0f, 1e-4f);

	Recipe->Layers[0].Duration = 2.0f;
	TestEqual(TEXT("A camada mais longa manda"), Recipe->GetLength(), 2.2f, 1e-4f);

	Recipe->Duration = 3.0f;
	TestEqual(TEXT("Duration explícito vence"), Recipe->GetLength(), 3.0f, 1e-4f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
