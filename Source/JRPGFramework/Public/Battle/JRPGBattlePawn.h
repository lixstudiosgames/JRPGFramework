#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Engine/TimerHandle.h"
#include "Battle/BattleTypes.h"
#include "Battle/EnemyActionData.h"
#include "Battle/EnemyData.h"
#include "JRPGBattlePawn.generated.h"

class AJRPGBattleArena;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;

/**
 * AJRPGBattlePawn
 * Um combatente na cena da luta: o modelo de batalha (não o do Field) parado no seu lugar da
 * arena. Criado e destruído pela AJRPGBattleArena da luta, um por combatente. O modelo vem do
 * DT_Characters (BattleMesh/BattleAnims) para os heróis e do DT_Enemies (Mesh/Anims) para os
 * inimigos.
 *
 * O ator fica com os pés no lugar da formação; a cápsula se ajusta à malha (serve para a câmera
 * e, na Fase 5, para mirar). Sem Anim Blueprint, o clip Idle toca em loop e a troca de clip
 * (PlayClip) mistura com o anterior — a mesma animação nativa dos inimigos do Field, que serve
 * para qualquer esqueleto do PS1.
 *
 * Herói com equipamento visível (FJRPGBattleVisual::Pieces): a Mesh continua sendo quem anima,
 * mas invisível; cada parte é um componente que segue a pose dela (Leader Pose). Os acessórios
 * são malhas estáticas presas aos bones da Mesh.
 *
 * Na luta (Fase 5) o pawn é só APRESENTAÇÃO: toca os clips em compasso com a linha do tempo lógica
 * (BeginAction + UpdatePlayback, pelo contador de quadros do UBattleSubsystem, nunca por notify nem
 * por timer próprio) e mostra os eventos que as regras já resolveram (ShowEvent: a reação do alvo e o
 * texto em cima da cabeça). A fase da ação (EJRPGActionPhase) mora aqui e não no combatente. Ninguém
 * volta para o lugar depois do turno ("Staying in the arena" do roadmap): o alvo é a parede, a arena
 * limita, e cada um fica onde a ação o deixou.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGBattlePawn : public APawn
{
	GENERATED_BODY()

public:
	AJRPGBattlePawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle Pawn")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle Pawn")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/**
	 * Giro da malha para ela olhar para a frente do ator (+X). Os modelos convertidos do PS1
	 * (Extras/ps1_glb_to_fbx.py) olham para -Y do Unreal: +90 vale para todos eles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn")
	float MeshYawOffset = 90.0f;

	/** Segundos de mistura quando o clip troca (sem Anim Blueprint). 0 = corte seco. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn", meta = (ClampMin = "0.0"))
	float AnimBlendTime = 0.2f;

	/** Quem ele é na luta. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const FJRPGCombatant& GetCombatant() const { return Combatant; }

	/**
	 * Põe o modelo e o combatente, com os pés em FeetTransform (o lugar da arena). Chamado pelo
	 * palco logo depois do spawn.
	 */
	void InitCombatant(const FJRPGCombatant& InCombatant, const FJRPGBattleVisual& Visual, const FTransform& FeetTransform);

	/** Toca um clip, misturando com o atual por AnimBlendTime (sem Anim Blueprint). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void PlayClip(UAnimSequence* Clip, bool bLoop = true, float PlayRate = 1.0f);

	/** Volta ao clip Idle do visual. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void PlayIdle();

	/** O modelo está posto (Blueprint: efeitos de entrada, nome em cima da cabeça...). */
	UFUNCTION(BlueprintImplementableEvent, Category = "JRPG|Battle")
	void OnCombatantInitialized();

	// --- A luta (apresentação) ----------------------------------------------------------------

	/** A arena que limita onde ele pode andar (a AssembleBattle põe). */
	void SetArena(AJRPGBattleArena* InArena) { Arena = InArena; }

	/**
	 * Começa a ação desta timeline: vira para o alvo, anda até o alcance dele (LeadFrames) e depois toca o clip de
	 * cada passo no quadro lógico em que ele começa. Quem chama é o UJRPGBattleSceneSubsystem, no ActionStart.
	 */
	void BeginAction(const FJRPGActionTimeline& Timeline, const AJRPGBattlePawn* Target);

	/** O quadro lógico atual da luta: anda a aproximação, entra no passo que começou, fecha a ação. */
	void UpdatePlayback(int32 LogicFrame);

	/** O ActionIndex da ação em curso; -1 se não há. */
	int32 GetActiveActionIndex() const { return bActionActive ? Action.ActionIndex : INDEX_NONE; }

	/**
	 * A luta cortou a ação em curso (o último inimigo caiu no meio da combo): adota a linha do tempo nova, sem os
	 * passos que não valem mais. Não faz nada se a ação em curso não é a desta timeline ou se nada mudou.
	 */
	void SyncActionTimeline(const FJRPGActionTimeline& Current);

	/** A ação acabou (o evento ActionEnd): fecha o que estiver em curso e volta ao idle, sem esperar o TotalFrames da cópia. */
	void EndAction();

	/**
	 * Mostra um evento que as regras já resolveram. bAsTarget: este pawn é quem sofre (Damage, Miss, Block) ou quem
	 * caiu (Death); senão é quem age (APChanged, ArtRecognized, ArtLearned). Toca a reação e solta o Text em cima da cabeça.
	 */
	void ShowEvent(const FJRPGBattleEvent& Event, const FString& Text, bool bAsTarget);

	/** A fase da ação em curso: mora no pawn, em compasso com a timeline lógica. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	EJRPGActionPhase GetPhase() const { return Phase; }

	/** Caiu e fica no chão até o fim da luta. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	bool IsDown() const { return bDown; }

	/** A fase mudou (Blueprint: câmera, som, efeito). */
	UFUNCTION(BlueprintImplementableEvent, Category = "JRPG|Battle")
	void OnActionPhaseChanged(EJRPGActionPhase NewPhase);

	/** Um evento foi mostrado neste pawn (Blueprint: o número do dano, o aviso da Art). */
	UFUNCTION(BlueprintImplementableEvent, Category = "JRPG|Battle")
	void OnBattleEventShown(const FJRPGBattleEvent& Event, const FString& Text);

	/** A distância, em cm, que ele para do alvo para bater: o alvo é a parede (a lei de alcance do original). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn", meta = (ClampMin = "0.0"))
	float ReachDistance = 220.0f;

	/** Quanto tempo o texto de um evento fica em cima da cabeça, em segundos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn", meta = (ClampMin = "0.0"))
	float EventTextSeconds = 1.4f;

	/** Os textos soltos em cima da cabeça (DrawDebugString: só em builds com debug). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn")
	bool bShowEventText = true;

private:
	/** Cria as partes e os acessórios do visual (e apaga os de um InitCombatant anterior). */
	void BuildPieces(const FJRPGBattleVisual& Visual);

	void SetPhase(EJRPGActionPhase NewPhase);

	/** Toca o clip do passo na velocidade do jogo dele. */
	void PlayStep(const FJRPGActionStep& Step);

	/** Um clip de reação / pose do papel (Idle, Walk, Flinch1, Flinch2, Knockdown, Block, Dying...) e o play rate dele. */
	UAnimSequence* FindRoleClip(FName Role, float& OutPlayRate) const;

	/** Toca uma reação de uma vez e volta ao idle quando acabar (a menos que tenha caído). */
	void PlayReaction(FName Role, bool bHold);
	void ReturnToIdle();

	/** A linha do DT_EnemyActions com este índice de ação (nula em herói ou sem tabela). */
	const FJRPGEnemyActionData* FindEnemyAction(int32 ActionIndex) const;
	const FJRPGEnemyActionData* FindEnemyActionByTag(int32 Tag) const;

	FVector ClampToArena(const FVector& From, const FVector& To) const;

	/** As partes do herói (Leader Pose na Mesh). */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Battle Pawn")
	TArray<TObjectPtr<USkeletalMeshComponent>> PieceComponents;

	/** Os acessórios, presos aos bones da Mesh. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Battle Pawn")
	TArray<TObjectPtr<UStaticMeshComponent>> AttachmentComponents;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleClip;

	UPROPERTY(Transient)
	FJRPGCombatant Combatant;

	bool bUsesAnimClass = false;

	TWeakObjectPtr<AJRPGBattleArena> Arena;
	TWeakObjectPtr<const AJRPGBattlePawn> TargetPawn;

	/** As ações do monstro (DT_EnemyActions) e o conjunto de clips dele (DT_Enemies), lidos no InitCombatant. */
	TArray<FJRPGEnemyActionData> EnemyActions;
	FJRPGMonsterAnimSet EnemyAnims;

	EJRPGActionPhase Phase = EJRPGActionPhase::Idle;
	FJRPGActionTimeline Action;
	bool bActionActive = false;
	int32 PlayedStep = -1;
	int32 StrikesPlayed = 0;
	/** Fecha a ação em curso (a ação acabou ou foi cortada): fica onde está e volta ao idle. */
	void FinishAction();

	FVector ApproachFrom = FVector::ZeroVector;
	FVector ApproachTo = FVector::ZeroVector;
	bool bDown = false;
	FTimerHandle ReactionTimer;
};
