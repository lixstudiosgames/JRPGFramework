#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "Battle/BattleTypes.h"
#include "Camera/CameraData.h"
#include "JRPGBattleStage.generated.h"

class AJRPGBattlePawn;
class AJRPGBattleArena;
class APlayerController;
class UBillboardComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGStageAssembled, const TArray<AJRPGBattlePawn*>&, Pawns);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJRPGStageCleared);

/**
 * AJRPGBattleStage
 * Monta e desmonta a cena da luta por cima do mapa atual. O UBattleSubsystem não conhece ator
 * nenhum: o palco escuta OnBattleStarted / OnBattleEnded e cuida de tudo que é AActor.
 *
 * Começo (debaixo de um fade; a tela quebrando é a Fase 7):
 * - a arena: AJRPGBattleArena::FindArenaFor(Setup.Location); mapa sem arena, uma provisória no
 *   ponto do encontro, virada para Setup.FacingYaw;
 * - esconde e congela a party do Field (o personagem controlado e os seguidores) e o
 *   HideDuringBattle da arena; os inimigos do Field se escondem sozinhos;
 * - tira o controle do Field (AJRPGFieldPlayerController::SetFieldInputEnabled);
 * - cria um AJRPGBattlePawn por combatente nos lugares da arena (GetFormation), com o modelo de
 *   batalha: PartyVisuals para os heróis, o Mesh/Anims.Idle do DT_Enemies para os inimigos;
 * - a câmera: a BattleCamera da arena, senão FrameGroup em todos os combatentes.
 * Fim: destrói os pawns e devolve tudo como estava (visibilidade, controle, câmera).
 *
 * Um por mapa. O UJRPGBattleStageSubsystem cria o da classe de Project Settings > JRPG Battle
 * quando o mapa não tem um posto à mão. Guia: Docs/Guides/BATTLE_STAGE.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGBattleStage : public AActor
{
	GENERATED_BODY()

public:
	AJRPGBattleStage();

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif

	// --- Combatentes --------------------------------------------------------------------------

	/** A classe de cada combatente na cena (um Blueprint filho dá efeitos, nome na cabeça...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Battle Stage")
	TSubclassOf<AJRPGBattlePawn> PawnClass;

	/** O modelo de batalha de cada herói, pela key do DT_Characters ("Vahn"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Battle Stage")
	TMap<FName, FJRPGBattleVisual> PartyVisuals;

	/** Herói fora do PartyVisuals, ou inimigo do DT_Enemies ainda sem Mesh. Vazio: só a cápsula. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Battle Stage")
	FJRPGBattleVisual FallbackVisual;

	// --- Transição ----------------------------------------------------------------------------

	/** Segundos de fade para o preto ao entrar e ao sair (a cena troca no escuro). 0 = corte seco. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Stage|Transition", meta = (ClampMin = "0.0"))
	float TransitionTime = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Stage|Transition")
	FLinearColor TransitionColor = FLinearColor::Black;

	// --- Câmera -------------------------------------------------------------------------------

	/** O enquadramento quando a arena não tem BattleCamera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Stage|Camera")
	ECameraFramingPreset CameraPreset = ECameraFramingPreset::BattleWide;

	/** Folga em volta dos combatentes (1 = encostando nas bordas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Stage|Camera", meta = (ClampMin = "1.0"))
	float CameraPadding = 1.15f;

	/**
	 * A câmera fica atrás da party, olhando para os inimigos. Desligado, do lado dos inimigos,
	 * olhando para a party. (O preset gira em volta do primeiro do grupo; BattleWide olha para
	 * o rosto dele.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Stage|Camera")
	bool bCameraBehindParty = true;

	// --- Debug --------------------------------------------------------------------------------

	/** Escreve na tela e no log a montagem e a desmontagem (arena, lugares, modelos). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Stage|Debug")
	bool bPrintDebug = true;

	// --- Eventos e consulta -------------------------------------------------------------------

	/** A cena está montada (depois do fade de entrada começar a abrir). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Stage")
	FOnJRPGStageAssembled OnStageAssembled;

	/** A cena foi desmontada e o Field voltou. */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Stage")
	FOnJRPGStageCleared OnStageCleared;

	/** O palco do mundo (nulo se o mapa não tem e o subsystem não criou). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Stage", meta = (WorldContext = "WorldContextObject"))
	static AJRPGBattleStage* GetBattleStage(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Stage")
	TArray<AJRPGBattlePawn*> GetPawns() const;

	/** O pawn de um combatente (pelo SlotIndex do FJRPGCombatant). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Stage")
	AJRPGBattlePawn* GetPawnForSlot(int32 SlotIndex) const;

	/** A arena da luta atual (pode ser a provisória). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Stage")
	AJRPGBattleArena* GetArena() const { return Arena.Get(); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Stage")
	bool IsAssembled() const { return Phase == EPhase::Assembled; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EPhase : uint8
	{
		Idle,
		/** Fade de entrada fechando; a cena ainda não existe. */
		Entering,
		Assembled,
		/** Fade de saída fechando; a cena ainda está lá. */
		Leaving
	};

	/** Um ator que o palco escondeu, e como ele estava. */
	struct FHiddenActor
	{
		TWeakObjectPtr<AActor> Actor;
		bool bWasHidden = false;
		bool bHadCollision = true;
		float TimeDilation = 1.0f;
	};

	UFUNCTION()
	void HandleBattleStarted(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants);

	UFUNCTION()
	void HandleBattleEnded(EJRPGBattleResult Result);

	void Assemble();
	void Teardown();

	void ChooseArena();
	void HideFieldActors();
	void HideActor(AActor* Actor, bool bFreeze);
	void RestoreHiddenActors();
	void DisableFieldInput();
	void RestoreFieldInput();
	void FrameCamera();

	bool ResolveVisual(const FJRPGCombatant& Combatant, FJRPGBattleVisual& OutVisual, FString& OutSource) const;
	void Fade(bool bToBlack);
	APlayerController* GetPlayerController() const;
	void Print(const FString& Message, const FColor& Color = FColor::Silver) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AJRPGBattlePawn>> Pawns;

	TWeakObjectPtr<AJRPGBattleArena> Arena;
	/** A arena foi criada pelo palco (mapa sem arena) e morre com a luta. */
	bool bOwnsArena = false;

	FJRPGBattleSetup PendingSetup;
	TArray<FJRPGCombatant> PendingCombatants;

	TArray<FHiddenActor> HiddenActors;

	TWeakObjectPtr<APlayerController> InputController;
	bool bRestoreFieldInput = false;
	bool bRestoreMoveInput = false;

	EPhase Phase = EPhase::Idle;
	FTimerHandle TransitionTimer;
};

/**
 * UJRPGBattleStageSubsystem
 * Garante um AJRPGBattleStage em todo mapa jogado: se o mapa não tem um posto à mão, cria o da
 * classe de Project Settings > JRPG Battle no começo do jogo.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGBattleStageSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
