#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Arts/ArtsData.h"
#include "JRPGArtSequence.generated.h"

class UAnimSequence;

/** O que um passo é, para a escala de velocidade do turno. */
UENUM(BlueprintType)
enum class EJRPGArtStepKind : uint8
{
	Swing           UMETA(DisplayName = "Swing"),
	ArtStarter      UMETA(DisplayName = "Art Starter"),
	SpecialStarter  UMETA(DisplayName = "Special Starter"),
	Art             UMETA(DisplayName = "Art")
};

/** Um clip da fila de um turno: o nome mostrado, o clip, a escala do jogo nele e o corte. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtStep
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FString Label;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TObjectPtr<UAnimSequence> Clip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	EJRPGArtStepKind Kind = EJRPGArtStepKind::Swing;

	/** A Art (row do DT_Arts) a que o passo pertence; None num golpe solto. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName ArtKey;

	/** A escala de velocidade do jogo neste passo: 8 golpe/starter, 4 numa Art, 2 depois do SpecialStarter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 GameSpeed = 8;

	/** Onde o clip é cortado se houver passo depois (o Chain Cut dele), em segundos; -1 = toca inteiro. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	float CutTime = -1.0f;
};

/**
 * UJRPGArtSequenceLibrary
 * Monta a fila de clips de um turno como o jogo original encena, a partir do DT_Arts — a mesma
 * regra para a luta e para o AJRPGArtTester:
 *
 *  - Uma Art Regular: os botões ANTES do último saem como golpes simples; o último vira o
 *    ArtStarter e a Art. Iron Head (High, Low, Low) = SwingU, SwingD, ArtStarter, Ironhead.
 *  - Hyper: sem golpes, só o SpecialStarter e a Art (a Sequence dela).
 *  - Super e Miracle: a Sequence delas é o turno inteiro (as Arts, os conectores, o finalizador).
 *  - Velocidade (FUN_8004AD80, ctx +0x243): golpes e ArtStarter na escala 8; cada Art 4; o
 *    SpecialStarter liga o modo especial e as Arts depois dele vão a 2.
 *  - Chain Cut: com outro passo depois, o clip termina no UJRPGAnimNotify_ChainCut dele.
 *
 * Os clips básicos do herói (SwingL..U, ArtStarter, SpecialStarter) são AS_<Herói>_Battle_<Nome>,
 * na pasta da BattleMesh dele no DT_Characters. Guia: Docs/Guides/ARTS.md.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGArtSequenceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Põe no fim de Steps os passos de uma Art. bLeadingSwings: os golpes dos botões antes do último
	 * (o jogo faz; desligado = só starter + Art). Falso se a Art não está no DT_Arts.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static bool AppendArtSteps(FName ArtKey, bool bLeftHanded, bool bLeadingSwings, UPARAM(ref) TArray<FJRPGArtStep>& Steps);

	/** Põe no fim de Steps o golpe simples de um botão (Arms -> SwingL, ou SwingR no canhoto). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static void AppendButtonSwing(FName Hero, EJRPGArtButton Button, bool bLeftHanded, UPARAM(ref) TArray<FJRPGArtStep>& Steps);

	/** Escreve a escala do jogo de cada passo, na ordem do turno. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static void ApplyGameSpeeds(UPARAM(ref) TArray<FJRPGArtStep>& Steps);

	/** O play rate que dá a escala do passo por cima do RateScale do clip (RateScale 2 = escala 8). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static float StepPlayRate(const FJRPGArtStep& Step);

	/** O nome do clip do golpe de um botão: SwingL / SwingR / SwingU / SwingD. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static FString SwingClipName(EJRPGArtButton Button, bool bLeftHanded);

	/** AS_<Herói>_Battle_<ClipName>, ao lado da BattleMesh do herói. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static UAnimSequence* LoadHeroClip(FName Hero, const FString& ClipName);

	/** As Arts do herói, na ordem da tabela (com ou sem as Parts). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static TArray<FName> HeroArts(FName Hero, bool bIncludeParts);

	/** A linha do DT_Arts (nullptr se não existe). */
	static const FJRPGArtData* FindArt(FName ArtKey);

private:
	static void AddStep(TArray<FJRPGArtStep>& Steps, const FString& Label, UAnimSequence* Clip, EJRPGArtStepKind Kind, FName ArtKey);
	static void AddSequenceStep(TArray<FJRPGArtStep>& Steps, FName Hero, FName Step, FName SelfKey, const FJRPGArtData& Self, int32 Depth);
};
