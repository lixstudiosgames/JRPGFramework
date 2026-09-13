#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Inventory/ItemData.h"
#include "ShopData.generated.h"

/**
 * FShopData
 * Uma loja (linha da DataTable DT_Shops, gerada de Docs/Data/ITENS DATA/shops.toml
 * via Extras/generate_shops_csv.py). O row name é o ShopID usado no OpenShop.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FShopData : public FTableRowBase
{
	GENERATED_BODY()

	/** Nome de exibição da loja ("Variety Shop") ou do lojista ("Morlang"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FText ShopName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FString Town;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FString Merchant;

	/** Keys dos itens à venda, em ordem (resolvem na DT_Items). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	TArray<FName> Inventory;

	/** Subconjunto do Inventory que só aparece se o jogador tiver o item-gate (platinum_card). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	TArray<FName> FeaturedItems;

	/**
	 * Condição de disponibilidade da loja: event flag do WorldStateSubsystem que
	 * precisa valer bRequiredFlagValue. None = loja sempre disponível.
	 * (Ex: variantes before/after mist usam mist_cleared = false/true — troque
	 * pela flag que quiser.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FName RequiredFlagID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	bool bRequiredFlagValue = true;
};

/**
 * FShopStockEntry
 * Um item resolvido do estoque (ou do inventário vendável) — pronto para a UI e BPs.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FShopStockEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FName ItemID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FText Description;

	/**
	 * Linha de stats para a UI: o que o item aumenta e para qual personagem
	 * (ex: "ATK +10 — Vahn", "UDF +8 / LDF +4 — Noa", "Teaches Tornado Flame — Vahn").
	 * Vazio quando a descrição já cobre (consumíveis, waters).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FText StatsLine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	EItemCategory Category = EItemCategory::Consumable;

	/** Preço unitário: compra = BuyPrice; venda = BuyPrice/2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 Price = 0;

	/** True se o item é featured (visível só com o item-gate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	bool bFeatured = false;

	/** Quantidade que o jogador já possui. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 OwnedQuantity = 0;

	// --- Bônus estruturados (espelham o FItemData) ---
	// A StatsLine acima continua sendo o resumo em texto; estes campos são o que
	// a UI da loja usa para calcular o delta por personagem ("ATK 200 ▲ 206").

	/** Bônus de ataque da arma (FItemData::AttackBonus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 AttackBonus = 0;

	/** Upper Defense Factor da armadura (FItemData::UDF). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 UDF = 0;

	/** Lower Defense Factor da armadura (FItemData::LDF). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 LDF = 0;

	/**
	 * Personagem para quem o item rende mais: EquipBest (armas) ou
	 * EquipCharacter (armaduras). Vazio = serve para todos.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FString EquipBest;

	/** Demais personagens que conseguem equipar (FItemData::EquipOthers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	TArray<FString> EquipOthers;
};
