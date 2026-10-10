#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Camera/CameraData.h"
#include "UI/WebUISubsystem.h"
#include "CameraSubsystem.generated.h"

class AJRPGCameraActor;
class UJRPGCameraShakeModifier;
class UDataTable;
class APlayerController;

/** Disparado quando a câmera passa a focar um ator (PresetID = nome do preset usado). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCameraFocusChangedSignature, AActor*, FocusActor, FName, PresetID);
/** Disparado quando a câmera volta para o player (manual ou auto-restore da UI). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCameraRestoredSignature);
/** Disparado quando um camera shake termina (Custom = shake via params customizados). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCameraShakeFinishedSignature, ECameraShakePreset, Preset);

/**
 * UCameraSubsystem
 * Sistema de câmera completo do framework: foco em personagens (NPC de loja,
 * diálogo), presets de enquadramento prontos, camera shake com variações,
 * registry de alvos e base para o modo batalha (grupo/órbita).
 *
 * Player e NPCs são Blueprints do jogo host — o subsystem só trabalha com
 * AActor* genéricos e resolve o player via PlayerController->GetPawn().
 *
 * Fluxo típico (loja):
 * - BP do NPC: FocusOnActor(self, ShopFront) + WebUI->OpenShop("shop_id")
 * - Jogador fecha a loja → a câmera volta SOZINHA para o player
 *   (auto-restore via OnUIStateChanged do WebUISubsystem; desligue por
 *   chamada com bAutoRestoreOnUIClose = false)
 *
 * Presets:
 * - Built-in por enum (ECameraFramingPreset) — funcionam sem setup nenhum.
 * - Opcional: DataTable de FCameraPresetRow (SetCameraPresetTable). Row com o
 *   nome do enum (ex: "ShopFront") SOBRESCREVE o built-in; rows com outros
 *   nomes são presets novos usáveis via FocusOnActorWithPreset.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UCameraSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ============================================================
	// REGISTRY DE ALVOS (players/NPCs/inimigos registrados pelos BPs)
	// ============================================================

	/**
	 * Registra um ator como alvo de câmera (chame no BeginPlay do BP).
	 * Re-registrar o mesmo TargetID sobrescreve. Referências são weak:
	 * ator destruído some do registry sozinho.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Targets")
	void RegisterCameraTarget(FName TargetID, AActor* TargetActor, ECameraTargetType Type = ECameraTargetType::NPC);

	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Targets")
	void UnregisterCameraTarget(FName TargetID);

	/** O ator registrado com esse ID (nullptr se não existe ou foi destruído). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Camera|Targets")
	AActor* GetCameraTarget(FName TargetID) const;

	/** Todos os alvos vivos do tipo dado (ex: todos os NPCs registrados). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Camera|Targets")
	TArray<AActor*> GetCameraTargetsByType(ECameraTargetType Type) const;

	/**
	 * Varre o mundo registrando atores com a actor tag AutoDetectTag.
	 * TargetID = nome do ator; tipo inferido por tags extras no ator
	 * ("Player"/"Enemy"/"Prop", senão NPC). Roda sozinho a cada mapa novo
	 * se bAutoDetectTargetsByTag = true.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Targets")
	void ScanWorldForCameraTargets();

	/** true = re-escaneia o mundo por tag a cada mapa carregado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera|Targets")
	bool bAutoDetectTargetsByTag = false;

	/** Actor tag procurada pelo ScanWorldForCameraTargets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera|Targets")
	FName AutoDetectTag = TEXT("CameraTarget");

	// ============================================================
	// FOCO (câmera para NPC/personagem — loja, cutscene, etc)
	// ============================================================

	/**
	 * Foca a câmera num ator com um preset built-in. É isto que o BP do NPC
	 * da loja chama antes do OpenShop, escolhendo o tipo de câmera.
	 * @param bAutoRestoreOnUIClose  true = quando a WebUI fechar (voltar a
	 *        HudOnly), a câmera volta sozinha para o player.
	 * @param FadeOutTime  blend da VOLTA para o player no auto-restore
	 *        (0 = corte seco — útil em top-down, onde a câmera viaja lá do alto).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Focus")
	void FocusOnActor(AActor* TargetActor, ECameraFramingPreset Preset = ECameraFramingPreset::Default,
		float BlendTime = 0.75f, bool bAutoRestoreOnUIClose = true, float FadeOutTime = 0.75f);

	/** Igual ao FocusOnActor, mas resolve o ator pelo registry (TargetID). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Focus")
	void FocusOnTarget(FName TargetID, ECameraFramingPreset Preset = ECameraFramingPreset::Default,
		float BlendTime = 0.75f, bool bAutoRestoreOnUIClose = true, float FadeOutTime = 0.75f);

	/**
	 * Foca usando um preset por NOME: row da DataTable de presets, com
	 * fallback para built-in de mesmo nome (ex: "ShopFront").
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Focus")
	void FocusOnActorWithPreset(AActor* TargetActor, FName PresetID,
		float BlendTime = 0.75f, bool bAutoRestoreOnUIClose = true, float FadeOutTime = 0.75f);

	/** Foca com parâmetros totalmente customizados (struct montada no BP). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Focus")
	void FocusWithCustomParams(AActor* TargetActor, const FCameraPresetRow& Params,
		float BlendTime = 0.75f, bool bAutoRestoreOnUIClose = true, float FadeOutTime = 0.75f);

	/**
	 * Foca numa câmera posicionada À MÃO no level (CameraActor/CineCamera que
	 * você colocou onde quis). Fechar a UI volta para o player (auto-restore);
	 * saindo de um JRPGCameraZone a volta é automática também.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Focus")
	void FocusOnLevelCamera(AActor* CameraActor, float BlendTime = 0.75f,
		bool bAutoRestoreOnUIClose = true, float FadeOutTime = 0.75f);

	/** Devolve a câmera para o player (blend de volta para o pawn). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Focus")
	void RestoreToPlayerCamera(float BlendTime = 0.75f);

	/** A câmera gerenciada está ativa (foco/grupo/órbita)? */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Camera|Focus")
	bool IsCameraFocusActive() const { return bFocusActive; }

	/** O ator focado agora (nullptr se nenhum/grupo). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Camera|Focus")
	AActor* GetCurrentFocusActor() const { return CurrentFocusActor.Get(); }

	/** O enquadramento efetivo de um preset built-in (DataTable override já aplicado). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Camera|Focus")
	FCameraPresetRow GetFramingPreset(ECameraFramingPreset Preset) const;

	/**
	 * DataTable de presets (row struct CameraPresetRow). Rows com nome de
	 * enum sobrescrevem os built-in; outros nomes viram presets novos.
	 * Ver Extras/DT_CameraPresets.csv pronto para importar.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Focus")
	void SetCameraPresetTable(UDataTable* PresetTable);

	// ============================================================
	// DIÁLOGO (já engatilhado para o futuro dialogue system)
	// ============================================================

	/**
	 * Começa a câmera de conversa: enquadra o NPC (preset Conversation) e
	 * guarda os dois participantes para o shot-reverse-shot do FocusSpeaker.
	 * @param PlayerOverride  ator do lado do player (default: o pawn atual).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Dialogue")
	void StartConversation(AActor* NPC, AActor* PlayerOverride = nullptr, float BlendTime = 0.6f);

	/**
	 * Shot-reverse-shot: câmera over-shoulder por trás do OUVINTE mirando o
	 * FALANTE. Chame a cada troca de fala do diálogo.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Dialogue")
	void FocusSpeaker(bool bNPCSpeaking, float BlendTime = 0.4f);

	/** Termina a conversa e devolve a câmera para o player. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Dialogue")
	void EndConversation(float BlendTime = 0.6f);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Camera|Dialogue")
	bool IsConversationActive() const { return bConversationActive; }

	// ============================================================
	// BATALHA / GRUPO (base para o battle mode)
	// ============================================================

	/**
	 * Enquadra um grupo de atores (todos cabem na tela). Padding > 1 dá
	 * folga nas bordas. Battle camera: auto-restore desligado por padrão.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Battle")
	void FrameGroup(const TArray<AActor*>& Actors, float BlendTime = 0.75f, float Padding = 1.15f,
		ECameraFramingPreset BasePreset = ECameraFramingPreset::BattleWide, bool bAutoRestoreOnUIClose = false);

	/** Foca e orbita um ator (graus/segundo; negativo inverte o sentido). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Battle")
	void OrbitAroundActor(AActor* TargetActor, float DegreesPerSecond = 20.0f,
		ECameraFramingPreset Preset = ECameraFramingPreset::ThreeQuarter, float BlendTime = 0.75f);

	/** Congela a órbita mantendo o foco (RestoreToPlayerCamera volta ao player). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Battle")
	void StopOrbit();

	// ============================================================
	// CAMERA SHAKE (variações prontas + customizado)
	// ============================================================

	/**
	 * Toca uma variação de shake. Rumble e Handheld são loops — param só com
	 * StopAllCameraShakes. Funciona na câmera do player E na câmera focada.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Shake")
	void PlayCameraShake(ECameraShakePreset Preset, float Scale = 1.0f);

	/** Toca um shake com parâmetros customizados (struct montada no BP). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Shake")
	void PlayCameraShakeCustom(const FJRPGCameraShakeParams& Params, float Scale = 1.0f);

	/** Para todos os shakes: bImmediate = corte seco; false = fade suave. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Camera|Shake")
	void StopAllCameraShakes(bool bImmediate = false);

	/** Os parâmetros built-in de uma variação de shake (para customizar em cima). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Camera|Shake")
	FJRPGCameraShakeParams GetShakePresetParams(ECameraShakePreset Preset) const;

	// ============================================================
	// EVENTOS
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Camera|Events")
	FOnCameraFocusChangedSignature OnCameraFocusChanged;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Camera|Events")
	FOnCameraRestoredSignature OnCameraRestored;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Camera|Events")
	FOnCameraShakeFinishedSignature OnCameraShakeFinished;

	/** Blend usado pelo auto-restore quando a UI fecha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	float DefaultRestoreBlendTime = 0.75f;

	// --- Chamado pelo UJRPGCameraShakeModifier (C++) quando um shake acaba ---
	void NotifyShakeFinished(ECameraShakePreset Preset);

private:
	/** Um alvo registrado (weak — some sozinho quando o ator morre). */
	struct FCameraTargetEntry
	{
		TWeakObjectPtr<AActor> Actor;
		ECameraTargetType Type = ECameraTargetType::NPC;
	};

	/** PlayerController local (idioma padrão dos subsystems do framework). */
	APlayerController* ResolvePlayerController() const;

	/** Garante a câmera gerenciada viva no mundo atual (spawn transient sob demanda). */
	AJRPGCameraActor* EnsureManagedCamera();

	/** Garante o shake modifier no PlayerCameraManager ATUAL (recriado por mapa/PIE). */
	UJRPGCameraShakeModifier* EnsureShakeModifier();

	/** Funil de todos os focos: posiciona a câmera gerenciada e faz o blend. */
	void FocusInternal(AActor* Target, const FCameraPresetRow& Preset, FName PresetID,
		float BlendTime, bool bAutoRestore, float FadeOutTime);

	/** Preset efetivo: row da DataTable com nome do enum ganha do built-in. */
	FCameraPresetRow ResolvePreset(ECameraFramingPreset Preset) const;

	/** Auto-restore: UI voltou a HudOnly com foco ativo e flag ligada. */
	UFUNCTION()
	void HandleUIStateChanged(EJRPGUIState OldState, EJRPGUIState NewState);

	/** Alvo do foco destruído — restaura o player. */
	void HandleFocusTargetLost();

	/** Mapa novo: registry e ponteiros do mundo velho morreram — limpa tudo. */
	void HandlePostLoadMap(UWorld* NewWorld);

	/** DataTable opcional de presets (row struct FCameraPresetRow). */
	UPROPERTY()
	TObjectPtr<UDataTable> CameraPresetTable;

	TMap<FName, FCameraTargetEntry> TargetRegistry;

	// Câmera gerenciada e shake modifier morrem com o mapa — weak de propósito
	TWeakObjectPtr<AJRPGCameraActor> ManagedCamera;
	TWeakObjectPtr<UJRPGCameraShakeModifier> ShakeModifier;

	TWeakObjectPtr<AActor> CurrentFocusActor;

	// Participantes da conversa atual (StartConversation)
	TWeakObjectPtr<AActor> ConversationNPC;
	TWeakObjectPtr<AActor> ConversationPlayer;

	bool bFocusActive = false;
	bool bConversationActive = false;

	/**
	 * Mundo em que o foco atual foi feito. Um foco feito já no mapa novo (o jogador nascendo
	 * dentro de uma JRPGCameraZone durante o carregamento) sobrevive ao HandlePostLoadMap, que
	 * roda no fim do carregamento, depois do BeginPlay dos atores.
	 */
	TWeakObjectPtr<UWorld> FocusWorld;

	// true = quando a WebUI fechar, restaura o player (última chamada de foco ganha)
	bool bAutoRestoreFlag = false;

	// Blend da volta pro player, gravado por cada foco (FadeOutTime; 0 = corte seco)
	float PendingRestoreBlendTime = 0.75f;

	FDelegateHandle PostLoadMapHandle;
};
