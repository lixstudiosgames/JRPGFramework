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
 * volta para o lugar depois do turno ("Staying in the arena" do roadmap; o RE confirma: "Where an action leaves
 * its combatants", battle-action.md — não há perna de volta nem recuo de recuperação): o alvo é a parede, a arena
 * limita, e cada um fica onde a ação o deixou. O que move quem apanha é o golpe: o defensor vira para quem bateu e
 * recua um pouco (ReactToHit), e o atacante o acompanha no começo de cada passo (o "reach snap" do original).
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

	/**
	 * Este pawn apanhou de Attacker (um Damage ou um Block): vira para ele e recua um pouco, para longe dele, limitado pela
	 * arena. LogicFrame é o quadro lógico do evento (o recuo desliza em KnockbackFrames quadros lógicos).
	 */
	void ReactToHit(const AJRPGBattlePawn* Attacker, int32 LogicFrame, bool bKnockdown, bool bBlocked);

	/** Onde ele vai parar: o fim do deslize em curso (recuo, acompanhar o alvo), senão onde está. */
	FVector GetSettledLocation() const { return bSliding ? SlideTo : GetActorLocation(); }

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

	/**
	 * A distância fixa, em cm, que ele para do alvo; 0 (o padrão) = a lei de alcance do original, por tamanho: o limiar de
	 * FUN_8004E2F0 ({256, 384, 1024}[classe] se a classe de tamanho é menor que 3, senão classe << 4, em unidades do PS1),
	 * a classe sendo a do alvo para quem é herói e a do próprio atacante para o monstro (FJRPGCombatant::SizeClass), e o
	 * herói somando o desvio dele ({+43, 0, -53, -100} do Vahn, Noa, Gala e Terra; positivo encurta o alcance).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "0.0"))
	float ReachDistance = 0.0f;

	/** Centímetros por unidade do PS1 (a escala calibrada dos modelos: 0.433005, a mesma na party e nos monstros). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "0.0"))
	float UnitsToCm = 0.433005f;

	/** A classe de tamanho quando o monstro não a traz (SizeClass 0, linha criada à mão): a do meio do disco (14..48). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "1"))
	int32 DefaultSizeClass = 22;

	/** Quanto ele para antes do limiar de alcance, em cm: o original anda até estar dentro, então termina um passo dentro. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "0.0"))
	float StopMargin = 8.0f;

	/** Menos que isto a andar, em cm, e ele nem anda (vai direto para o lugar): nada de passo no lugar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "0.0"))
	float MinWalkDistance = 40.0f;

	/** Quadros lógicos que o atacante leva para fechar a distância de novo no começo de cada passo (o reach snap do original é de um quadro). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "1"))
	int32 FollowFrames = 6;

	/**
	 * Quanto o defensor recua por golpe, em cm. O original move quem apanha pela velocidade de raiz do clip de reação
	 * (entrada +0x0C, negativa) e pelo passo do fim do clip (+0x0E): nos heróis, o flinch recua 105..159 unidades do PS1
	 * (45..69 cm: Vahn -159/-115, Noa -130/-105), no Gala e nos monstros pela velocidade (-4 por vsync). Não há dado por
	 * clip aqui; estes são valores médios, ajustáveis. O knockdown (velocidade -4..-12) recua mais, e o block quase nada.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Knockback", meta = (ClampMin = "0.0"))
	float FlinchKnockback = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Knockback", meta = (ClampMin = "0.0"))
	float KnockdownKnockback = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Knockback", meta = (ClampMin = "0.0"))
	float BlockKnockback = 20.0f;

	/** Em quantos quadros lógicos o recuo desliza (12 = 0,2 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Knockback", meta = (ClampMin = "1"))
	int32 KnockbackFrames = 12;

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
	UAnimSequence* FindRoleClip(FName ClipRole, float& OutPlayRate) const;

	/**
	 * Toca uma reação de uma vez. bHold: fica no último quadro. Com FollowRole, ao acabar toca esse papel (bHoldFollow:
	 * e fica nele): o knockdown encadeia o levantar (Knockdown -> GetUp -> Idle), o herói que cai encadeia o Dying e o Down.
	 * Sem nada depois, volta ao idle (a menos que tenha caído).
	 */
	void PlayReaction(FName ClipRole, bool bHold, FName FollowRole = NAME_None, bool bHoldFollow = false);
	/** O timer da reação acabou: o papel seguinte da cadeia, ou o idle. */
	void OnReactionEnded();
	void ReturnToIdle();

	/** Desliza até To em Frames quadros lógicos, a partir do quadro dado (o recuo do defensor, o acompanhar do atacante). */
	void StartSlide(const FVector& To, int32 LogicFrame, int32 Frames);
	void UpdateSlide(int32 LogicFrame);

	/** O alcance, em cm, entre este pawn e o alvo, pela lei de alcance do original (ou o ReachDistance fixo). */
	float ReachFor(const AJRPGBattlePawn* Target) const;

	/** Onde parar para bater no alvo, a partir de From: From mesmo se já está ao alcance, limitado pela arena. */
	FVector ComputeStopPoint(const FVector& From, const AJRPGBattlePawn* Target) const;

	/** No começo de cada passo: vira para o alvo e fecha a distância de novo (o alvo recuou com o golpe anterior). */
	void FollowTarget(int32 LogicFrame);

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
	/** A aproximação (os LeadFrames) já foi fechada no lugar final. */
	bool bApproachSettled = true;
	bool bDown = false;
	FTimerHandle ReactionTimer;

	/** O papel que toca depois da reação em curso (a cadeia Knockdown -> GetUp, Dying -> Down). */
	FName PendingFollowRole = NAME_None;
	bool bPendingFollowHold = false;

	/** O deslize em curso (recuo ou acompanhar o alvo), em quadros lógicos. */
	bool bSliding = false;
	FVector SlideFrom = FVector::ZeroVector;
	FVector SlideTo = FVector::ZeroVector;
	int32 SlideStartFrame = 0;
	int32 SlideFrames = 1;
};
