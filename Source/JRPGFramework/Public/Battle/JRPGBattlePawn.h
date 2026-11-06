#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Battle/BattleTypes.h"
#include "Battle/EnemyActionData.h"
#include "Battle/EnemyData.h"
#include "Battle/HeroBattleData.h"
#include "JRPGBattlePawn.generated.h"

class AJRPGBattleArena;
class UCapsuleComponent;
class UAudioComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;
class USoundBase;

/**
 * Um som de clip da luta (o PlaySound da engine, ou o UJRPGAnimNotify_HitSound) a decidir pelo pawn: perto de um golpe, só toca
 * se ele conectou. Os campos são os do notify; o som segue como o notify manda (preso à malha / ao bone, ou solto no ponto).
 */
USTRUCT()
struct FJRPGBattleSoundSpec
{
	GENERATED_BODY()

	/** O som do impacto (o Sound do PlaySound). */
	UPROPERTY()
	TObjectPtr<USoundBase> HitSound;

	/** O som de quando o golpe erra; nulo = nenhum (o pawn pode ter o dele, AJRPGBattlePawn::MissSound). */
	UPROPERTY()
	TObjectPtr<USoundBase> MissSound;

	UPROPERTY()
	float VolumeMultiplier = 1.0f;

	UPROPERTY()
	float PitchMultiplier = 1.0f;

	UPROPERTY()
	bool bFollow = false;

	UPROPERTY()
	FName AttachName;

	/**
	 * Verdadeiro no UJRPGAnimNotify_HitSound: é o som do golpe mais próximo do passo, sem janela. Falso no PlaySound da engine: só é
	 * do golpe se cai na janela dele (AJRPGBattlePawn::HitSoundLeadFrames / HitSoundLagFrames); fora dela (voz, passo) toca na hora.
	 */
	UPROPERTY()
	bool bExplicitHitSound = false;
};

/** Um som de golpe à espera do resultado dele (Damage / Block / Miss), até o quadro ExpireFrame. */
USTRUCT()
struct FJRPGHeldBattleSound
{
	GENERATED_BODY()

	UPROPERTY()
	FJRPGBattleSoundSpec Spec;

	/** O índice do golpe em FJRPGActionTimeline::Hits. */
	UPROPERTY()
	int32 HitIndex = INDEX_NONE;

	/** O quadro lógico em que desiste (o golpe nunca foi resolvido): o som é descartado. */
	UPROPERTY()
	int32 ExpireFrame = 0;
};

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

	/**
	 * Segundos de mistura quando um LOOP entra (o idle, o andar, o caído; sem Anim Blueprint). 0 = corte seco. Os clips de uma vez só
	 * (golpes, reações) sempre entram com corte seco. O original não tem camada de mistura: o decodificador só interpola o último
	 * intervalo de quadros do clip até o quadro 0 do próximo (4 / rate vsyncs na velocidade 8: 2 a 4 quadros lógicos, uns 0,05 s).
	 * O padrão (0,2) é mais macio que o original; o passo do fim do clip anda durante esta mistura (SettleIntoIdle).
	 */
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
	void SetArena(AJRPGBattleArena* InArena);

	/**
	 * Começa a ação desta timeline: vira para o alvo, anda até o alcance dele (LeadFrames) e depois toca o clip de
	 * cada passo no quadro lógico em que ele começa. Quem chama é o UJRPGBattleSceneSubsystem, no ActionStart.
	 */
	void BeginAction(const FJRPGActionTimeline& Timeline, AJRPGBattlePawn* Target);

	/**
	 * Quantos quadros a caminhada da ação que acabou de começar passa dos LeadFrames das regras (0 se cabe neles). O pawn anda em
	 * velocidade constante (WalkSpeedUnits), então um alvo longe leva mais que os LeadFrames: a cena segura o resolvedor esse tanto
	 * (UBattleSubsystem::StallResolver) e os passos da ação já vêm deslocados aqui.
	 */
	int32 GetApproachStallFrames() const { return ApproachStallFrames; }

	/** Quantos quadros lógicos a caminhada da ação que começou leva (0 se não anda): o termo de distância das regras. */
	int32 GetApproachFrames() const { return ApproachFrames; }

	/** A ação em curso começou com uma caminhada até o alvo (ainda não fechada). */
	bool IsWalkingApproach() const { return bActionActive && !bApproachSettled; }

	/**
	 * A câmera lenta das Arts que este pawn (o que age) pede para o resto da luta, na escala do original (8 normal): 0 congela todos
	 * durante o starter de Super / Miracle (só quem dá o dash anda), a velocidade do passo (4, ou 2) durante o clip de uma Art, e 8
	 * de novo assim que o clip da Art acaba (FUN_801E93C8). Fora de ação, ou com bSlowWorldDuringArts desligado, 8.
	 */
	int32 GetWorldSpeedRequest(int32 LogicFrame) const;

	/**
	 * A velocidade de animação do resto da luta para este pawn quando ele não está agindo (0..8; a cena chama a cada quadro lógico):
	 * o idle, a reação, o caído tocam nela (GlobalAnimRateScale da malha), e os prazos da reação e a velocidade de raiz andam no
	 * mesmo tempo. Quem age toca os próprios passos na velocidade deles.
	 */
	void SetWorldAnimSpeed(int32 Speed);

	/** Outra ação começou: o recuo dentro do alcance que este pawn seguia fazendo depois da ação dele acaba. */
	void EndPostActionSpacing() { bPostActionSpacing = false; }

	/**
	 * O alcance, em cm, entre este pawn e o alvo, pela lei de alcance do original (ou o ReachDistance fixo), medido da posição
	 * deste ao corpo do alvo (GetBodyLocation).
	 */
	float ReachFor(const AJRPGBattlePawn* Target) const;

	/** O quadro lógico atual da luta: anda a aproximação, entra no passo que começou, fecha a ação. */
	void UpdatePlayback(int32 LogicFrame);

	/** O ActionIndex da ação em curso; -1 se não há. */
	int32 GetActiveActionIndex() const { return bActionActive ? Action.ActionIndex : INDEX_NONE; }

	/**
	 * A luta cortou a ação em curso (o último inimigo caiu no meio da combo): adota a linha do tempo nova, sem os
	 * passos que não valem mais. Não faz nada se a ação em curso não é a desta timeline ou se nada mudou.
	 */
	void SyncActionTimeline(const FJRPGActionTimeline& Current);

	/**
	 * A ação acabou (o evento ActionEnd, no quadro lógico LogicFrame): fecha o que estiver em curso e passa ao idle sem esperar o
	 * TotalFrames da cópia. Se o último clip ainda tem o que tocar (até MaxIdleHandoffWait quadros), o idle entra quando ele acaba
	 * (o original nunca corta o último clip); a diferença entre o fim da ação e o fim real do clip sai no log.
	 */
	void EndAction(int32 LogicFrame);

	/**
	 * O resultado de um golpe DESTE pawn (Damage, Block ou Miss; o evento do atacante): solta os sons de impacto que esperavam por
	 * ele. Quem chama é o UJRPGBattleSceneSubsystem, em todo golpe resolvido.
	 */
	void NoteHitResult(const FJRPGBattleEvent& Event);

	/**
	 * Um som de clip (AnimNotify): o do impacto de um golpe só toca se ele conectou (Damage, Block); no Miss é descartado (ou
	 * toca o Spec.MissSound / MissSound, se houver). Um som que não é de nenhum golpe (voz, passo, fora da janela, reação, idle)
	 * toca na hora. Se o notify dispara antes do resultado do golpe, o som espera por ele (HeldSounds); se depois, decide na hora.
	 */
	void HandleClipSound(const FJRPGBattleSoundSpec& Spec);

	/** Toca um som como o AnimNotify_PlaySound da engine toca (preso à malha ou ao bone, ou solto no ponto da malha). */
	static void PlayNotifySound(USkeletalMeshComponent* MeshComp, USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier, bool bFollow, FName AttachName);

	/** PlayNotifySound que devolve o som tocando (nulo se não tocou). */
	static UAudioComponent* SpawnNotifySound(USkeletalMeshComponent* MeshComp, USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier, bool bFollow, FName AttachName);

	/**
	 * Num clip em loop (o andar, o idle), o mesmo som deste pawn só toca de novo depois que o anterior acabou (uma voz por som,
	 * como no PS1). Sem isto um ciclo mais curto que o som empilha instâncias (o andar do Slime). Os sons de clips de uma vez só
	 * (golpes, reações) tocam sempre.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Sound")
	bool bLoopSoundsWaitToFinish = true;

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

	/**
	 * Onde o CORPO está no chão (o "body pair" +0x3C / +0x40 do original): a posição do ator mais o centro da pose atual (a média
	 * das peças), no plano do chão. É o que o original mede de fora: o lado do alvo na lei de alcance, os dois lados da separação, a
	 * mira do atacante e a virada do defensor. Um clip que leva o corpo para a frente (uma investida, um knockdown) muda isto sem
	 * mexer no ator. Com bUseBodyPair desligado, é a posição do ator.
	 */
	FVector GetBodyLocation() const { return GetActorLocation() + BodyOffset(); }

	/** GetBodyLocation no fim do deslize em curso (o corpo de agora, levado para onde o ator vai parar). */
	FVector GetSettledBodyLocation() const { return GetSettledLocation() + BodyOffset(); }

	// --- A formação (a cena move todo mundo, como o original) ----------------------------------

	/**
	 * Translada o pawn por Delta (no plano do chão) junto com tudo que ele guarda em posição de mundo (a caminhada, o deslize):
	 * o recentrar da formação e o empurrão da separação se somam ao que ele já estiver fazendo.
	 */
	void TranslateBy(const FVector& Delta);

	/**
	 * Move só o destino da caminhada (o lugar de bater, calculado a partir do alvo). Quando a formação é comprimida, cada um anda
	 * um tanto: o destino tem de andar o que o ALVO andou, não o que o atacante andou (TranslateBy já somou o do atacante).
	 */
	void TranslateApproachGoal(const FVector& Delta);

	/** O alvo da ação em curso (nulo fora de ação). */
	AJRPGBattlePawn* GetActionTarget() const { return bActionActive ? TargetPawn.Get() : nullptr; }

	/**
	 * As regras ficaram paradas Frames quadros (PvP: à espera da aproximação): os passos da ação andam o mesmo tanto, para o golpe
	 * não tocar antes de o resolvedor resolver. A caminhada não muda (ela tem o relógio dela).
	 */
	void DelayAction(int32 Frames);

	/** Sai da linha do ataque (o make-way do original): desliza Offset em Frames quadros lógicos, limitado pela arena. */
	void StepAside(const FVector& Offset, int32 LogicFrame, int32 Frames);

	/**
	 * Empurrado por quem chegou para bater (o arrival shove do original): anda Offset de uma vez, no plano, limitado pela arena.
	 * Quem está na ação dele não é empurrado.
	 */
	void ShoveBy(const FVector& Offset);

	/** Está em pé na luta (vivo, à vista): entra na separação, no make-way e no recentrar. */
	bool IsStanding() const;

	/** O raio da separação do original, em cm: herói 0x280 unidades (a Terra 0x400), monstro classe de tamanho << 5. */
	float SeparationRadiusCm() const;

	/** A classe de tamanho deste combatente (a padrão se o monstro não a traz); 0 num herói. */
	int32 GetSizeClass() const;

	/** cm por unidade do PS1 (UnitsToCm). */
	float GetUnitsToCm() const { return UnitsToCm; }

	/** A fase da ação em curso: mora no pawn, em compasso com a timeline lógica. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	EJRPGActionPhase GetPhase() const { return Phase; }

	/** Caiu e fica no chão até o fim da luta. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	bool IsDown() const { return bDown; }

	// --- O que a câmera da luta lê ---------------------------------------------------------------

	/** A timeline da ação em curso (nula fora de ação). */
	const FJRPGActionTimeline* GetActiveTimeline() const { return bActionActive ? &Action : nullptr; }

	/** Derrotado nesta luta (HP 0): o herói caído, o monstro caindo ou desbotando. */
	bool IsDefeated() const { return bDown || bDeathFadeQueued || bDeathFading; }

	/** No knockdown, no levantar ou caído (o clip do original é o knockdown / get-up dele): a câmera do fim da ação fecha nele. */
	bool IsOnKnockdown() const;

	/** Derrotado e o corpo ainda caindo (o clip da queda ainda toca; depois ele segura o último quadro ou repete o caído). */
	bool IsFallingDead() const { return IsDefeated() && ReactionEndFrame != INDEX_NONE; }

	/** Tocando algo que não é o idle: uma ação ou uma reação. */
	bool IsAnimating() const { return bActionActive || ReactionEndFrame != INDEX_NONE; }

	/** A altura do modelo em cm (a "display height" +0x3E do original), medida da malha no InitCombatant. */
	float GetDisplayHeightCm() const { return DisplayHeightCm; }

	/**
	 * O passo da ação que toca agora e o cursor do clip dele: quantos segundos do asset já tocaram e quanto o asset dura (o
	 * cursor de animação +0x22C[+0x68] do original, que a câmera de cada Art lê). Falso fora de ação ou antes do primeiro passo.
	 */
	bool GetPlayingStep(FJRPGActionStep& OutStep, float& OutClipSeconds, float& OutClipLength) const;

	/**
	 * O cursor real do clip do passo que toca (segundos do asset, com as voltas do trecho de laço) e as voltas que faltam; falso se o
	 * clip não é o que a animação toca agora. A câmera própria das Arts lê o contador do dash daqui.
	 */
	bool GetPlayingClipCursor(float& OutSeconds, int32& OutLoopsLeft, bool& bOutHasLoopWindow) const;

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

	/**
	 * A velocidade da caminhada do monstro, em unidades do PS1 por quadro lógico (vsync), quando a linha de andar (tag 1) do
	 * DT_EnemyActions não traz a RootSpeed dela. O original anda em velocidade constante até a lei de alcance dizer "ao alcance";
	 * os heróis têm a do disco (Vahn 42, Noa 44, Gala 28, Terra 48).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "1.0"))
	float EnemyWalkSpeedUnits = 36.0f;

	/**
	 * O movimento de cada clip, do disco (RootSpeed / EndStep do DT_EnemyActions, DT_HeroReactions e DT_ArtHits): a velocidade de
	 * raiz move o ator enquanto o clip toca (um knockdown desliza a queda inteira, um block recua) e o passo do fim anda o que o clip
	 * deixa o corpo de onde começou (o Slash Kick termina 534 unidades à frente). Substitui o recuo fixo (FlinchKnockback...) e o
	 * passo pelo centro da pose, que ficam só para os clips sem dado. Desligado: o comportamento antigo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Knockback")
	bool bUseClipMotion = true;

	/**
	 * Para os clips sem dado de movimento (as tabelas sem as colunas novas): na volta ao idle, o deslocamento do corpo em relação à
	 * pose do idle vira posição do ator (a heurística antiga do passo do fim do clip).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Knockback")
	bool bApplyEndStep = true;

	/**
	 * O arrival shove do original (só na cadeia de caminhada dos seis monstros com a entrada de tag 0x20: Caruban x3, Evil Shadow,
	 * Shade, Nightmare): quando a caminhada até o alvo fecha, o alvo é empurrado até a borda do alcance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach")
	bool bApplyArrivalShove = true;

	/**
	 * Medir o corpo (GetBodyLocation: ator + centro da pose), como o original, no alcance, na separação, na mira e na virada do
	 * defensor. Desligado: mede a posição do ator (os pés), o comportamento antigo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach")
	bool bUseBodyPair = true;

	/** Abaixo disto, em cm, o deslocamento do fim do clip é ignorado (o idle entra com a mistura normal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Knockback", meta = (ClampMin = "0.0"))
	float EndStepMinDistance = 3.0f;

	/**
	 * Quadros lógicos do reach snap no começo de cada passo: o atacante vai até a borda do alcance do corpo do alvo, para a frente
	 * se está longe, para trás se está perto demais. O original faz isso num quadro só (0 = igual, um corte); o padrão desliza.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "0"))
	int32 FollowFrames = 6;

	/**
	 * O recuo do original dentro do alcance (0x80047C3C..0x80047D1C): enquanto o atacante age e o corpo do alvo está mais perto
	 * que o limiar de alcance, ele anda para trás isto em unidades do PS1 por quadro lógico (4 no original, na velocidade 8; a
	 * câmera lenta das Arts reduz junto), até a borda. É o que impede um atacante de terminar dentro do alvo. 0 desliga.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach", meta = (ClampMin = "0.0"))
	float InRangeBackoffUnits = 4.0f;

	/**
	 * Depois da caminhada, o atacante mira o corpo do alvo a cada quadro (o original nos estados 0x1E e 0x1F, 0x801E36F0): um
	 * knockdown que leva o corpo de lado vira o atacante junto. Desligado: só vira no começo de cada passo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Reach")
	bool bFaceTargetEveryFrame = true;

	/**
	 * A câmera lenta das Arts para todos (o byte de velocidade +0x21D do original, em cada ator): numa Art a luta inteira anda a 4
	 * (metade), no starter de Super / Miracle todo mundo congela menos quem dá o dash, e tudo volta a 8 quando o clip da Art acaba.
	 * Desligado: só quem age desacelera, e a reação do alvo toca na velocidade do golpe que a causou (o comportamento antigo).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Animation")
	bool bSlowWorldDuringArts = true;

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

	/**
	 * A janela do som do impacto é assimétrica, em quadros lógicos em volta do quadro de cada golpe: um PlaySound do clip de ataque
	 * dentro dela é "o som do impacto" e espera o resultado do golpe (no Miss, não toca). Um PlaySound fora da janela (a voz, o
	 * passo, o assobio do início) toca sempre. Este é o ANTES do golpe (o assobio posto uns quadros antes do impacto não deve ser
	 * engolido no Miss: padrão curto); o DEPOIS é HitSoundLagFrames. Os dois em 0 desligam a regra: todo PlaySound toca como no
	 * notify da engine. Os sons de impacto que forem postos nos clips daqui em diante devem usar o UJRPGAnimNotify_HitSound, que
	 * não depende de janela.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Sound", meta = (ClampMin = "0"))
	int32 HitSoundLeadFrames = 2;

	/** O DEPOIS da janela do som do impacto (ver HitSoundLeadFrames): os clips do original põem o som uns quadros depois do golpe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Sound", meta = (ClampMin = "0"))
	int32 HitSoundLagFrames = 8;

	/** O som de um golpe que ERRA (pela altura do alvo), no lugar do impacto. Nulo (o padrão) = silêncio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Sound")
	TObjectPtr<USoundBase> MissSound;

	/**
	 * Quantos quadros lógicos o idle espera pelo fim real do último clip da ação quando a ação (os dados) acaba antes dele. O
	 * original nunca corta o último clip (o estado 0x20 espera ele voltar ao idle); o teto só protege de um clip errado e longo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Idle", meta = (ClampMin = "0"))
	int32 MaxIdleHandoffWait = 600;

	/**
	 * O fade de derrota do monstro (lanes 0x100 -> 0 em ~32 vsyncs): o nome do parâmetro escalar do material, de 1 (inteiro) a 0
	 * (preto). Só tem efeito nos materiais que têm o parâmetro; os que não têm ficam como estão e o corpo só afunda e some. None
	 * desliga o parâmetro.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Death")
	FName DeathFadeParameter = TEXT("JRPGDeathFade");

	/** O monstro derrotado afunda durante o fade, como no original ((classe de tamanho * passo) >> 2 unidades por passo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Death")
	bool bDeathSink = true;

	/**
	 * O som da morte do monstro: quando ele começa a desbotar e afundar, os sons do clip de knockdown tocam de novo, todos de uma vez.
	 * No original, o braço de morte do commit (FUN_8004AD80, 0x8004B094..0x8004B6A0) reinstala a entrada do knockdown parada no
	 * último quadro, e todo commit zera o cursor da trilha de sons (+0x1F6, 0x8004AFD0): no quadro seguinte o andador da trilha
	 * (FUN_800508DC) dispara toda pista que o quadro já passou, ou seja, a trilha inteira (o grito e o baque). O roteador só pula o
	 * som igual ao último, e o primeiro da trilha não é o último dela. Leitura do código, sem captura. Desligado: no teste o usuário
	 * ouviu que o som do original é outro (um som de "sugar para o chão", ainda não achado; o DeathFadeSound o toca quando for).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Death")
	bool bReplayKnockdownSoundsOnDeath = false;

	/**
	 * O som do monstro derrotado sendo "sugado" para o chão: toca quando ele começa a desbotar e afundar. O do original é a linha
	 * 0x0D da tabela estática de sons (o som 0x0E, banco de luta): achado de ouvido pelo usuário (o RE não registra quem o toca).
	 * O padrão é o SW_Battle_Cue0E do projeto Legaia; vazio = nenhum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Death")
	TSoftObjectPtr<USoundBase> DeathFadeSound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/Legaia/Battle/Sounds/Common/SW_Battle_Cue0E.SW_Battle_Cue0E")));

	/**
	 * O fantasma perto da câmera (FUN_8004DC68 do original; quem decide é o UJRPGBattleSceneSubsystem): o parâmetro escalar do
	 * material que leva a opacidade do corpo, 1 = inteiro. No M_PS1Monster ele entra, junto do JRPGDeathFade, num dither da máscara
	 * de opacidade. None desliga.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Camera")
	FName CameraGhostParameter = TEXT("JRPGCameraGhost");

	/**
	 * O clarão do alvo do cursor (FUN_801DA6B4 do original: o monstro apontado leva a cor 0x200 por canal, o dobro do neutro, com o
	 * peso cheio: a textura dele fica duas vezes mais clara): o parâmetro escalar do material, 0 = normal. No M_PS1Monster ele soma
	 * a cor do corpo à emissão. Quem decide é o UJRPGBattleSceneSubsystem. None desliga.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn|Camera")
	FName TargetHighlightParameter = TEXT("JRPGHighlight");

	/** O clarão do alvo do cursor (0 = normal); só mexe nos materiais quando muda. */
	void SetTargetHighlight(float Strength);

	/**
	 * A opacidade do fantasma perto da câmera (1 = inteiro); só mexe nos materiais quando muda. Com GhostMaterial (translúcido, com
	 * os parâmetros BaseTexture e Opacity), o corpo troca para ele enquanto estiver abaixo de 1 e volta aos materiais dele depois:
	 * dá para ver através. Sem, a opacidade vai no CameraGhostParameter (o dither do M_PS1Monster).
	 */
	void SetCameraGhost(float Opacity, UMaterialInterface* GhostMaterial = nullptr);

	/**
	 * Entre dois quadros lógicos: o deslize (o passo do fim de um clip na volta ao idle, o reach snap) anda no tempo do mundo, que é
	 * o relógio da mistura das poses. Só quadro a quadro lógico (60 Hz, mais grosso ainda com o slow das Arts), o corpo andava em
	 * degraus enquanto a pose misturava liso: a tremidinha depois do flinch, que o motion blur realça.
	 */
	virtual void Tick(float DeltaSeconds) override;

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
	/** FindRoleClip sem as trocas do original (o flinch que falta pelo outro, o knockdown que falta pelo flinch). */
	UAnimSequence* FindRoleClipExact(FName ClipRole, float& OutPlayRate) const;

	/** O que o último clip de uma cadeia de reações faz ao acabar. */
	enum class EReactionEnd : uint8
	{
		/** Volta ao idle (a menos que tenha caído). */
		Idle,
		/** Fica no último quadro. */
		Hold,
		/** Repete para sempre (o herói caído: a entrada 8 do original re-comita a si mesma). */
		Loop
	};

	/** Um papel da cadeia de uma reação: o clip e o que fazer quando ele é o último e acaba. */
	struct FReactionStep
	{
		FName Role;
		EReactionEnd End = EReactionEnd::Idle;
	};

	/**
	 * Começa uma cadeia de reações no quadro lógico LogicFrame, na escala de velocidade GameSpeed (a em que o golpe foi tomado):
	 * o knockdown encadeia o levantar (Knockdown -> GetUp -> Idle), o herói que morre encadeia Knockdown -> Dying -> Down (em loop).
	 * Cada clip dura o que o clip dura, contado em quadros lógicos (não em timer de relógio): a pausa e os soluços do quadro valem
	 * para ela como para o resolvedor. Um papel sem clip é pulado.
	 */
	void StartReaction(const TArray<FReactionStep>& Steps, int32 LogicFrame, int32 GameSpeed);
	/** Toca o próximo papel da cadeia a partir do quadro dado; no fim dela aplica o End do último. */
	void AdvanceReaction(int32 LogicFrame);
	/** O quadro lógico do fim do clip chegou: o papel seguinte, ou o fim da cadeia. */
	void OnReactionEnded(int32 LogicFrame);
	/** Volta ao idle com o passo do fim do clip (nada, se caído ou no meio da própria ação). */
	void ReturnToIdle();

	/** PlayClip com o tempo de mistura dado (0 = corte seco). */
	void PlayClipBlended(UAnimSequence* Clip, bool bLoop, float PlayRate, float BlendTime);

	/**
	 * Entra no idle aplicando o passo do fim do clip que acabou (EndStepShift): o ator anda o passo durante a mistura para o idle,
	 * e o corpo fica onde o clip o deixou. Sem passo, é o PlayIdle de sempre.
	 */
	void SettleIntoIdle();

	/** O movimento de um clip, do disco (+0x0C velocidade de raiz por vsync, +0x0E passo do fim; unidades do PS1). */
	struct FClipMotion
	{
		int32 RootSpeed = 0;
		int32 EndStep = 0;
		/** Há dado para este clip (senão vale a heurística antiga: o passo pelo centro da pose, o recuo fixo). */
		bool bValid = false;
	};

	/** O movimento do clip de uma reação (DT_EnemyActions pela tag; DT_HeroReactions no herói). */
	FClipMotion RoleMotion(FName ClipRole) const;

	/** O movimento do clip de um passo da ação (a linha do monstro, a linha da Art no DT_ArtHits; golpe simples e starter: 0 / 0). */
	FClipMotion StepMotion(const FJRPGActionStep& Step) const;

	/**
	 * O clip que começa no quadro StartFrame e acaba (natural) em EndFrame passa a mover o ator (UpdateRootMotion, EndStepShift).
	 * bOwnSpeed: um passo da ação, que toca na velocidade dele (GameSpeed) até o fim, mesmo depois de a ação acabar; uma reação
	 * (false) anda na velocidade do mundo.
	 */
	void BeginClipMotion(const FClipMotion& Motion, int32 StartFrame, int32 EndFrame, int32 GameSpeed, bool bOwnSpeed);

	/** O passo do fim do clip em curso foi andado: não anda de novo (um clip seguinte que falta não o repete). */
	void ConsumeClipEndStep() { ActiveMotion.EndStep = 0; }

	/** A velocidade de raiz do clip em curso, quadro a quadro (negativa sempre; positiva só com o alvo fora do alcance). */
	void UpdateRootMotion(int32 LogicFrame);

	/**
	 * O passo do fim do clip em curso, em mundo, no plano do chão: EndStep * Fraction ao longo da direção (Fraction < 1 num corte, a
	 * parte tocada). Sem dado do clip e com bHeuristic: o deslocamento do corpo em relação à pose do idle (a heurística antiga,
	 * bApplyEndStep, só na volta ao idle).
	 */
	FVector EndStepShift(float Fraction, bool bHeuristic = false) const;

	/** A fração do clip em curso que já tocou no quadro dado (1 no fim natural ou depois). */
	float ClipMotionFraction() const;

	/**
	 * Anda Shift (limitado pela arena) junto com a troca de clip: BlendTime 0 (o clip que entra com corte seco, todo clip de uma vez
	 * só) move no mesmo instante; senão desliza durante a mistura (a volta ao idle, BlendTime = AnimBlendTime).
	 */
	void SlideBy(const FVector& Shift, int32 LogicFrame, float BlendTime);

	/** O centro das posições dos bones (menos a raiz) na pose atual, no espaço do componente. */
	FVector PoseCentroid() const;

	/** Do ator ao corpo, no plano do chão (o centro da pose em mundo menos a posição do ator); zero sem pose ou sem bUseBodyPair. */
	FVector BodyOffset() const;

	/** A velocidade da caminhada em cm por quadro lógico (a do disco nos heróis, EnemyWalkSpeedUnits nos monstros). */
	float WalkSpeedCm() const;

	/** O fade e o afundar do monstro derrotado, depois do knockdown. */
	void BeginDeathFade(int32 LogicFrame);
	void UpdateDeathFade(int32 LogicFrame);

	/** Toca o som decidido (o do impacto, ou o do Miss), ou nada. */
	void ResolveSound(const FJRPGBattleSoundSpec& Spec, EJRPGBattleEventType Result) const;
	/**
	 * O índice do golpe do passo em curso a que o som pertence (o mais próximo do quadro atual, dentro da janela assimétrica
	 * HitSoundLeadFrames / HitSoundLagFrames); INDEX_NONE se nenhum. Vale também na cauda da ação que acabou (bSoundContextActive).
	 */
	int32 FindHitForSound(const FJRPGBattleSoundSpec& Spec) const;
	void UpdateHeldSounds(int32 LogicFrame);

	/** Desliza até To em Frames quadros lógicos, a partir do quadro dado (o recuo do defensor, o acompanhar do atacante). */
	void StartSlide(const FVector& To, int32 LogicFrame, int32 Frames);
	void UpdateSlide(int32 LogicFrame);

	/**
	 * O arrival shove do original (estado 0x16, 0x801E33EC..0x801E3490): fechada a caminhada, o alvo é empurrado ao longo da
	 * direção do atacante até a borda do alcance (o laço roda enquanto a lei de alcance diz "dentro"). Só depois de uma caminhada:
	 * quem já começa ao alcance pula o estado 0x16 (e o empurrão) direto para 0x1E.
	 */
	void ArrivalShove();

	/** Onde parar para bater no alvo, a partir de From: From mesmo se já está ao alcance, limitado pela arena. */
	FVector ComputeStopPoint(const FVector& From, const AJRPGBattlePawn* Target) const;

	/** No começo de cada passo: vira para o alvo e vai até a borda do alcance, para a frente ou para trás (o reach snap). */
	void FollowTarget(int32 LogicFrame);

	/**
	 * O ponto do reach snap a partir de From: na linha até o corpo do alvo, exatamente no limiar de alcance (ReachFor +
	 * StopMargin), venha de longe ou de perto demais; limitado pela arena.
	 */
	FVector ComputeSnapPoint(const FVector& From, const AJRPGBattlePawn* Target) const;

	/** A cada quadro da ação, depois da caminhada: mira o corpo do alvo e recua o que estiver dentro do alcance (InRangeBackoffUnits). */
	void UpdateStrikeSpacing();

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
	TWeakObjectPtr<AJRPGBattlePawn> TargetPawn;

	/** As ações do monstro (DT_EnemyActions) e o conjunto de clips dele (DT_Enemies), lidos no InitCombatant. */
	TArray<FJRPGEnemyActionData> EnemyActions;
	FJRPGMonsterAnimSet EnemyAnims;

	EJRPGActionPhase Phase = EJRPGActionPhase::Idle;
	FJRPGActionTimeline Action;
	bool bActionActive = false;
	int32 PlayedStep = -1;
	int32 StrikesPlayed = 0;
	/** Fecha a ação em curso (a ação acabou ou foi cortada): fica onde está e passa ao idle. */
	void FinishAction(int32 LogicFrame);

	FVector ApproachFrom = FVector::ZeroVector;
	FVector ApproachTo = FVector::ZeroVector;
	/** A aproximação já foi fechada no lugar final. */
	bool bApproachSettled = true;
	/** A caminhada da ação: o quadro em que começa (o StartFrame original da ação), quantos quadros leva na velocidade constante e
	 *  quanto passa dos LeadFrames (os passos da ação são deslocados disso). */
	int32 ApproachStartFrame = 0;
	int32 ApproachFrames = 0;
	int32 ApproachStallFrames = 0;

	/** O movimento do clip em curso (BeginClipMotion) e onde ele está no relógio lógico. */
	FClipMotion ActiveMotion;
	int32 MotionStartFrame = INDEX_NONE;
	int32 MotionEndFrame = INDEX_NONE;
	int32 MotionGameSpeed = 8;
	/** Até que quadro lógico a velocidade de raiz já foi aplicada. */
	int32 MotionAppliedFrame = INDEX_NONE;
	/** O clip em curso é um passo da ação (toca na velocidade dele) e não uma reação (anda na velocidade do mundo). */
	bool bMotionOwnSpeed = false;
	/** Quanto do clip em curso já tocou, em quadros lógicos do clip (na velocidade em que começou), e quanto ele dura. */
	double MotionProgress = 0.0;
	int32 MotionDuration = 0;

	/**
	 * Depois da própria ação: o recuo dentro do alcance continua (o último clip pode terminar com um passo para dentro do alvo, o
	 * Slash Kick anda 534 unidades), como o original faz no done band. Acaba quando outra ação começa ou ele apanha.
	 */
	bool bPostActionSpacing = false;

	/** A velocidade do resto da luta para este pawn (SetWorldAnimSpeed), 0..8, e o atraso acumulado do tempo local dele (< 1 quadro). */
	int32 WorldAnimSpeed = 8;
	double WorldTimeLag = 0.0;

	/** O tempo local anda a WorldAnimSpeed / 8: cada quadro lógico inteiro de atraso empurra os prazos da reação e do clip em 1. */
	void AdvanceWorldTime();

	/** A linha do herói no DT_HeroReactions (o movimento das reações); só vale com bHasHeroReactionRow. */
	FJRPGHeroReactionData HeroReactionRow;
	bool bHasHeroReactionRow = false;
	/** O DT_EnemyActions importado traz o movimento (alguma linha do monstro com RootSpeed ou EndStep); senão, a heurística antiga. */
	bool bEnemyHasMotionData = false;

	/** A pose do idle medida (o centro dos bones, espaço do componente): a referência do passo do fim do clip sem dado. */
	FVector NeutralCentroid = FVector::ZeroVector;
	bool bHasNeutralCentroid = false;
	/** O idle toca desde este quadro lógico (INDEX_NONE = outro clip toca). */
	int32 IdleSinceFrame = INDEX_NONE;
	/** Quadros lógicos vistos com o idle tocando (a pose só é medida depois de a mistura de entrada acabar). */
	int32 IdleFramesSeen = 0;
	bool bDown = false;

	/** O último quadro lógico que o pawn viu (UpdatePlayback / BeginAction): a hora dos sons de clip e do log. */
	int32 LastLogicFrame = 0;

	/** A cadeia da reação em curso: os papéis que faltam tocar depois do que toca agora, e o quadro lógico em que o que toca acaba. */
	TArray<FReactionStep> ReactionQueue;
	/** O papel da reação que toca (ou ficou: o knockdown segurado, o caído em loop); None no idle e na ação. */
	FName ReactionRole = NAME_None;
	/** A altura do modelo, em cm (GetDisplayHeightCm). */
	float DisplayHeightCm = 0.0f;
	EReactionEnd ReactionLastEnd = EReactionEnd::Idle;
	int32 ReactionEndFrame = INDEX_NONE;
	int32 ReactionGameSpeed = 8;

	/** O monstro derrotado: o fade (e o afundar) começa quando o knockdown dele acaba. */
	bool bDeathFadeQueued = false;
	/** A opacidade do fantasma perto da câmera posta por último nos materiais. */
	float CameraGhostOpacity = 1.0f;
	/** O clarão do alvo posto por último nos materiais. */
	float TargetHighlight = 0.0f;
	/** O fantasma translúcido: os materiais trocados (componente, slot, o material de antes) e os dinâmicos do fantasma. */
	void BeginGhostMaterials(UMaterialInterface* GhostMaterial);
	void EndGhostMaterials();
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> GhostSavedComponents;
	TArray<int32> GhostSavedSlots;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> GhostSavedMaterials;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostMaterials;
	bool bDeathFading = false;
	int32 DeathFadeFrames = 32;
	int32 DeathFadeStartFrame = 0;
	float DeathFadeBaseZ = 0.0f;

	/** O clip que toca agora é um loop (PlayClipBlended), e o som que cada som de clip em loop deixou tocando. */
	bool bClipLooping = false;
	TMap<TWeakObjectPtr<USoundBase>, TWeakObjectPtr<UAudioComponent>> LoopVoices;

	/** O clip do passo que toca: o quadro lógico em que começou, a velocidade (play rate x RateScale) e a duração do asset. */
	int32 StepClipStartFrame = INDEX_NONE;
	float StepClipSpeed = 1.0f;
	float StepClipLength = 0.0f;

	/** O último clip de golpe tocado: de onde a ação sabe quando ele de fato acaba (o log do idle, a espera do idle). */
	FName StepClipName = NAME_None;
	int32 StepClipEndFrame = INDEX_NONE;
	/** O idle entra neste quadro lógico (o último clip da ação ainda tocava quando a ação acabou); INDEX_NONE = nenhum pendente. */
	int32 IdleHandoffFrame = INDEX_NONE;

	/** O resultado de cada golpe desta ação já resolvido (HitIndex -> Damage / Block / Miss), e os sons que esperam por ele. */
	TMap<int32, EJRPGBattleEventType> HitResults;

	/**
	 * O contexto de som da ação (o passo, os quadros dos golpes e os HitResults) vale: do BeginAction até a cauda da ação que acabou
	 * passar (SoundContextEndFrame). O último clip segue tocando depois do FinishAction (a espera do idle, o crossfade de AnimBlendTime
	 * em que o clip que sai ainda dispara notifies), e o som de impacto dessa cauda tem de obedecer o golpe como os de antes.
	 * Apagado no BeginAction da ação seguinte, na morte e no InitCombatant.
	 */
	bool bSoundContextActive = false;
	int32 SoundContextEndFrame = INDEX_NONE;

	UPROPERTY(Transient)
	TArray<FJRPGHeldBattleSound> HeldSounds;

	/** O deslize em curso (recuo ou acompanhar o alvo), em quadros lógicos. */
	bool bSliding = false;
	FVector SlideFrom = FVector::ZeroVector;
	FVector SlideTo = FVector::ZeroVector;
	int32 SlideStartFrame = 0;
	int32 SlideFrames = 1;
	/** O tempo do mundo (já dilatado) do último quadro lógico: o deslize anda entre um quadro e o próximo no Tick. */
	double LastLogicFrameTime = 0.0;
};
