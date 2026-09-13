#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Inventory/ItemData.h"
#include "Engine/DataTable.h"
#include "InventorySubsystem.generated.h"

// Delegate disparado quando um item é usado com sucesso
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemUsedSignature, const FItemData&, ItemData, FName, TargetCharacter);

/**
 * UInventorySubsystem
 * Gerencia o inventário do jogador, incluindo armazenamento, consumo, regras de empilhamento,
 * e roteamento de efeitos para outros subsistemas.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UInventorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UInventorySubsystem();

	// GameInstanceSubsystem overrides
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- INVENTORY MANAGEMENT ---

	/**
	 * Adiciona itens ao inventário.
	 * Respeita a regra de limite de stack (99) para consumíveis/iscas,
	 * e adiciona slots separados (com quantidade 1) para armas, armaduras, acessórios, livros de arte e chaves.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory")
	bool AddItem(FName ItemID, int32 Quantity = 1);

	/**
	 * Remove itens do inventário.
	 * Retorna false se o jogador não tiver a quantidade necessária.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory")
	bool RemoveItem(FName ItemID, int32 Quantity = 1);

	/**
	 * Verifica se o jogador possui a quantidade necessária de um item específico.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory")
	bool HasItem(FName ItemID, int32 Quantity = 1) const;

	/**
	 * Retorna a quantidade total de um item específico no inventário.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory")
	int32 GetItemQuantity(FName ItemID) const;

	/**
	 * Retorna todos os slots ativos do inventário (ID do Item e quantidade).
	 * Ideal para ler e salvar no sistema de Save/Load do jogo.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory")
	TArray<FInventorySlot> GetInventorySlots() const { return InventorySlots; }

	/**
	 * Filtra e retorna os slots do inventário que pertencem a uma categoria específica.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory")
	TArray<FInventorySlot> GetInventorySlotsByCategory(EItemCategory Category) const;

	/**
	 * Busca as propriedades e dados do item na DataTable.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory")
	bool GetItemData(FName ItemID, FItemData& OutItemData) const;

	/**
	 * Limpa completamente o inventário do jogador.
	 */
	/**
	 * Este item pode ser jogado fora?
	 *
	 * NAO para item de missao (Key) nem Art Book: o primeiro travaria a
	 * historia, o segundo apagaria uma Art para sempre. Mesma dupla que o
	 * ShopSubsystem::CanSellItem ja recusa.
	 *
	 * A regra mora AQUI, nao na UI — a tela e so mais um caminho ate ela.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory")
	bool CanDiscardItem(FName ItemID) const;

	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory")
	void ResetInventory();

	/**
	 * Sobrescreve todo o inventário atual com um estado carregado (Save/Load).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory")
	void LoadInventoryState(const TArray<FInventorySlot>& SavedSlots);

	// --- USE LOGIC ---

	/**
	 * Verifica se um item pode ser usado de acordo com o contexto atual do jogo (campo/batalha).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory")
	bool CanUseItem(FName ItemID, bool bInBattle) const;

	/**
	 * Consome um item e executa o efeito direcionando-o para o subsistema específico C++ (ou BP via Evento).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory")
	bool UseItem(FName ItemID, FName TargetCharacter, bool bInBattle);

	// --- BLUEPRINT DROPDOWN WRAPPERS ---

	/**
	 * Versão do AddItem otimizada para Blueprint que permite escolher o item por dropdown diretamente no nó.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory", meta = (DisplayName = "Add Item (Row)"))
	bool AddItemRow(FDataTableRowHandle ItemRow, int32 Quantity = 1);

	/**
	 * Versão do RemoveItem otimizada para Blueprint que permite escolher o item por dropdown diretamente no nó.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory", meta = (DisplayName = "Remove Item (Row)"))
	bool RemoveItemRow(FDataTableRowHandle ItemRow, int32 Quantity = 1);

	/**
	 * Versão do HasItem otimizada para Blueprint que permite escolher o item por dropdown diretamente no nó.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory", meta = (DisplayName = "Has Item (Row)"))
	bool HasItemRow(FDataTableRowHandle ItemRow, int32 Quantity = 1) const;

	/**
	 * Versão do GetItemQuantity otimizada para Blueprint que permite escolher o item por dropdown diretamente no nó.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Inventory", meta = (DisplayName = "Get Item Quantity (Row)"))
	int32 GetItemQuantityRow(FDataTableRowHandle ItemRow) const;

	/**
	 * Versão do UseItem otimizada para Blueprint que permite escolher o item por dropdown diretamente no nó.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory", meta = (DisplayName = "Use Item (Row)"))
	bool UseItemRow(FDataTableRowHandle ItemRow, FName TargetCharacter, bool bInBattle);

	// --- CONFIGURATION ---

	/**
	 * Define manualmente a DataTable de itens em tempo de execução.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Inventory")
	void SetItemDataTable(UDataTable* NewTable);

	// --- DELEGATES ---

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Inventory")
	FOnItemUsedSignature OnItemUsed;

protected:
	/**
	 * Busca se o item correspondente à categoria é empilhável.
	 */
	bool IsCategoryStackable(EItemCategory Category) const;

private:
	UPROPERTY()
	TArray<FInventorySlot> InventorySlots;

	UPROPERTY()
	UDataTable* ItemDataTable;

	/** So cresce; carimba AcquiredOrder a cada AddItem. Ver FInventorySlot. */
	UPROPERTY()
	int32 AcquisitionCounter = 0;
};
