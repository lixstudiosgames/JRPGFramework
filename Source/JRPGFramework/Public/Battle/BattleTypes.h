#pragma once

#include "CoreMinimal.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Engine/DataTable.h"
#include "Party/CharacterData.h"
#include "Arts/JRPGArtsRules.h"
#include "BattleTypes.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimInstance;

/** De que lado da luta. */
UENUM(BlueprintType)
enum class EJRPGBattleSide : uint8
{
	Party,
	Enemy
};

/** O que é um combatente. Os Seru entram como membros de apoio (Fase 8). */
UENUM(BlueprintType)
enum class EJRPGCombatantType : uint8
{
	Character,
	Seru,
	Enemy
};

/** Como a luta acabou. */
UENUM(BlueprintType)
enum class EJRPGBattleResult : uint8
{
	Victory,
	Defeat,
	Escaped,
	/** Encerrada de fora (comando de debug, troca de mapa): sem recompensa nem game over. */
	Aborted
};

/** O que o jogador (ou a IA) manda um combatente fazer na rodada. Só Attack e Arts existem na Fase 5. */
UENUM(BlueprintType)
enum class EJRPGBattleCommandKind : uint8
{
	None,
	Attack,
	Arts,
	/** Fase 7. */
	Item,
	/** Fase 8. */
	Magic,
	/** Fase 7. */
	Escape,
	/** O Spirit do original, Fase 7. */
	Guard
};

/** O que chega durante a execução, carimbado com o quadro lógico. */
UENUM(BlueprintType)
enum class EJRPGBattleInputKind : uint8
{
	/** O parry planejado (BATTLE_ROADMAP.md › Open questions, 5): a janela e o efeito ainda não existem. */
	Parry
};

/** O que a resolução conta; a UI monta o texto a partir daqui. */
UENUM(BlueprintType)
enum class EJRPGBattleEventType : uint8
{
	/** Value = o número da rodada. */
	RoundStart,
	/** Value = o TargetSlot do começo da ação. */
	ActionStart,
	/** Value = o dano; SlotIndex bate em TargetSlot. */
	Damage,
	/** O golpe errou pela altura (o limb-vs-height do original). */
	Miss,
	/** O defensor (TargetSlot) bloqueou o golpe de SlotIndex. */
	Block,
	Heal,
	/** SlotIndex = quem caiu. */
	Death,
	/** Value = o AP novo de SlotIndex. */
	APChanged,
	/** A ação chegou a uma Art (FJRPGBattleEvent::ArtKey; Value = a entrada da cadeia) e, se ela era nova, a aprendeu. */
	ArtRecognized,
	ArtLearned,
	/** A ação acabou (ou foi descartada: Value = 1). */
	ActionEnd,
	/**
	 * O resumo da combo, no fim da ação, para cada alvo que levou algum golpe: SlotIndex bate em TargetSlot, Count =
	 * quantos golpes acertaram, Value = o dano de TODOS somado, inclusive o que passou do HP (o HIT / TOTAL do original).
	 */
	ComboTotal
};

/** Bits de FJRPGBattleEvent::Flags (a apresentação escolhe o clip de reação por eles). */
struct FJRPGBattleEventFlag
{
	/** O golpe derruba (a reação pesada, o knockdown do original). */
	static constexpr int32 Knockdown = 1 << 0;
	/** O golpe foi baixo (o flinch 2) e não alto (o flinch 1). */
	static constexpr int32 LowHit = 1 << 1;
	/** O golpe é de uma Art. */
	static constexpr int32 ArtHit = 1 << 2;
};

/**
 * As fases de uma ação, só da APRESENTAÇÃO: o AJRPGBattlePawn toca isto em compasso com a timeline
 * lógica. Mora no pawn, nunca no combatente (as regras não têm fase nem timer).
 */
UENUM(BlueprintType)
enum class EJRPGActionPhase : uint8
{
	Idle,
	Windup,
	Advance,
	Strike,
	Chain,
	Recovery,
	Return,
	Done
};

/** Onde a rodada está: Commands (todo mundo escolhe) ou Execution (a timeline lógica toca). */
UENUM(BlueprintType)
enum class EJRPGBattleStage : uint8
{
	None,
	Commands,
	Execution,
	Finished
};

/** De onde vem o golpe, para a dobra de equipamento (quem bate com o quê) e a regra do bloqueio. */
UENUM(BlueprintType)
enum class EJRPGHitSource : uint8
{
	/** Golpe de monstro: o ATK dele como está. */
	Monster,
	/** Botão Arms: ½ do ATK da arma. */
	Arms,
	/** Botão Ra-Seru: ½ do ATK do tier do Ra-Seru. */
	RaSeru,
	/** Botão High: ½ do ATK do calçado. */
	High,
	/** Botão Low: ½ do ATK do calçado. */
	Low,
	/** O starter: nada. */
	Starter,
	/** Golpe de uma Art: ½ da soma das cinco peças. */
	Art
};

/** Uma condição na luta: o contrato (duração, quando age, o efeito por turno); as condições em si são da Fase 8. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleCondition
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName Name;

	/** Rodadas que faltam; -1 = até ser curada. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 RoundsLeft = -1;

	/** Age no começo do turno de quem a tem (senão, no fim). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bActsAtTurnStart = true;

	/** O dano que ela dá a cada turno de quem a tem (o veneno). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 DamagePerTurn = 0;

	/** Quem a tem perde o turno (o sono). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bSkipsTurn = false;

	/** Continua depois da luta, no registro do herói. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bSurvivesFight = false;
};

/**
 * Uma entrada de ação do monstro como as regras a veem (a cópia, no combatente, de uma linha do
 * DT_EnemyActions): o que a IA enfileira e o que a linha do tempo precisa. Dados do disco, inteiros.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleActionEntry
{
	GENERATED_BODY()

	/** A ação do DT_EnemyActions; -1 = o golpe padrão (monstro sem linha de golpe: a apresentação toca o primeiro ataque do conjunto). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ActionIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Tag = 0;

	/** Um byte de poder por golpe (o 1º é o do primeiro golpe). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> Power;

	/** Os quadros de golpe do clip, em ordem. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> HitFrames;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 AGLCost = 0;

	/** 0 deixa o próximo golpe do turno começar no meio do clip. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Gate = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Rate = 2;

	/** Os quadros do clip (o tamanho dele). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 FrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName Clip;
};

/**
 * A janela de uma reação (flinch, block): o beat e o rate da entrada. Beat 0 = sem janela. FrameCount é o tamanho do
 * clip em quadros (0 = desconhecido): a janela nunca passa do clip (o disco tem um beat 10 num clip de 7 quadros).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleReactionClip
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Beat = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Rate = 1;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 FrameCount = 0;
};

/**
 * O golpe simples de um botão do herói, lido do registro de golpe do equipamento (DT_HeroSwings): os bytes de poder (um por
 * golpe), os quadros de golpe do clip (os "beats") e o rate, para o quadro lógico de cada golpe sair do dado.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleSwingProfile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> PowerBytes;

	/** O beat de cada golpe (a mesma quantidade de PowerBytes); vazio = sem quadro lido, vale o quadro padrão. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> Beats;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Rate = 2;
};

/**
 * Quanto cada reação de um combatente dura, em quadros lógicos (vsyncs) na velocidade normal do jogo (escala 8): é o que o
 * resolvedor espera depois de um golpe antes de a próxima ação poder começar (o estado 0x20 do original só solta quando o alvo
 * volta ao idle; Docs/Reference/BATTLE_TIMING.md, seções 3 e 10). DADO do disco (o tamanho e o rate do clip, 4 * N / R), nunca
 * o clip que o pawn toca: a lógica não depende de geometria nem de asset. 0 = não se sabe: vale o padrão de
 * FJRPGTimelineDefaults (herói: o Vahn; monstro: a mediana do disco).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleReactionLengths
{
	GENERATED_BODY()

	/** O flinch de golpe alto (tag 2) e de golpe baixo (tag 3). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 FlinchHighFrames = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 FlinchLowFrames = 0;

	/** O clip de block (tag 0x0B). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 BlockFrames = 0;

	/** O knockdown (tag 4) e o levantar (tag 5). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 KnockdownFrames = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 GetUpFrames = 0;

	/** Herói: a entrada 7 depois do knockdown de quem morreu (ele solta a ação ao chegar na 8, que repete para sempre). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 DyingFrames = 0;

	/** Monstro: o fade de derrota depois do knockdown (o corpo some; o original gasta 0x100 / 8 = 32 passos). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 DeathFadeFrames = 0;
};

/**
 * Um combatente da luta. SÓ REGRAS, tudo inteiro: sem fase, sem timer, nada que o pawn mostre (a
 * fase da ação mora no AJRPGBattlePawn). É uma FOTO tirada no BeginBattle — a resolução nunca lê o
 * UPartySubsystem, o UStatusSubsystem nem o UInventorySubsystem (BATTLE_ROADMAP.md › 5.0 e
 * PvP › What the core cannot do).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGCombatant
{
	GENERATED_BODY()

	/** Índice na lista da luta (heróis primeiro, depois inimigos). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SlotIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGBattleSide Side = EJRPGBattleSide::Party;

	/** Índice dentro do lado: o lugar da formação (0 = o líder / o primeiro inimigo). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SideIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGCombatantType Type = EJRPGCombatantType::Character;

	/** A regra pergunta "mesmo time?"; a vitória é "um time inteiro caído". */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Team = 0;

	/** Quem digita os comandos dele; -1 = a IA. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 OwnerPlayer = -1;

	/** O membro da party (DT_Characters) ou a row do DT_Enemies. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName Key;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 HP = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 MaxHP = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 MP = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 MaxMP = 0;

	/**
	 * Herói: o ATK BASE, sem equipamento (o carregador do original não o dobra: a dobra é por golpe).
	 * Inimigo: o ATK de UJRPGBattleRules::InstallEnemyStats mais a dificuldade.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ATK = 0;

	/** Herói: UStatusSubsystem::GetEffectiveStat (equipamento e Ra-Seru já dobrados na carga, como o FUN_80053CB8). Inimigo: InstallEnemyStats + dificuldade. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 UDF = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 LDF = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SPD = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 INT = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 AGL = 0;

	/**
	 * Herói: o ATK de cada peça, por papel, na ordem de UBattleFormulas::PieceWeapon.. (Weapon, Ra-Seru
	 * — o tier —, Shoes, Body, Head). Dobrados por golpe (UBattleFormulas::EquipmentFold).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> PieceATK;

	/** Herói: Vahn fogo, Noa vento, Gala trovão, Terra vento (o RaSeruElement). Inimigo: o DT_Enemies. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGElement Element = EJRPGElement::None;

	/** Inimigo: o +0x1E do registro (o miss pela altura). Herói: 0. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 TargetClass = 0;

	/**
	 * Inimigo: o +0x1F do registro, a classe de tamanho que decide até onde o golpe alcança (a lei de alcance
	 * FUN_8004E2F0: o limiar é {256, 384, 1024}[classe] se menor que 3, senão classe << 4, em unidades do PS1). 0 = não
	 * se sabe (o disco só tem 14..48): a apresentação cai numa classe padrão. Herói: 0.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SizeClass = 0;

	/** O medidor persistente (as Arts somam, os especiais gastam). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 AP = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 MaxAP = 100;

	/** O pool da AGL: o orçamento da janela de entrada do herói e o da IA do inimigo. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 CommandGauge = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 MaxCommandGauge = 0;

	/** Herói: o custo de cada botão no gauge, indexado por EJRPGArtButton (Arms, Ra-Seru, High, Low): 30 / 42 / 54. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> ButtonCost;

	/**
	 * Herói: o byte de poder do golpe simples de cada botão, indexado por EJRPGArtButton (Arms, Ra-Seru, High, Low). No
	 * original ele vem do registro de golpe do equipamento (calçado: o chute baixo e o alto; arma e Ra-Seru: os braços).
	 * Vazio, ou um 0 no índice, vale o padrão do herói (UBattleFormulas::DefaultSwingPowerByte).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> SwingBytes;

	/**
	 * Herói: o golpe simples de cada botão (indexado por EJRPGArtButton) lido do equipamento: os bytes de poder, os quadros de
	 * golpe e o rate. Vazio, ou um botão sem bytes, vale SwingBytes / o byte padrão e o quadro padrão da timeline.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGBattleSwingProfile> SwingProfiles;

	/** Herói: as Arts que sabe (cresce quando uma Art é aprendida na luta). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FName> KnownArts;

	/** Herói: tem o Ra-Seru (destrava a Hyper e a Miracle, Fase 6). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bHasRaSeru = true;

	/** Herói: a chance, em %, de uma Regular desconhecida ser aprendida ao ser feita. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 LearnOnUseChance = 100;

	/** Herói: canhoto (a Noa): o Arms é a direita. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bLeftHanded = false;

	/** Tem o clip de block (tag 0x0B): só quem tem rola o bloqueio. Os heróis têm. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bHasBlock = false;

	/** Tem o clip de levantar (a reação de knockdown do original só vale com ele). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bHasGetUp = false;

	/** A janela do flinch leve de golpe alto (tag 2) e baixo (tag 3), do juggle. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGBattleReactionClip FlinchHigh;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGBattleReactionClip FlinchLow;

	/** A janela do clip de block. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGBattleReactionClip BlockClip;

	/** Quanto duram as reações dele (flinch, block, knockdown + levantar, morrer): o que o resolvedor espera antes da próxima ação. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGBattleReactionLengths Reactions;

	/** Inimigo: os golpes que a IA sorteia (tag 0x0C..0x1F com custo de AGL), na ordem do arquivo. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGBattleActionEntry> Swings;

	/** A chave de iniciativa da rodada; 0 = já agiu (ou caiu). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 InitiativeKey = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 TargetSlot = -1;

	/** Quantos golpes seguidos entraram na janela de reação dele (o juggle do kernel). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 JuggleCounter = 0;

	/** O quadro lógico em que a reação atual acaba (o juggle enquanto o quadro é menor); 0 = parado. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ReactionUntilFrame = 0;

	/** A reação atual é o block (a janela dele força o próximo bloqueio). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bReactionIsBlock = false;

	/**
	 * O quadro lógico em que a reação inteira do último golpe que ele tomou acaba (flinch, block, knockdown + levantar, queda +
	 * Dying, queda + fade): a próxima ação espera por isto (o estado 0x20 do original). É diferente de ReactionUntilFrame, que é só
	 * a janela do juggle. 0 = nada a esperar.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ReactionHoldUntilFrame = 0;

	/** O Spirit do original (Fase 7). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bGuarding = false;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGBattleCondition> Conditions;
};

/** O pedido de uma luta: quem entra e onde ela acontece. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleSetup
{
	GENERATED_BODY()

	/** Os heróis, na ordem da formação. Vazio = a party ativa (UPartySubsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FName> Party;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FJRPGEncounterCombatant> Enemies;

	/**
	 * Onde foi o encontro. O palco monta a luta na arena dele (AJRPGBattleArena::FindArenaFor);
	 * sem arena no mapa, monta aqui mesmo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FVector Location = FVector::ZeroVector;

	/** Sem arena: para onde a party olha (o yaw de quem encostou até o inimigo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	float FacingYaw = 0.0f;

	/**
	 * A semente do Rng da luta: mesma semente + mesmos comandos + mesmos inputs = mesmos eventos. 0 =
	 * o BeginBattle sorteia uma (fora das regras, como o sorteio do encontro) e a guarda aqui, em
	 * GetBattleSetup(), para a luta poder ser repetida.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 Seed = 0;

	/**
	 * A flag de luta roteirizada do original (ctx+0x287), que é da formação: os inimigos entram com o perfil
	 * de stats dos chefes (UJRPGBattleRules::InstallEnemyStats), a luta não pode ser fugida e não rola a
	 * vantagem de formação. Uma luta de DT_Bosses copia FJRPGBossData::bScriptedFight; encontro aleatório: falso.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	bool bScriptedFight = false;

	/** Luta que não paga nada (_DAT_8007BAC0 no original, as lutas especiais). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	bool bNoRewards = false;

	/**
	 * A vantagem de formação já sorteada (FUN_80051D84, Fase 7): 0 nenhuma, 1 ataque pelas costas (a party
	 * perde as chaves de iniciativa da rodada 1), 2 ataque surpresa (os inimigos perdem). A Fase 5 só honra o
	 * que vier aqui; quem sorteia é a Fase 7, e uma luta roteirizada não sorteia.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 FormationAdvantage = 0;
};

/** Um comando, um ponto de entrada: conferido contra quem é dono do slot, nunca confiado (regra 22). */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleCommand
{
	GENERATED_BODY()

	/** Quem manda (índice do jogador; 0 no jogo local). Tem que ser o OwnerPlayer do slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 PlayerIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 Slot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	EJRPGBattleCommandKind Kind = EJRPGBattleCommandKind::None;

	/** O alvo, por slot (a resolução decide alcance e acerto por slot, nunca por posição). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 TargetSlot = -1;

	/** A cadeia inteira da janela de entrada: um byte por botão (UJRPGArtsRules::EncodeArtBytes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<uint8> ArtBytes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FName ItemKey;
};

/** O que chega durante a execução, carimbado com o quadro lógico em que foi apertado. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 PlayerIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 Slot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	EJRPGBattleInputKind Kind = EJRPGBattleInputKind::Parry;

	/** O golpe que o input responde (o índice em FJRPGActionTimeline::Hits). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 HitIndex = 0;

	/** O quadro lógico em que foi apertado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 Frame = 0;
};

/** Um fato da luta, na ordem em que a resolução o produz. A apresentação o mostra quando o clip chega. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGBattleEventType Type = EJRPGBattleEventType::RoundStart;

	/** Quem faz (ou de quem é, na Death / APChanged). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SlotIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 TargetSlot = -1;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Value = 0;

	/** Qual ação (um contador da luta inteira) e qual golpe dela: o notify sabe qual evento mostrar. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ActionIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 HitIndex = -1;

	/**
	 * O quadro lógico da luta em que aconteceu. Nas regras (FJRPGBattleResolver::GetEvents, UBattleSubsystem::GetEventLog) é o
	 * State.Frame cru. No OnBattleEvent do UBattleSubsystem já vem somado ao GetPresentationOffset, no relógio de apresentação
	 * (o do OnLogicFrame), que também anda na digitação dos comandos.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Frame = 0;

	/** Bits de FJRPGBattleEventFlag. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Flags = 0;

	/** ComboTotal: quantos golpes acertaram o alvo. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Count = 0;

	/**
	 * A escala de velocidade do jogo em vigor quando o fato aconteceu (8 normal, 4 / 2 durante uma Art): a reação de quem apanha
	 * toca nela, e o resolvedor mede a espera pela mesma escala. Death: Value traz o fade de derrota do monstro (0 no herói).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 GameSpeed = 8;

	/** Na ArtRecognized / ArtLearned: a key da Art no DT_Arts. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName ArtKey;
};

/** O tipo de passo de uma ação (para a apresentação escolher o clip). */
UENUM(BlueprintType)
enum class EJRPGActionStepKind : uint8
{
	Swing,
	ArtStarter,
	SpecialStarter,
	Art,
	/** O golpe de um monstro (a ação ActionIndex do DT_EnemyActions). */
	EnemySwing
};

/** Um clip da ação, em quadros lógicos a partir do começo dela. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGActionStep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGActionStepKind Kind = EJRPGActionStepKind::Swing;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 StartFrame = 0;

	/** Onde o passo acaba: o corte do clip (o Chain Cut), se há outro depois. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 EndFrame = 0;

	/** O clip básico do herói (SwingL..U, ArtStarter, SpecialStarter) ou o clip do monstro. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName ClipName;

	/** A Art (row do DT_Arts) do passo; None num golpe solto. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName ArtKey;

	/** A escala de velocidade do jogo neste passo (8 normal, 4 numa Art, 2 depois do SpecialStarter). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 GameSpeed = 8;

	/** Monstro: o ActionIndex da entrada. Herói: a entrada da cadeia (FJRPGArtChain::Entries). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 EntryIndex = -1;
};

/** Um golpe da ação: o quadro em que ele é resolvido e o byte de poder. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGActionHit
{
	GENERATED_BODY()

	/** Quadros lógicos a partir do começo da ação. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Frame = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 PowerByte = 0x18;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGHitSource Source = EJRPGHitSource::Arms;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 StepIndex = 0;

	/** A entrada da cadeia a que o golpe pertence (-1 no monstro). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ChainEntry = -1;

	/** O primeiro golpe da entrada (onde a Art soma o AP). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bFirstOfEntry = false;
};

/** O que a ação faz fora dos golpes, num quadro: o AP que anda e o aviso de uma Art. */
UENUM(BlueprintType)
enum class EJRPGActionMarkKind : uint8
{
	/** Value = o AP novo (o plano da cadeia, APBefore -> APAfter). */
	SetAP,
	ArtRecognized,
	ArtLearned
};

USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGActionMark
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Frame = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGActionMarkKind Kind = EJRPGActionMarkKind::SetAP;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName ArtKey;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Value = 0;
};

/**
 * FJRPGActionTimeline
 * Os quadros de uma ação, montados QUANDO ELA COMEÇA, antes de qualquer coisa tocar: o total, o golpe
 * de cada quadro com o byte de poder, os passos (clips). Sempre de DADOS, nunca de um notify
 * disparando: o monstro vem da linha do DT_EnemyActions, o herói do tamanho do clip e dos
 * marcadores lidos como dado, e sem clip valem os padrões por tipo (BATTLE_ROADMAP.md › 5.0 › The
 * logic timeline). A resolução só vê isto, então os testes alimentam a struct direto, sem asset.
 *
 * Quadro lógico = 1/60 s (o vblank do original); o clip de rate R na escala E anda E·R/32 quadros do
 * clip por quadro lógico (UBattleFormulas::ClipFramesToSteps).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGActionTimeline
{
	GENERATED_BODY()

	/** O número da ação na luta (o ActionIndex dos eventos). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ActionIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SlotIndex = -1;

	/** O quadro lógico das regras (State.Frame) em que a ação começou. A cena soma o GetPresentationOffset ao dar a cópia aos pawns. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 StartFrame = 0;

	/** O tempo de chegar ao alvo antes do primeiro passo (a apresentação anda; as regras só esperam). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 LeadFrames = 0;

	/** Do começo da ação até ela acabar, a recuperação incluída. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 TotalFrames = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGActionStep> Steps;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGActionHit> Hits;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGActionMark> Marks;

	/** Algum passo usou os quadros padrão (clip ausente, sem marcador de golpe): foi registrado no log. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bUsedDefaults = false;
};

/** A ação que está tocando: o comando, a cadeia decidida, a timeline e onde ela está. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGActiveAction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Slot = -1;

	/** O alvo de agora (muda quando ele cai no meio da ação e há outro). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 TargetSlot = -1;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGBattleCommand Command;

	/** Herói em Arts / Attack: a cadeia que os botões deram (FJRPGArtChain), do snapshot e do Rng da luta. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGArtChain Chain;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGActionTimeline Timeline;

	/** Quadros desde o começo da ação. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Elapsed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 NextHit = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 NextMark = 0;

	/** O dano acumulado em cada slot por esta ação, sem teto no HP (a regra do knockdown e o TOTAL do original). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> ComboDamage;

	/** Quantos golpes acertaram cada slot nesta ação (o HIT do original; o bloqueado e o que erra não contam). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> ComboHits;
};

/**
 * FJRPGBattleState
 * A luta inteira numa struct: mesma semente + mesmos comandos + mesmos inputs = mesma lista de eventos. O
 * Rng é só das regras (aleatoriedade cosmética usa outro stream).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGCombatant> Combatants;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FRandomStream Rng;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Round = 0;

	/** Quem agiu nesta rodada, na ordem dos sorteios da iniciativa. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<int32> TurnOrder;

	/** O contador da timeline lógica (1/60 s), da luta inteira. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Frame = 0;

	/**
	 * Depois de uma ação a luta ainda espera, na ordem do original (estados 0x20, 0x51, 0x5A): o clip do atacante e a reação do
	 * alvo (ReactionHoldUntilFrame), a faixa Done de 60 vsyncs, e SÓ ENTÃO o teste de vitória / derrota (FinishCheckFrame); a
	 * próxima ação só entra uns passos depois (NextActionNotBefore).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bFinishCheckPending = false;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 FinishCheckFrame = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 NextActionNotBefore = 0;

	/** As recompensas já foram pagas (o EndBattle confere; uma segunda chamada não paga nada). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bRewardsPaid = false;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGBattleStage Stage = EJRPGBattleStage::None;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bScriptedFight = false;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 FormationAdvantage = 0;

	/** O próximo ActionIndex dos eventos. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 NextActionIndex = 0;

	/** O comando de cada slot nesta rodada (Kind None = ainda não entrou). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGBattleCommand> Commands;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FJRPGActiveAction Action;

	/** Os inputs aceitos durante a execução (o parry), carimbados. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	TArray<FJRPGBattleInput> Inputs;

	/** Quem está agindo (Execution) ou digitando (Commands); -1 entre uma e outra. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 ActiveSlot = -1;

	/** A luta acabou (a resolução decidiu: um time inteiro caído); o BattleSubsystem chama o EndBattle. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	bool bFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGBattleResult Result = EJRPGBattleResult::Aborted;
};

/**
 * As partes do modelo de batalha de um herói, uma por seção de equipamento do jogo original.
 * O jogo não tem malha de item: cada seção troca objetos inteiros do esqueleto (a arma troca a
 * mão por outra mão com lâmina), e o herói é a soma das cinco (Docs/Guides/PS1_ASSETS.md,
 * "Equipment pieces"). A Noa é canhota, mas a parte é pelo papel, não pelo lado: Weapon é o braço
 * da arma de quem for.
 */
UENUM(BlueprintType)
enum class EJRPGBattlePiece : uint8
{
	Body,    // EJRPGEquipSlot::Armor
	Head,    // EJRPGEquipSlot::Helmet
	Weapon,  // EJRPGEquipSlot::Weapon
	RaSeru,  // o SeruLevel (1..9) — não é slot de equipamento
	Feet,    // EJRPGEquipSlot::Shoes

	MAX      UMETA(Hidden)
};

/** Uma parte do herói na luta: a malha (no esqueleto do BattleMesh) que ocupa aquela seção. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattlePieceMesh
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	EJRPGBattlePiece Piece = EJRPGBattlePiece::Body;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<USkeletalMesh> Mesh;
};

/**
 * FJRPGEquipVisualRow
 * Uma linha da DT_EquipVisuals: a malha de uma parte do herói com um equipamento (ou a padrão).
 * O row name é a chave da busca: <Personagem>_<Parte>_<Item>, com o Item sendo a chave da
 * DT_Items, "Lv<N>" para o Ra-Seru no nível N, ou "Default" para a parte sem equipamento — ex.:
 * Vahn_Weapon_survival_knife, Vahn_RaSeru_Lv3, Vahn_Body_Default. Gerada pelo
 * Extras/generate_equip_visuals_csv.py a partir das peças do Extras/ps1_equip_pieces.py.
 *
 * Herói com alguma linha Default na tabela entra na luta montado por partes; sem nenhuma, entra
 * com o BattleMesh inteiro (como antes).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEquipVisualRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FName Character;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	EJRPGBattlePiece Piece = EJRPGBattlePiece::Body;

	/** Chave da DT_Items, Lv<N> (Ra-Seru) ou Default. Informativo: a busca é pelo row name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FName Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<USkeletalMesh> Mesh;
};

/**
 * O modelo de batalha de um combatente: malha e animação. Ou um Anim Blueprint, ou o clip Idle
 * tocado direto.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleVisual
{
	GENERATED_BODY()

	/**
	 * A malha que anima. Com Pieces, fica invisível e só conduz a pose: as partes a seguem
	 * (Leader Pose) — por isso todas usam o esqueleto dela.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Opcional. Com ele, o IdleAnim é ignorado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSubclassOf<UAnimInstance> AnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0.01"))
	float Scale = 1.0f;

	/** O herói montado por partes (o que ele tem equipado). Vazio = a Mesh inteira aparece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FJRPGBattlePieceMesh> Pieces;

	/** Malhas presas a bones da Mesh (os acessórios). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FJRPGBattleAttachment> Attachments;
};
