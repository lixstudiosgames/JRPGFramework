#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Arts/ArtsData.h"
#include "Arts/JRPGArtSequence.h"
#include "JRPGArtsRules.generated.h"

class UDataTable;

/**
 * As quatro direções do controle, como o jogador aperta. Qual botão de Art cada uma é depende da
 * mão do herói (UJRPGArtsRules::ButtonForPad): Vahn e Gala, destros, têm Arms na esquerda; a Noa,
 * canhota, na direita. Cima é sempre High e baixo sempre Low.
 */
UENUM(BlueprintType)
enum class EJRPGPadButton : uint8
{
	Left   UMETA(DisplayName = "Left"),
	Right  UMETA(DisplayName = "Right"),
	Up     UMETA(DisplayName = "Up"),
	Down   UMETA(DisplayName = "Down")
};

/** O que é cada linha da cadeia que um turno toca. */
UENUM(BlueprintType)
enum class EJRPGArtChainStepType : uint8
{
	/** Um botão que não terminou Art nenhuma: o golpe simples dele. */
	Swing    UMETA(DisplayName = "Swing"),
	/** Uma Regular ou uma Hyper reconhecida e solta (fora de uma Super/Miracle que disparou). */
	Art      UMETA(DisplayName = "Art"),
	/** Uma Super ou Miracle que disparou: toca a Sequence dela. */
	Special  UMETA(DisplayName = "Special")
};

/** Por que um especial (Hyper / Super / Miracle) não saiu e virou os botões dele. */
UENUM(BlueprintType)
enum class EJRPGArtDowngradeReason : uint8
{
	/** O AP naquele ponto da cadeia não paga o APCost. */
	NotEnoughAP  UMETA(DisplayName = "Not enough AP"),
	/** Ainda não destravada: Super com uma Art da cadeia desconhecida antes do turno, não ensinada, sem Ra-Seru. */
	Locked       UMETA(DisplayName = "Locked")
};

/**
 * FJRPGArtsContext
 * O que as regras precisam saber do herói, tirado ANTES do turno — uma foto, nunca um subsistema
 * (a luta tira a dela do combatente; fora dela, o UArtsSubsystem tira do roster).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtsContext
{
	GENERATED_BODY()

	/** Row name na DT_Characters. Só as Arts dele são reconhecidas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arts")
	FName Character;

	/** As Arts que ele já sabia quando o turno começou (keys do DT_Arts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arts")
	TArray<FName> KnownArts;

	/** Tem um Ra-Seru: o original só deixa Hyper e Miracle sair com ele (o marcador ctx +0x25F). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arts")
	bool bHasRaSeru = true;

	/** O AP no começo do turno. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arts")
	int32 AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arts")
	int32 MaxAP = 100;

	/**
	 * Chance, em %, de uma Regular desconhecida ser aprendida quando o jogador a faz. 100 = sempre
	 * (o portão do original lê aberto). Abaixo de 100 sorteia uma vez por Art, do Rng passado.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arts")
	int32 LearnOnUseChance = 100;
};

/** Uma Art que os botões fazem: onde ela começa e termina na entrada, e se é nova. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGRecognizedArt
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName ArtKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	EJRPGArtKind Kind = EJRPGArtKind::Regular;

	/** O primeiro e o último botão dela na entrada (as Arts se sobrepõem: um botão pode ser de duas). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 FirstButton = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 LastButton = 0;

	/** Não era conhecida no começo do turno e foi aprendida neste uso (o aviso sai uma vez, na primeira). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	bool bNewlyLearned = false;

	/** A Super / Miracle que disparou por cima dela (None = solta). Dentro de uma, não mexe no AP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName InsideSpecial;
};

/** Uma linha da cadeia, na ordem em que o turno toca, com o AP correndo. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtChainEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	EJRPGArtChainStepType Type = EJRPGArtChainStepType::Swing;

	/** O botão do golpe (só no Swing). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	EJRPGArtButton Button = EJRPGArtButton::Arms;

	/** A Art ou o especial (None no Swing). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName ArtKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 FirstButton = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 LastButton = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	bool bNewlyLearned = false;

	/** O AP antes e depois desta linha (a Regular soma APGain, o especial gasta APCost). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APBefore = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APAfter = 0;

	/** Num especial: as Arts que os botões dele fazem (que não somam AP enquanto ele dispara). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<FName> InsideArts;
};

/** Um especial que os botões fariam mas que não saiu. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtDowngrade
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName ArtKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	EJRPGArtDowngradeReason Reason = EJRPGArtDowngradeReason::NotEnoughAP;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 FirstButton = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 LastButton = 0;

	/** O AP que havia naquele ponto da cadeia, e o que ele custa. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APAtPoint = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APCost = 0;
};

/**
 * FJRPGArtChain
 * O turno inteiro decidido no commit: o que os botões reconhecem, o que toca (em ordem), o AP
 * correndo e o que caiu. Nada foi debitado nem creditado ainda — o AP anda quando a ação chega em
 * cada Art (Fase 5); APBefore / APAfter de cada linha são o plano.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtChain
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName Character;

	/** Os botões, como entraram. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<EJRPGArtButton> Buttons;

	/** Toda Art que os botões fazem, pela ordem em que termina na entrada. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<FJRPGRecognizedArt> Recognized;

	/** O que o turno toca, em ordem. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<FJRPGArtChainEntry> Entries;

	/** Especiais que não saíram (AP ou trava). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<FJRPGArtDowngrade> Downgrades;

	/** As Arts aprendidas neste turno, uma vez cada. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<FName> NewlyLearned;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APStart = 0;

	/** O AP no fim do turno, se a ação chegar até o fim. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APEnd = 0;
};

/** Se um especial do herói sai se for o próximo a ser digitado (o menu acinzenta os que não). */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtAvailability
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName ArtKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	EJRPGArtKind Kind = EJRPGArtKind::Hyper;

	/** Destravada (ensinada, a cadeia conhecida, o Ra-Seru — o Unlock dela). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	bool bUnlocked = false;

	/** O AP no fim do que já foi digitado paga o APCost. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	bool bAffordable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APCost = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 APAvailable = 0;
};

/**
 * FJRPGArtsInputWindow
 * A janela de entrada das Arts: LOCAL, sem ida às regras a cada botão. Cada botão gasta o custo
 * dele do gauge do turno (o original: pool = AGL, cada comando custa o byte +0x74 da arma — 30 o
 * favorito, 42 / 54 fora da classe; RE: docs/subsystems/arts-command-gauge.md). O commit manda a
 * cadeia inteira de uma vez (EncodeArtBytes), e as regras reconhecem de novo a partir dos bytes.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtsInputWindow
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName Character;

	/** O gauge do turno (semeado do AGL). 0 ou menos = sem limite (só o MaxChainButtons). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 Gauge = 0;

	/** O que sobra dele. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	int32 GaugeLeft = 0;

	/** O custo de cada botão, indexado por EJRPGArtButton (Arms, RaSeru, High, Low). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<int32> ButtonCost;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	TArray<EJRPGArtButton> Buttons;
};

/**
 * UJRPGArtsRules
 * As regras das Arts como funções puras: botões in -> o turno decidido out. Leem só o DT_Arts e a
 * foto do herói (FJRPGArtsContext); nada de subsistema, nada de float, nada de RNG global — o sorteio
 * do aprender-no-uso (quando a chance não é 100) usa o FRandomStream passado. A luta (Fase 5) chama
 * BuildChain com o Rng dela; fora da luta, o UArtsSubsystem e o AJRPGArtTester.
 *
 * Reconhecimento = o tokenizer do original (FUN_801EED1C; RE crates/art/src/tokenize.rs): a entrada
 * é percorrida do fim para o começo, cada Art casada troca o ÚLTIMO botão dela pelo starter e os
 * anteriores ficam (viram golpes), e a busca recomeça logo depois — então um botão pode ser de duas
 * Arts (Tri-Somersault = Somersault, Cyclone, Somersault em 7 botões). A Hyper cobre a faixa inteira
 * (o original escreve o 0x1A no começo dela). Super e Miracle casam pelo Command da linha e têm
 * precedência sobre as Arts dentro da faixa. Tudo vem do DT_Arts: uma Art nova é uma linha.
 *
 * Guia: Docs/Guides/ARTS.md › 4. Recognition. Roadmap: BATTLE_ROADMAP.md › Phase 6.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGArtsRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** O máximo de botões numa cadeia (o buffer de 16 bytes do original). */
	static constexpr int32 MaxChainButtons = 16;

	/** O custo de um botão no gauge quando ninguém diz outro (o 0x1E do original). */
	static constexpr int32 DefaultButtonCost = 30;

	/** O botão de Art de uma direção do controle. Canhoto: Arms é a direita, Ra-Seru a esquerda. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static EJRPGArtButton ButtonForPad(bool bLeftHanded, EJRPGPadButton Pad);

	/** A regra de aprender da linha: o Unlock dela, ou a do Kind quando é ByKind. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static EJRPGArtUnlock EffectiveUnlock(const FJRPGArtData& Art);

	/**
	 * O turno inteiro a partir dos botões: reconhecimento, quais Arts são novas, Super / Miracle, o
	 * AP correndo da esquerda para a direita e o que cai por falta dele. Ver FJRPGArtChain.
	 */
	static FJRPGArtChain BuildChain(const UDataTable* Arts, const FJRPGArtsContext& Context,
		const TArray<EJRPGArtButton>& Buttons, FRandomStream& Rng);

	/**
	 * O tokenizer puro, com todas as Arts Regular / Hyper do herói (conhecidas ou não, sem AP): o que
	 * o validador e as Supers usam. Em ordem de onde cada Art termina.
	 */
	static TArray<FJRPGRecognizedArt> TokenizeAll(const UDataTable* Arts, FName Character, const TArray<EJRPGArtButton>& Buttons);

	/** As Arts de uma Super / Miracle: o que o Command dela tokeniza (NÃO a Sequence, que tira a última pelo finalizador). */
	static TArray<FName> GetSpecialOwnArts(const UDataTable* Arts, FName SpecialKey);

	/** Para cada Hyper / Super / Miracle do herói: sai se for digitada depois de TypedSoFar? */
	static TArray<FJRPGArtAvailability> GetSpecialsAvailability(const UDataTable* Arts, const FJRPGArtsContext& Context,
		const TArray<EJRPGArtButton>& TypedSoFar);

	/** Põe no fim de Steps os clips do turno (golpes, starters, Arts, Sequences), na ordem da cadeia. */
	static void AppendChainSteps(const FJRPGArtChain& Chain, bool bLeftHanded, TArray<FJRPGArtStep>& Steps);

	/** Os botões como bytes (o ArtBytes do FJRPGBattleCommand): um por botão, o valor do EJRPGArtButton. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static TArray<uint8> EncodeArtBytes(const TArray<EJRPGArtButton>& Buttons);

	/** Os bytes de volta. Falso (e nada em Out) com um byte que não é botão ou mais que MaxChainButtons. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static bool DecodeArtBytes(const TArray<uint8>& Bytes, TArray<EJRPGArtButton>& OutButtons);

	/** Abre a janela de entrada com o gauge do turno (0 = sem limite) e o mesmo custo em todo botão. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static FJRPGArtsInputWindow OpenInputWindow(FName Character, int32 Gauge, int32 ButtonCost = 30);

	/** Um botão na janela. Falso se o gauge não paga o custo dele ou a cadeia já está cheia. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static bool PushInputButton(UPARAM(ref) FJRPGArtsInputWindow& Window, EJRPGArtButton Button);

	/** Desfaz o último botão (devolve o custo). Falso se a janela está vazia. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static bool PopInputButton(UPARAM(ref) FJRPGArtsInputWindow& Window);

	/** Ainda cabe algum botão (o original fecha a janela sozinho quando nenhum cabe). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static bool CanPushAnyButton(const FJRPGArtsInputWindow& Window);

	/**
	 * Lê uma entrada escrita: "Arms, Ra-Seru, High", "A R H L", ou setas do controle ("← → ↑ ↓",
	 * "Left Right Up Down"), estas pela mão do herói. Falso se uma palavra não é botão.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static bool ParseButtons(const FString& Text, bool bLeftHanded, TArray<EJRPGArtButton>& OutButtons);

	/** O nome curto de um botão ("Arms", "Ra-Seru", "High", "Low"). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static FString ButtonName(EJRPGArtButton Button);

	/** A cadeia em texto, uma linha por passo, com o AP (o que o Art Tester mostra). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static FString DescribeChain(const FJRPGArtChain& Chain);
};
