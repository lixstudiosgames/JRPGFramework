#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "World/EventFlag.h"
#include "World/RevivalTreeState.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "WorldStateSubsystem.generated.h"

/**
 * UWorldStateSubsystem
 * Subsistema baseado em GameInstance para persistência temporária do estado do mundo.
 * Gerencia as flags de eventos de história, estado dos baús de tesouro e a purificação das Revival Trees.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UWorldStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UWorldStateSubsystem();

	// UGameInstanceSubsystem overrides
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- EVENT FLAGS ---
	
	/**
	 * Define o estado (booleano) de uma flag de evento de história ou diálogo.
	 * Cria a flag caso ela não exista.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|World|Flags")
	void SetEventFlag(FName FlagID, bool bValue, FString Metadata = TEXT(""));

	/**
	 * Retorna o valor de uma flag de evento. Retorna false caso a flag não exista.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|World|Flags")
	bool GetEventFlagValue(FName FlagID) const;

	/**
	 * Verifica se uma flag de evento já foi registrada no sistema.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|World|Flags")
	bool HasEventFlag(FName FlagID) const;

	/**
	 * Retorna todas as flags registradas (útil para sistemas de save e debug).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|World|Flags")
	TArray<FEventFlag> GetAllEventFlags() const;

	// --- CHESTS ---

	/**
	 * Registra o estado de abertura de um baú de tesouro.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|World|Chests")
	void SetChestOpened(FName ChestID, bool bOpened);

	/**
	 * Verifica se um baú específico já foi aberto.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|World|Chests")
	bool IsChestOpened(FName ChestID) const;

	/**
	 * Retorna a lista de IDs de todos os baús de tesouro abertos no mundo.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|World|Chests")
	TArray<FName> GetOpenedChests() const;

	// --- REVIVAL (GENESIS) TREES ---

	/**
	 * Define o estado atual de uma Revival Tree.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|World|Trees")
	void SetRevivalTreeState(FName TreeID, ERevivalTreeStage Stage);

	/**
	 * Busca o estado de purificação de uma Revival Tree específica. Retorna true se encontrar.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|World|Trees")
	bool GetRevivalTreeState(FName TreeID, FRevivalTreeState& OutState) const;

	/**
	 * Retorna os estados de purificação de todas as Revival Trees.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|World|Trees")
	TArray<FRevivalTreeState> GetAllRevivalTreeStates() const;

	// --- CONTEXTO DE GAMEPLAY ---

	/**
	 * Marca se o jogador está no Field (mapa explorável) — chamar na transição
	 * de level: true ao entrar num mapa de campo, false em batalha, cutscene,
	 * main menu, etc.
	 *
	 * O menu de pausa usa isto para liberar ou travar a opção Save: fora do
	 * Field o item aparece apagado e o C++ recusa abrir a tela de save.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|World")
	void SetIsInField(bool bValue);

	/** True se o jogador está num mapa de Field (ver SetIsInField). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|World")
	bool IsInField() const { return bIsInField; }

	// --- GLOBAL CONTROL ---

	/**
	 * Reseta todo o estado do mundo para os padrões (útil ao iniciar um New Game).
	 * Zera também bIsInField: quem carrega o mapa é que declara o contexto.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|World")
	void ResetWorldState();

	/**
	 * Sobrescreve todo o estado do mundo com um estado carregado de um save.
	 * Operação atômica: os três containers são esvaziados e reconstruídos.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|World")
	void LoadWorldState(const TArray<FEventFlag>& InEventFlags,
	                    const TArray<FName>& InOpenedChests,
	                    const TArray<FRevivalTreeState>& InRevivalTrees);

private:
	UPROPERTY()
	TMap<FName, FEventFlag> EventFlags;

	UPROPERTY()
	TSet<FName> OpenedChests;

	UPROPERTY()
	TMap<FName, FRevivalTreeState> RevivalTreeStates;

	/** Ver SetIsInField. Não é serializado no save: quem carrega o mapa declara. */
	bool bIsInField = false;
};
