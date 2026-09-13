#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Shop/ShopData.h"
#include "ShopSubsystem.generated.h"

class UDataTable;

/**
 * UShopSubsystem
 * Lógica de compra e venda das lojas (DT_Shops).
 *
 * Regras:
 * - Itens em FeaturedItems só aparecem se o jogador tiver o FeaturedGateItemID
 *   (default: platinum_card) no inventário.
 * - Disponibilidade da loja é uma condição por event flag (RequiredFlagID) —
 *   variantes before/after mist são duas linhas com flags opostas; o BP do NPC
 *   decide qual abrir via IsShopAvailable.
 * - Venda: preço = BuyPrice/2; BuyPrice=0, Key items e Art Books nunca vendem.
 * - Gold via UCoreSubsystem; itens via UInventorySubsystem.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UShopSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ============================================================
	// CONFIGURAÇÃO
	// ============================================================

	/** Define manualmente a DataTable de lojas em runtime. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Shop")
	void SetShopDataTable(UDataTable* NewTable);

	/** Item que libera os itens featured das lojas (default: platinum_card). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Shop")
	FName FeaturedGateItemID = TEXT("platinum_card");

	// ============================================================
	// CONSULTA
	// ============================================================

	/**
	 * A loja está disponível? (RequiredFlagID do WorldState == bRequiredFlagValue;
	 * None = sempre). Use no BP do NPC para escolher a variante before/after.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Shop")
	bool IsShopAvailable(FName ShopID) const;

	/**
	 * Estoque resolvido da loja: nome/cidade + itens com preço/descrição/possuídos.
	 * Itens featured são OMITIDOS se o jogador não tiver o item-gate.
	 * Retorna false se a loja não existir ou estiver indisponível.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Shop")
	bool GetShopStock(FName ShopID, FText& OutShopName, FString& OutTown,
	                  TArray<FShopStockEntry>& OutStock) const;

	/** Preço de venda de um item (BuyPrice/2; 0 = não vendável). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Shop")
	int32 GetSellPrice(FName ItemID) const;

	/** O item pode ser vendido? (BuyPrice > 0 e não é Key nem Art Book). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Shop")
	bool CanSellItem(FName ItemID) const;

	/** Inventário do jogador filtrado para a aba de venda (com preços de venda). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Shop")
	TArray<FShopStockEntry> GetSellableInventory() const;

	// ============================================================
	// TRANSAÇÕES
	// ============================================================

	/**
	 * Compra Quantity (1..99) do item na loja indicada. Valida que o item está no
	 * estoque RESOLVIDO (gates aplicados), cobra Gold (falha sem alterar nada se
	 * insuficiente) e adiciona ao inventário.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Shop")
	bool BuyItem(FName ShopID, FName ItemID, int32 Quantity = 1);

	/**
	 * Vende Quantity do item do inventário por BuyPrice/2 cada.
	 * Falha se o item não for vendável ou não houver quantidade.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Shop")
	bool SellItem(FName ItemID, int32 Quantity = 1);

private:
	const FShopData* FindShopRow(FName ShopID) const;
	bool IsRowAvailable(const FShopData& Shop) const;

	UPROPERTY()
	TObjectPtr<UDataTable> ShopDataTable;
};
