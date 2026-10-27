#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Battle/BattleTypes.h"
#include "UI/WebUISubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "JRPGFieldEnemy.generated.h"

class USoundBase;
class USoundConcurrency;
class UAudioComponent;
class UPathFollowingComponent;

class USphereComponent;
class UBillboardComponent;
class UAnimInstance;
class UAnimSequence;
class USkeletalMesh;
class UJRPGMistDisturberComponent;
class AJRPGEncounterZone;
class AJRPGFieldEnemy;
class AJRPGBattleArena;

/** O que o inimigo do Field está fazendo. */
UENUM(BlueprintType)
enum class EJRPGFieldEnemyState : uint8
{
	/** Parado, esperando para passear de novo. */
	Idle,
	/** Andando até um ponto ao acaso (em volta de casa, ou dentro da zona). */
	Wander,
	/** O jogador entrou no raio: indo atrás dele. */
	Chase,
	/** Desistiu da perseguição: voltando para casa / para a zona. */
	Return,
	/** Encostou no jogador: sumiu e espera o respawn. */
	Hidden
};

/**
 * O que um inimigo do Field mostra no mapa: malha e animação. Ou um Anim Blueprint, ou os
 * clips Idle/Walk/Run trocados pelo próprio inimigo conforme a velocidade (sem AnimBP).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGFieldEnemyVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Opcional. Com ele, os clips abaixo são ignorados. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy")
	TSubclassOf<UAnimInstance> AnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy")
	TSoftObjectPtr<UAnimSequence> WalkAnim;

	/** Perseguindo. Vazio: o WalkAnim mais rápido. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy")
	TSoftObjectPtr<UAnimSequence> RunAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy", meta = (ClampMin = "0.01"))
	float Scale = 1.0f;
};

/** Um inimigo do Field encostou no jogador. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGFieldEncounter
{
	GENERATED_BODY()

	/** Quem entra na luta (sorteado do encounter). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Field|Enemy")
	TArray<FJRPGEncounterCombatant> Enemies;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Field|Enemy")
	TObjectPtr<AJRPGFieldEnemy> FieldEnemy = nullptr;

	/** A zona que criou o inimigo; nulo se ele foi posto direto no mapa. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Field|Enemy")
	TObjectPtr<AJRPGEncounterZone> Zone = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Field|Enemy")
	FVector ContactLocation = FVector::ZeroVector;

	/** Onde a luta vai ser montada (AJRPGBattleArena::FindArenaFor no ContactLocation); nulo se o mapa não tem arena. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Field|Enemy")
	TObjectPtr<AJRPGBattleArena> Arena = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGFieldEncounter, const FJRPGFieldEncounter&, Encounter);

/**
 * AJRPGFieldEnemy
 * O inimigo visível no Field. É SÓ o visual que começa uma luta: quem luta é o Encounter (keys
 * do DT_Enemies), sorteado no toque. Por isso é uma classe só, sem inimigo específico — o
 * BP_FieldEnemy do projeto serve para todos.
 *
 * - Visual: Visuals (sorteia um), senão o modelo do inimigo principal do encounter no DT_Enemies
 *   (Mesh + Anims.Idle/Walk), senão o FallbackVisual. Inimigo do DT ainda sem modelo cai no
 *   fallback e avisa. A cápsula se ajusta à malha.
 * - Comportamento (sem Behavior Tree; decide a cada DecisionInterval):
 *   Idle/Wander em volta de casa (ou dentro da zona) -> Chase quando o jogador entra no
 *   DetectRadius -> Return quando ele passa do LoseRadius por GiveUpTime, ou quando o inimigo
 *   se afasta LeashDistance de casa/da zona. O alvo é o pawn que o jogador controla (acompanha
 *   o R3); os seguidores da party nunca disparam nada.
 * - Toque: sorteia o encounter, pede o UBattleSubsystem::TryStartEncounter, escolhe a arena
 *   (AJRPGBattleArena::FindArenaFor), dispara OnEncounterTriggered/OnContact, some, começa a
 *   luta (UBattleSubsystem::BeginBattle — o AJRPGBattleStage monta a cena) e reaparece depois
 *   do delay. O efeito de tela quebrando entra no OnContact.
 * - Congela com menu aberto (estado da WebUI fora de HudOnly). Durante uma luta, todos os
 *   inimigos do Field somem e congelam; o respawn que cair no meio dela espera ela acabar.
 * - Sem navmesh: anda reto até o alvo.
 *
 * Avulso: ponha no mapa e configure o Encounter. Em zona: o AJRPGEncounterZone cria e cuida
 * dele. Guia: Docs/Guides/FIELD_ENEMIES.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGFieldEnemy : public ACharacter
{
	GENERATED_BODY()

public:
	AJRPGFieldEnemy();

	/** Encostar no jogador (overlap só com Pawn). Segue o tamanho da cápsula. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Enemy")
	TObjectPtr<USphereComponent> ContactSphere;

	/** Abre a névoa da JRPGMist em volta dele e deixa rastro, como o jogador. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Enemy")
	TObjectPtr<UJRPGMistDisturberComponent> MistDisturber;

#if WITH_EDITORONLY_DATA
	/** Só no editor: o DetectRadius desenhado em volta dele. */
	UPROPERTY()
	TObjectPtr<USphereComponent> DetectPreview;

	/** Só no editor: o ícone vermelho em cima dele, para achar no mapa. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif

	// --- Visual ---

	/** Visuais deste inimigo; um é sorteado a cada spawn. Vazio: o modelo vem do encounter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Visual")
	TArray<FJRPGFieldEnemyVisual> Visuals;

	/** Sem Visuals: mostrar o modelo do inimigo principal do encounter (DT_Enemies: Mesh, Anims). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Visual")
	bool bVisualFromEncounter = true;

	/** Quando nada acima tem modelo (inimigo do DT ainda sem Mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Visual")
	FJRPGFieldEnemyVisual FallbackVisual;

	/**
	 * Giro da malha para ela olhar para a frente do ator (+X). Os modelos convertidos do PS1
	 * (Extras/ps1_glb_to_fbx.py) olham para -Y do Unreal: +90 vale para todos eles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Visual")
	float MeshYawOffset = 90.0f;

	/**
	 * Segundos de mistura quando o clip troca (Idle -> Walk -> Run), sem Anim Blueprint: o
	 * UJRPGFieldEnemyAnimInstance faz o crossfade. 0 = corte seco. Visual com AnimClass ignora.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Visual", meta = (ClampMin = "0.0"))
	float AnimBlendTime = 0.2f;

	/** Ajusta a cápsula ao tamanho da malha (e põe os pés da malha no chão). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Visual")
	bool bFitCapsuleToMesh = true;

	// --- Som ---

	/**
	 * Toca os sons dos clips (os AnimNotify_PlaySound que o Extras/ue_import_monster_sounds.py
	 * põe). Os SoundWaves são os da batalha, 2D; aqui no Field eles tocam em 3D, com a
	 * SoundAttenuation abaixo, e um por vez por inimigo (o passo não empilha).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Sound")
	bool bPlayClipSounds = true;

	/** Volume dos sons dos clips no Field (multiplica o do notify). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Sound", meta = (ClampMin = "0.0"))
	float SoundVolume = 0.7f;

	/**
	 * Até onde se ouve: volume cheio no raio interno, cai até sumir no Falloff Distance. A
	 * distância conta do personagem controlado (AJRPGFieldPlayerController::bHearFromPawn).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Sound")
	FSoundAttenuationSettings SoundAttenuation;

	/**
	 * Toca um som de clip no Field: 3D, preso à malha, um por vez. É o que o
	 * UJRPGFieldEnemyAnimInstance faz com cada AnimNotify_PlaySound. Escondido, não toca.
	 */
	void PlayClipSound(USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier, bool bFollow, FName AttachName);

	/** Para os sons que ele está tocando (ao sumir). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Enemy")
	void StopClipSounds();

	// --- Encontro ---

	/** Quem luta quando ele encosta no jogador. Numa zona, vale o encounter da zona. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Encounter")
	FJRPGEncounterSetup Encounter;

	/** Encostar começa a luta. Desligado, o toque só anuncia o encontro (como na Fase 3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Encounter")
	bool bStartBattle = true;

	// --- Detecção e perseguição ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Detection", meta = (ClampMin = "0.0"))
	float DetectRadius = 800.0f;

	/** Maior que o DetectRadius: o jogador na borda não fica ligando e desligando a perseguição. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Detection", meta = (ClampMin = "0.0"))
	float LoseRadius = 1200.0f;

	/** Segundos com o jogador fora do LoseRadius até desistir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Detection", meta = (ClampMin = "0.0"))
	float GiveUpTime = 2.0f;

	/** Até onde ele se afasta de casa (avulso) ou da borda da zona antes de voltar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Detection", meta = (ClampMin = "0.0"))
	float LeashDistance = 2000.0f;

	/** Só detecta o jogador se não houver nada no meio (trace de Visibility). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Detection")
	bool bRequireLineOfSight = false;

	/** Depois de desistir, não detecta de novo por este tempo (conta do início da volta). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Detection", meta = (ClampMin = "0.0"))
	float ReacquireDelay = 1.5f;

	/** Passeando e voltando. No mínimo 400: acima do andar do jogador (300), ele alcança quem só anda. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Movement", meta = (ClampMin = "400.0", UIMin = "400.0"))
	float WalkSpeed = 400.0f;

	/**
	 * Perseguindo. Mais que o andar do jogador (300) e menos que a corrida (700): correndo, dá
	 * para fugir. Nunca abaixo do WalkSpeed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Movement", meta = (ClampMin = "0.0"))
	float ChaseSpeed = 450.0f;

	// --- Passear ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Wander")
	bool bWander = true;

	/** Avulso: raio em volta de casa. Na zona, ele passeia pela zona inteira. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Wander", meta = (ClampMin = "0.0"))
	float WanderRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Wander", meta = (ClampMin = "0.0"))
	float WaitTimeMin = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Wander", meta = (ClampMin = "0.0"))
	float WaitTimeMax = 4.0f;

	// --- Respawn (avulso; na zona quem decide é a zona) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Respawn")
	bool bRespawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Respawn", meta = (ClampMin = "0.0"))
	float RespawnDelayMin = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Respawn", meta = (ClampMin = "0.0"))
	float RespawnDelayMax = 60.0f;

	// --- Debug ---

	/**
	 * Escreve na tela e no log cada acontecimento (spawn, detectou, perdeu, toque, sumiu,
	 * voltou). Inimigo de zona usa o bPrintDebug da zona.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Enemy|Debug")
	bool bPrintDebug = false;

	/** Encostou no jogador e o encontro foi aceito. */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Field|Enemy")
	FOnJRPGFieldEncounter OnEncounterTriggered;

	/** Gancho para o efeito do toque (a tela quebrando, um som). Chamado antes de ele sumir. */
	UFUNCTION(BlueprintNativeEvent, Category = "JRPG|Field|Enemy")
	void OnContact(const FJRPGFieldEncounter& EncounterInfo);

	/** Força o encontro, como se tivesse encostado (dev menu, testes). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Enemy")
	void TriggerEncounter();

	/** Some agora (sem encontro) e agenda o respawn. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Enemy")
	void HideEnemy();

	/** Reaparece com os pés neste ponto do chão, com um visual sorteado de novo. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Enemy")
	void RespawnAtFloor(FVector FloorLocation);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Field|Enemy")
	EJRPGFieldEnemyState GetEnemyState() const { return State; }

	/** O encounter que vale para ele: o da zona, ou o próprio. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Field|Enemy")
	const FJRPGEncounterSetup& GetEncounterSetup() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Field|Enemy")
	AJRPGEncounterZone* GetZone() const { return Zone; }

	/** Chamado pela zona antes do FinishSpawning. */
	void InitFromZone(AJRPGEncounterZone* InZone);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Think();
	void EnterIdle(float WaitSeconds);
	void StartWander();
	void StartChase(APawn* Player);
	void StartReturn(const TCHAR* Why);
	void ScheduleRespawn();

	/** Escolhe e aplica um visual (malha, animação, cápsula). */
	void ApplyRandomVisual();
	/**
	 * Qual visual mostrar. bRandom: sorteia da lista/dos inimigos com modelo (no jogo); sem ele,
	 * o primeiro (prévia no editor, que não pode mudar a cada clique). bQuiet: sem mensagens.
	 */
	bool ResolveVisual(FJRPGFieldEnemyVisual& Out, FString& OutSource, bool bRandom, bool bQuiet) const;
	/** bKeepFeetOnFloor: move o ator para os pés continuarem no chão quando a cápsula muda. */
	void ApplyVisual(const FJRPGFieldEnemyVisual& Visual, const FString& Source, bool bKeepFeetOnFloor);
	void UpdateAnimation();

	APawn* GetPlayerPawn() const;
	bool CanDetect(const APawn* Player) const;
	/** Quanto ele (ou um ponto) está longe de casa / fora da zona. */
	float DistanceFromHome(const FVector& Point) const;
	bool PickWanderPoint(FVector& OutFloor) const;

	/** MoveTo no navmesh; sem navmesh, anda reto (Tick). */
	void MoveToPoint(const FVector& Target);
	void MoveToPawn(APawn* Target);
	void StopMoving();
	bool IsMoveFinished() const;
	/** Perseguindo, desliga a freada de fim de caminho do path following (o fim é o jogador). */
	void HandlePostProcessMove(UPathFollowingComponent* PathFollowing, FVector& MoveInput);

	UFUNCTION()
	void HandleContactOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleUIStateChanged(EJRPGUIState OldState, EJRPGUIState NewState);

	UFUNCTION()
	void HandleBattleStarted(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants);

	UFUNCTION()
	void HandleBattleEnded(EJRPGBattleResult Result);

	/** Congela com menu aberto ou durante uma luta. */
	void UpdateFrozen();
	void SetFrozen(bool bFreeze);

	void Print(const FString& Message, const FColor& Color = FColor::Silver) const;
	FString Describe() const;

	UPROPERTY(Transient)
	TObjectPtr<AJRPGEncounterZone> Zone;

	/** Os clips do visual atual, já carregados. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleClip;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> WalkClip;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> RunClip;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> PlayingClip;

	/** Feitos do SoundAttenuation na primeira vez que toca: o que o SpawnSoundAttached pede. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SoundAttenuationAsset;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> SoundConcurrency;

	TArray<TWeakObjectPtr<UAudioComponent>> ClipSounds;

	EJRPGFieldEnemyState State = EJRPGFieldEnemyState::Idle;
	FString VisualName;
	FVector HomeLocation = FVector::ZeroVector;
	float WaitUntil = 0.0f;
	float StateDeadline = 0.0f;
	float LostTime = 0.0f;
	float LastReturnTime = -100.0f;
	bool bFrozen = false;
	bool bMenuOpen = false;
	bool bInBattle = false;
	bool bUsesAnimClass = false;

	/** Sem navmesh: o alvo do andar reto. */
	bool bDirectMove = false;
	FVector DirectTarget = FVector::ZeroVector;
	TWeakObjectPtr<APawn> DirectPawn;

	FTimerHandle ThinkTimer;
	FTimerHandle RespawnTimer;

	static constexpr float DecisionInterval = 0.1f;
};
