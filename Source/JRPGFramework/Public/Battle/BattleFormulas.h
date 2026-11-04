#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Arts/ArtsData.h"
#include "Party/CharacterData.h"
#include "Battle/BattleTypes.h"
#include "BattleFormulas.generated.h"

/**
 * Uma fonte de sorteios: cada chamada devolve o rand() do original, 15 bits (0..0x7FFF). Os testes passam
 * uma lambda com sequência fixa para repetir as mesmas jogadas do RE; a luta passa o FRandomStream dela
 * (UBattleFormulas::Rand15).
 */
using FJRPGRand15 = TFunctionRef<int32()>;

/** A entrada do núcleo do dano (FUN_801EC3E4): tudo que ele lê, já decidido por quem chama. */
struct JRPGFRAMEWORK_API FJRPGMeleeInput
{
	/** O Base Offense: o ATK base mais a dobra de equipamento do golpe (UBattleFormulas::EquipmentFold). */
	int32 BaseOffense = 0;

	/** O HP atual do atacante (HP >> 8 entra no ataque). */
	int32 AttackerHP = 0;

	/** A defesa do alvo que o golpe enfrenta: UDF se o byte de poder bate alto, LDF se baixo. */
	int32 DefenderDefense = 0;

	/** O byte de poder do golpe (dá o multiplicador e a metade da defesa). */
	int32 PowerByte = 0x18;

	/** O contador de juggle (1 no primeiro golpe da cadeia). */
	int32 Juggle = 1;

	/** O golpe é de uma Art: x13/10 (x11/10 no remendo) e o elemento aplicado duas vezes. */
	bool bArt = false;

	/** Atacante x defensor na matriz de elementos, em %. */
	int32 ElementPercent = 100;

	/** O alvo está no Spirit: a defesa x3. */
	bool bDefenderGuarding = false;
};

/** O que o núcleo do dano devolveu: o ataque, a defesa e a diferença. */
struct JRPGFRAMEWORK_API FJRPGMeleeResult
{
	int32 Offense = 0;
	int32 Defense = 0;
	int32 Damage = 0;

	/** O remendo do azarão (underdog) entrou: o ataque não passava a defesa. */
	bool bUnderdog = false;
};

/** A entrada do sorteio de bloqueio. */
struct JRPGFRAMEWORK_API FJRPGBlockInput
{
	/** SPD e ATK do atacante e do defensor; o ATK é o NÃO dobrado (o do snapshot). */
	int32 AttackerSPD = 0;
	int32 AttackerATK = 0;
	int32 DefenderSPD = 0;
	int32 DefenderATK = 0;

	int32 PowerByte = 0x18;

	/** O defensor escolheu Spirit: o lado dele x3/2. */
	bool bDefenderSpirit = false;

	/** O golpe do atacante é de uma Art: o lado dele x3/2. */
	bool bAttackerArt = false;
};

/**
 * UBattleFormulas
 * As contas da luta como funções ESTÁTICAS PURAS, só inteiras, nos truncamentos e na ordem do original
 * (Docs/BATTLE_ROADMAP.md › Phase 5 › Damage formula; RE docs/subsystems/battle-formulas.md e
 * crates/engine-battle-vm/src/battle_formulas/). Nunca float, nunca FMath::RoundToInt: um float pode
 * arredondar diferente em duas máquinas (PvP, regra "Não faça" do roadmap).
 *
 * Recebem o sorteio por parâmetro (FJRPGRand15 ou FRandomStream&), nunca um subsistema, e as constantes
 * ficam expostas: refinar uma regra depois não deve pedir recompilar a máquina de estados. Os sorteios
 * saem na ordem do original (ataque, defesa; o remendo e o piso só quando disparam); copiar a sequência
 * exata de rand() dele não é objetivo (ela era compartilhada com a apresentação).
 *
 * Fora das regras do original, por decisão (Deliberate departures): o termo de ângulo e o de distância do
 * ataque e da defesa são 0 (posições não alimentam as regras).
 */
UCLASS()
class JRPGFRAMEWORK_API UBattleFormulas : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --- Constantes (o que o original guarda em tabelas) ------------------------------------------

	/** 0x801F64EC: o multiplicador de cada byte de poder, indexado por (byte - 0x0C) % 5. */
	static constexpr int32 PowerScalars[5] = { 12, 18, 20, 22, 28 };

	/** 0x801F64E4: o peso do sorteio de bloqueio do atacante, mesmo índice. */
	static constexpr int32 BlockScalars[5] = { 6, 4, 4, 4, 2 };

	/** O golpe simples de direção: poder 20, defesa de cima (0x18) ou de baixo (0x1D); alcança qualquer alvo. */
	static constexpr int32 SwingUpperByte = 0x18;
	static constexpr int32 SwingLowerByte = 0x1D;

	/** Um byte de poder de 0x16 para cima alcança qualquer classe de alvo (sem miss pela altura). */
	static constexpr int32 ReachesAllByte = 0x16;

	/** A ordem de FJRPGCombatant::PieceATK. */
	static constexpr int32 PieceWeapon = 0;
	static constexpr int32 PieceRaSeru = 1;
	static constexpr int32 PieceShoes = 2;
	static constexpr int32 PieceBody = 3;
	static constexpr int32 PieceHead = 4;
	static constexpr int32 PieceCount = 5;

	/** O teto do dano (o excesso acima de Defense + 9999 é cortado). */
	static constexpr int32 DamageCap = 9999;

	/** A linha do tempo anda a 60 quadros lógicos por segundo (o vblank do original). */
	static constexpr int32 StepsPerSecond = 60;

	/** A velocidade normal do jogo (a escala 8 dos clips de golpe). */
	static constexpr int32 NormalGameSpeed = 8;

	/** O máximo de golpes que a IA enfileira por turno, e de sorteios não pagos antes de desistir. */
	static constexpr int32 MaxEnemyActions = 15;
	static constexpr int32 MaxEnemyPickMisses = 16;

	/** O teto do gauge de AGL com o Spirit (min(Max * 7/5 + 8, 288), Fase 7). */
	static constexpr int32 SpiritGaugeCap = 288;

	// --- Bytes de poder ---------------------------------------------------------------------------

	/** O multiplicador do golpe: [12,18,20,22,28][(b - 0x0C) % 5]. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 PowerScalar(int32 PowerByte);

	/** O peso do bloqueio do atacante para o byte: [6,4,4,4,2][(b - 0x0C) % 5]. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 BlockScalar(int32 PowerByte);

	/** O golpe bate na defesa de CIMA (UDF) quando (b - 0x0C) % 10 < 5; senão na de baixo (LDF). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static bool TargetsUpper(int32 PowerByte);

	/**
	 * O golpe erra pela altura (o limb-vs-height do original; só atacante herói): b < 0x16 contra a classe 2
	 * (voando) erra se b >= 0x11, contra a classe 3 (baixo) erra se b < 0x11. Não sorteia nada.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static bool LimbMisses(bool bAttackerIsHero, int32 PowerByte, int32 TargetClass);

	/** O byte do golpe simples de um botão: Low 0x1D (poder 20, LDF), os outros 0x18 (poder 20, UDF). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 SwingPowerByte(EJRPGArtButton Button);

	/**
	 * O byte de uma Art a partir do HitPower (12/18/20/22/28) e do HitTarget da linha do DT_Arts, com idx a
	 * posição do poder em [12,18,20,22,28]: Upper 0x16+idx, Lower 0x1B+idx, UpperMissesShort 0x0C+idx,
	 * LowerMissesFloating 0x11+idx. Um poder fora da lista cai no mais próximo.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 ArtPowerByte(int32 HitPower, EJRPGArtHitTarget Target);

	// --- Elemento ---------------------------------------------------------------------------------

	/** O id do elemento no original: 0 terra, 1 água, 2 fogo, 3 vento, 4 trovão, 5 luz, 6 trevas, 7 neutro (None, Evil). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 ElementId(EJRPGElement Element);

	/**
	 * A matriz 8x8 do original (0x801F53E8, linha = atacante), em % inteiro: o mesmo elemento 96, os pares
	 * opostos (terra-vento, água-fogo, luz-trevas) 104, o trovão contra qualquer um 102 (98 contra trevas), o
	 * resto e o neutro 100.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 ElementPercent(EJRPGElement Attacker, EJRPGElement Defender);

	// --- Dobra de equipamento ---------------------------------------------------------------------

	/**
	 * A parte do equipamento no Base Offense, por golpe e pela fonte dele (não pelo item): Arms ½ da arma,
	 * Ra-Seru ½ do tier, High e Low ½ do calçado, o starter nada, uma Art ½ da soma das cinco peças. O
	 * monstro não dobra nada. PieceATK é o FJRPGCombatant::PieceATK (ver PieceWeapon...).
	 */
	static int32 EquipmentFold(EJRPGHitSource Source, const TArray<int32>& PieceATK);

	// --- Sorteios ---------------------------------------------------------------------------------

	/** O rand() de 15 bits do FRandomStream da luta: os bits altos do LCG (os baixos alternam). */
	static int32 Rand15(FRandomStream& Rng);

	/** Rnd(x) do original: x + rand % (x/8 + 1), de 1 a 1,125 de x. Sempre gasta um sorteio. */
	static int32 Rnd(int32 Value, FJRPGRand15 Draw);

	// --- O golpe ----------------------------------------------------------------------------------

	/**
	 * O sorteio de bloqueio (FUN_801EC3E4 0x801EC5A8..0x801EC878): a = SPD + ATK*4/5 de cada lado, a = max(a, d),
	 * a += (rand % a) * BlockScalar >> 1, d += rand % d, Spirit no defensor e Art no atacante x3/2, bloqueia se
	 * a < d. Quem chama checa os dois portões ANTES (o defensor tem o clip de block e ainda está em pé); sem
	 * eles nenhum sorteio é gasto. Soma zero não bloqueia e não sorteia. (As ações de habilidade de acessório
	 * entram quando os acessórios existirem.)
	 */
	static bool RollBlock(const FJRPGBlockInput& Input, FJRPGRand15 Draw);
	static bool RollBlock(const FJRPGBlockInput& Input, FRandomStream& Rng);

	/**
	 * O núcleo do dano (FUN_801EC3E4): o ataque, a defesa, o remendo do azarão e o piso do chip, na ordem
	 * dos sorteios do original (ataque, defesa; o remendo e o piso só se disparam). Sem piso em 1: um atacante
	 * mais fraco que a defesa ainda dá um golpe que cresce com o seu rolamento, e todo golpe que acerta
	 * dá pelo menos 3. Os termos de ângulo e de distância são 0 (Deliberate departures).
	 */
	static FJRPGMeleeResult ResolveMelee(const FJRPGMeleeInput& Input, FJRPGRand15 Draw);
	static FJRPGMeleeResult ResolveMelee(const FJRPGMeleeInput& Input, FRandomStream& Rng);

	// --- Ordem de turno ---------------------------------------------------------------------------

	/**
	 * A chave de iniciativa da rodada (FUN_801DA780): SPD + rand % (SPD/2 + 1) + 1; o herói ganha o que falta
	 * de HP >> 4 / >> 5 / >> 6 (abaixo de ¼, abaixo de ½, o resto), o monstro >> 10; Slow divide por 2.
	 */
	static int32 InitiativeKey(bool bHero, int32 SPD, int32 HP, int32 MaxHP, bool bSlow, FJRPGRand15 Draw);

	/**
	 * O próximo a agir (FUN_801DABA4): quem tem a maior chave ainda não gasta; os empates saem do sorteio como
	 * no original (a lista começa no assento 0 e quem SOBE o máximo entra duas vezes, e rand % (n + 1) escolhe).
	 * INDEX_NONE se todas as chaves são 0. Gasta um sorteio por escolha.
	 */
	static int32 PickByInitiative(const TArray<int32>& Keys, FJRPGRand15 Draw);
	static int32 PickByInitiative(const TArray<int32>& Keys, FRandomStream& Rng);

	/**
	 * Os golpes que a IA do original enfileira (FUN_801E9FD4, o ramo físico): sorteia entre os candidatos
	 * (rand % quantos) e enfileira o sorteado enquanto o gauge de AGL paga o custo dele; um sorteio que não
	 * paga conta uma falta; para em 15 golpes ou 16 faltas. Devolve os índices na lista de custos, na ordem da fila.
	 * Sem candidatos nenhum sorteio é gasto.
	 */
	static TArray<int32> EnemySwingQueue(const TArray<int32>& Costs, int32 Gauge, FJRPGRand15 Draw);

	// --- Tempo ------------------------------------------------------------------------------------

	/**
	 * Quantos quadros lógicos (1/60 s) um clip leva para chegar a ClipFrames quadros dele, na escala de
	 * velocidade do jogo e no rate: o cursor anda Scale * Rate / 2 dezesseis-avos por quadro, então são
	 * ceil(32 * quadros / (Scale * Rate)). Na escala 8 e rate 2 são 2 por quadro do clip (30 quadros do clip
	 * por segundo).
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 ClipFramesToSteps(int32 ClipFrames, int32 GameSpeed, int32 Rate);

	/** O quadro lógico em que o golpe de um beat do disco é resolvido: o original dispara no 1º quadro com quadro + 1 >= beat. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 HitStepForBeat(int32 Beat, int32 GameSpeed, int32 Rate);

	/** A janela de uma reação em quadros lógicos (o juggle): ceil(32 * beat / (Scale * Rate)); beat 0 = nenhuma. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Formulas")
	static int32 ReactionWindow(int32 Beat, int32 GameSpeed, int32 Rate);

	/** a * Percent / 100 truncado, em 64 bits. */
	static int32 ScalePercent(int64 Value, int32 Percent);
};
