#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EnemyActionData.generated.h"

class UAnimSequence;

/**
 * FJRPGEnemyActionData
 * Uma ação de um monstro — a entrada de ação do disco (linha da DataTable DT_EnemyActions, gerada
 * por Extras/generate_enemy_actions_csv.py a partir do arquivo de monstros, PROT 0867). Uma linha por
 * clip: o row name é <EnemyKey>_<NN>_<Clip> (NN = o índice da entrada no arquivo).
 *
 * É daqui que a luta tira a linha do tempo lógica do inimigo, NUNCA de um notify: os quadros de golpe
 * (a lista de eventos do original), o poder de cada golpe (um byte por golpe), o custo de AGL que a IA
 * paga para enfileirar o golpe, o rate do clip. Ver Docs/BATTLE_ROADMAP.md › 5.0 › The logic timeline
 * e Docs/Guides/ENEMIES.md.
 *
 * Formato (legend-of-legaia-re, docs/formats/monster-animation.md): +0x00 tag (e o 1º byte de poder),
 * +0x00..+0x03 poder, +0x10..+0x13 quadros de golpe, +0x74 custo de AGL, +0x76 gate, +0x78 rate,
 * +0x7A seletor de status, +0x8C [partes][quadros]…
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEnemyActionData : public FTableRowBase
{
	GENERATED_BODY()

	/** A row do DT_Enemies de quem tem a ação. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	FName Enemy;

	/** O slot do monstro no arquivo do disco (o MonsterArchiveID dele). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 ArchiveID = -1;

	/** O índice da entrada no arquivo do monstro (a ordem dos clips). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 ActionIndex = 0;

	/**
	 * O papel da ação: 0 idle, 1 andar, 2/3 flinch, 4 knockdown, 5 levantar, 7/8/9 ready/recover/defeat,
	 * 0x0B (11) block, 0x0C..0x1F golpes e magias (0x0D/0x0E/0x0F os ataques), 0x20..0x22 aproximação,
	 * 0x23 o clip da magia.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 Tag = 0;

	/** O nome do clip (Idle, Move, Attack1, Action_0x23…). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	FString Clip;

	/** O clip no Unreal (AS_<Monstro>_<Clip>); vazio se o monstro ainda não foi importado. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	TSoftObjectPtr<UAnimSequence> Anim;

	/**
	 * Os bytes de poder, os quatro de +0x00..+0x03, um por golpe (o índice do golpe escolhe o byte). Cada byte dá
	 * o multiplicador (12/18/20/22/28) e a defesa que o golpe enfrenta: ver UBattleFormulas::PowerScalar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	TArray<int32> Power;

	/** Os quadros de golpe do clip (+0x10..+0x13), em ordem. Vazio = a ação não bate nem tem janela. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	TArray<int32> HitFrames;

	/** O custo de AGL para enfileirar o golpe (+0x74). 255 = não é ataque. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 AGLCost = 0;

	/** O gate do evento (+0x76): 0 deixa o próximo golpe do turno começar no meio do clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 Gate = 0;

	/** O rate de reprodução (+0x78), 1 ou 2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 Rate = 2;

	/** O seletor de status/impacto (+0x7A). Fase 8. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 StatusSelector = 0;

	/** Os quadros do clip (o tamanho dele no disco). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 FrameCount = 0;

	/**
	 * A velocidade de raiz (+0x0C), em unidades do PS1 por vsync ao longo da direção do ator, enquanto o clip toca. Negativa (as
	 * reações: knockdown, block, flinch) recua o clip inteiro; positiva (andar, investidas) só anda enquanto o alvo está fora do
	 * alcance. Na tag 1 é a velocidade da caminhada do monstro.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 RootSpeed = 0;

	/**
	 * O passo do fim do clip (+0x0E), em unidades do PS1 ao longo da direção: o quanto o clip deixa o corpo de onde começou. No fim
	 * natural o ator anda isso (a pose volta ao começo e o corpo fica onde o clip o deixou); num corte, a fração tocada.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Action")
	int32 EndStep = 0;

	/** Um golpe que a IA do original sorteia: tag 0x0C..0x1F com um custo de AGL de verdade. */
	bool IsSwing() const { return Tag >= 0x0C && Tag <= 0x1F && AGLCost != 0xFF; }
};
