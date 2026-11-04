#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ArtsData.generated.h"

class UAnimSequence;

/** O tipo da Art. Regular soma AP; Hyper / Super / Miracle gastam (a inversão do remake). */
UENUM(BlueprintType)
enum class EJRPGArtKind : uint8
{
	Regular  UMETA(DisplayName = "Regular"),
	Hyper    UMETA(DisplayName = "Hyper"),
	Super    UMETA(DisplayName = "Super"),
	Miracle  UMETA(DisplayName = "Miracle"),
	/** Peça de uma sequência (o finalizador de uma Super, a 2ª parte do Hurricane Kick): não se digita. */
	Part     UMETA(DisplayName = "Part")
};

/**
 * Os quatro botões de comando, no esquema do jogador. O byte de direção depende da mão do
 * herói (Vahn/Gala: Arms = esquerda; Noa, canhota: Arms = direita) e é resolvido na entrada.
 */
UENUM(BlueprintType)
enum class EJRPGArtButton : uint8
{
	Arms    UMETA(DisplayName = "Arms"),
	RaSeru  UMETA(DisplayName = "Ra-Seru"),
	High    UMETA(DisplayName = "High"),
	Low     UMETA(DisplayName = "Low")
};

/**
 * Como a Art é aprendida (BATTLE_ROADMAP.md › Phase 6 › How each kind of Art is unlocked). ByKind é
 * a regra do original para o Kind da linha: Regular no uso (OnUse), Hyper só ensinada (Taught, e com
 * o Ra-Seru), Super quando todas as Arts do Command dela já eram conhecidas antes do turno
 * (ChainKnown), Miracle pelo Ra-Seru (RaSeru). Os outros valores trocam a regra de uma linha só — uma
 * Super nova que um NPC ensina é Taught, uma Regular que só vem de livro também.
 */
UENUM(BlueprintType)
enum class EJRPGArtUnlock : uint8
{
	ByKind      UMETA(DisplayName = "By Kind"),
	OnUse       UMETA(DisplayName = "On Use"),
	Taught      UMETA(DisplayName = "Taught"),
	ChainKnown  UMETA(DisplayName = "Chain Known"),
	RaSeru      UMETA(DisplayName = "Ra-Seru")
};

/**
 * Que defesa um golpe enfrenta. As variantes "Misses" são o alcance alternativo do disco: o golpe
 * alto passa por cima de inimigo baixo, o baixo por baixo de inimigo voador.
 */
UENUM(BlueprintType)
enum class EJRPGArtHitTarget : uint8
{
	Upper                UMETA(DisplayName = "Upper (UDF)"),
	Lower                UMETA(DisplayName = "Lower (LDF)"),
	UpperMissesShort     UMETA(DisplayName = "Upper, misses short"),
	LowerMissesFloating  UMETA(DisplayName = "Lower, misses floating")
};

/**
 * FJRPGArtData
 * Uma Art (linha da DataTable DT_Arts, gerada de Docs/Data/gamedata/arts.toml + o banco de Arts
 * do disco via Extras/generate_arts_csv.py). O row name é a ArtKey ("vahn_hyper_elbow").
 *
 * Linha nova (feita à mão ou no LegaiaStudio) é uma Art nova: o gerador nunca a apaga. Uma Art
 * simples precisa de Command, Anim e os golpes; uma combinada lista em Sequence o que toca.
 *
 * Guia: Docs/Guides/ARTS.md.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtData : public FTableRowBase
{
	GENERATED_BODY()

	/** Igual ao row name ("vahn_hyper_elbow"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art")
	FName Key;

	/** Row name na DT_Characters ("Vahn"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art")
	FName Character;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art")
	EJRPGArtKind Kind = EJRPGArtKind::Regular;

	/** Os botões que disparam a Art, em ordem. Vazio numa Part. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art")
	TArray<EJRPGArtButton> Command;

	/** Custo AP do original (referência de balanceamento; o jogo usa APGain / APCost). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|AP")
	int32 OriginalAP = 0;

	/** Regular: quanto AP a Art SOMA. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|AP")
	int32 APGain = 0;

	/** Hyper / Super / Miracle: quanto AP a Art GASTA. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|AP")
	int32 APCost = 0;

	/** Quanto ela pesa no gauge de comandos do turno (semeado com o número de botões). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|AP")
	int32 CommandCost = 0;

	/** O clip da Art (AS_<Herói>_Battle_<Art>). Numa Super/Miracle, o da própria linha, se houver. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|Animation")
	TSoftObjectPtr<UAnimSequence> Anim;

	/** Multiplicador de cada golpe, em ordem (o disco usa 12, 18, 20, 22, 28). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|Hits")
	TArray<int32> HitPower;

	/** A defesa que cada golpe enfrenta, na mesma ordem de HitPower. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|Hits")
	TArray<EJRPGArtHitTarget> HitTarget;

	/**
	 * Art combinada: o que toca, em ordem. Cada passo é a key de outra Art do mesmo herói (a
	 * própria key = o Anim desta linha) ou um clip básico: SwingL, SwingR, SwingD, SwingU,
	 * ArtStarter, SpecialStarter. Vazio = só esta Art.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|Sequence")
	TArray<FName> Sequence;

	/** O registro da Art no disco (Action Constant 0x1B..0x32). 0 = Art nova, sem registro. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|Disc")
	int32 ActionConstant = 0;

	/** Como a Art é aprendida. ByKind = a regra do original para o Kind (ver EJRPGArtUnlock). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|Unlock")
	EJRPGArtUnlock Unlock = EJRPGArtUnlock::ByKind;

	/**
	 * A escala de velocidade do jogo quando esta Art é um passo de um turno (8 / 4 / 2, a do clip
	 * de golpe = 8). 0 = a regra do original (x4 numa Art, x2 depois do SpecialStarter).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art|Animation")
	int32 SpeedScale = 0;

#if WITH_EDITOR
	/** Data Validation: as regras do UJRPGArtsValidator para esta linha (Docs/Guides/ARTS.md). */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
