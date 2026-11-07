#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Party/CharacterData.h"
#include "EnemyData.generated.h"

class USkeletalMesh;
class UAnimSequence;

/**
 * FJRPGMonsterAnimSet
 * Os clips de batalha de um inimigo, um por papel. Os papéis são as tags que o disco dá às
 * ações do monstro (0 idle, 1 walk, 2/3 flinch, 4 knockdown, 5 get-up, 7/8/9 ready/recover/
 * defeat, 0x0B block); os golpes ficam em Attacks, na ordem do arquivo.
 *
 * Struct fixo e não TMap porque um TMap não importa direito de CSV. Quem preenche é o lote
 * da Fase 2 (Extras/ps1_glb_to_fbx.py + import), não a mão.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGMonsterAnimSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Idle;

	/** O ciclo de andar/voar até o alvo (o "Move" do monstro). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Flinch1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Flinch2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Knockdown;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> GetUp;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Ready;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Recover;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Defeat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TSoftObjectPtr<UAnimSequence> Block;

	/** Golpes e magias do monstro, na ordem das ações no disco. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Anims")
	TArray<TSoftObjectPtr<UAnimSequence>> Attacks;
};

/**
 * FJRPGEnemyData
 * Um inimigo de batalha (linha da DataTable DT_Enemies, gerada de
 * Docs/Data/gamedata/enemies.toml + a listagem do arquivo de monstros do disco via
 * Extras/generate_enemies_csv.py). O row name é a EnemyKey que os encontros usam.
 *
 * Stats, EXP e Gold são o REGISTRO CRU do disco, igual em todas as versões do jogo. O que o
 * jogador enfrenta e recebe sai das regras da versão (UJRPGBattleRules + EJRPGRuleset): na USA
 * o loader dá boost nos stats ao carregar a luta e o fim paga EXP x3/4 e Gold cortado duas
 * vezes; na versão japonesa não há boost e o pagamento é maior. A dificuldade
 * (UCoreSubsystem::ScaleEnemyHP/ATK/DEF, ScaleXPReward/ScaleGoldReward) e o level do encontro
 * entram por cima do que a versão deu.
 *
 * Guia: Docs/Guides/ENEMIES.md.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEnemyData : public FTableRowBase
{
	GENERATED_BODY()

	/** Igual ao row name ("gimard", "gimard_lv2"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	FName Key;

	/** Nome mostrado na batalha. As variantes de level usam o mesmo nome ("Gimard"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	FText DisplayName;

	/** Onde ele aparece no original (referência para montar os encontros). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	FString Location;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	bool bIsBoss = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	EJRPGElement Element = EJRPGElement::None;

	/**
	 * A classe de altura do monstro (+0x1E do registro): quais golpes do herói o alcançam. 0 = todo golpe
	 * alcança; 2 = voando, o golpe baixo ("Lower, misses floating") erra; 3 = baixo, o golpe alto ("Upper,
	 * misses short") erra. Os golpes de poder 0x16+ alcançam qualquer um. Ver UBattleFormulas::LimbMisses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	int32 TargetClass = 0;

	/**
	 * A classe de tamanho do monstro (+0x1F do registro): decide até onde o golpe dele e o dos heróis alcançam (a lei de
	 * alcance FUN_8004E2F0). O limiar de alcance é {256, 384, 1024}[classe] se a classe é menor que 3, senão classe << 4
	 * (unidades do PS1; 0.433 cm cada). O disco vai de 14 a 48. 0 = não se sabe (linha criada à mão): a apresentação usa
	 * uma classe padrão (AJRPGBattlePawn::DefaultSizeClass).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	int32 SizeClass = 0;

	// --- Stats (registro cru; UJRPGBattleRules::InstallEnemyStats dá os da luta) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseHP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseMP = 0;

	/** Gauge de ação: cada ação gasta; volta à base a cada rodada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseAGL = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseATK = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseUDF = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseLDF = 0;

	/** Iniciativa: quem age primeiro na rodada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseSPD = 0;

	/** Dano e defesa mágicos; também alimenta acerto/esquiva. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Stats")
	int32 BaseINT = 0;

	// --- Recompensa ---

	/** EXP cru do registro. O que a luta paga: UJRPGBattleRules::VictoryEXPPool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	int32 EXP = 0;

	/** Gold cru do registro. O que a luta paga: UJRPGBattleRules::VictoryGold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	int32 Gold = 0;

	/** Key na DT_Items. None = não dropa nada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	FName DropItem;

	/** Chance do drop, em %. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	int32 DropChance = 0;

	/** Key na DT_Items do que dá para roubar (Evil God Icon). None = nada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	FName StealItem;

	/** Chance do roubo, em %. 0 = a regra padrão do roubo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	int32 StealChance = 0;

	/** Seru que o golpe final pode absorver ("Gimard"); None = não é capturável. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	FName CaptureSeru;

	/** Chance da absorção, em % (Magic Boost / Ivory Book somam por cima). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Rewards")
	int32 CatchRate = 0;

	// --- Modelo (Fase 2) ---

	/** Slot do monstro no arquivo do disco (PROT 0867, 1..194); -1 = sem modelo no disco. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Model")
	int32 MonsterArchiveID = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Model")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Model")
	FJRPGMonsterAnimSet Anims;
};
