#include "Shop/ShopSubsystem.h"
#include "Core/CoreSubsystem.h"
#include "Inventory/InventorySubsystem.h"
#include "World/WorldStateSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

// Limite de uma compra/venda por transação (espelha o stack máximo do inventário)
static constexpr int32 MaxTransactionQuantity = 99;

// "tornado_flame" -> "Tornado Flame" (nomes técnicos snake_case p/ exibição)
static FString PrettifyKey(const FString& Key)
{
	FString Out;
	bool bNewWord = true;
	for (const TCHAR Ch : Key)
	{
		if (Ch == TEXT('_'))
		{
			Out += TEXT(' ');
			bNewWord = true;
		}
		else
		{
			Out += bNewWord ? FChar::ToUpper(Ch) : Ch;
			bNewWord = false;
		}
	}
	return Out;
}

// Linha de stats do item para o card de detalhes da loja: o que aumenta e
// para qual personagem. Vazio quando a descrição já cobre (consumíveis, waters).
// Copia os bonus estruturados do item para a entrada da loja. A UI usa estes
// campos (e nao a StatsLine em texto) para calcular o delta por personagem.
static void FillEquipBonuses(const FItemData& ItemData, FShopStockEntry& Entry)
{
	Entry.AttackBonus = ItemData.AttackBonus;
	Entry.UDF = ItemData.UDF;
	Entry.LDF = ItemData.LDF;

	// Armas usam EquipBest; armaduras usam EquipCharacter. Vazio = serve a todos.
	Entry.EquipBest = ItemData.EquipBest.IsEmpty() ? ItemData.EquipCharacter : ItemData.EquipBest;
	Entry.EquipOthers = ItemData.EquipOthers;
}

static FText BuildStatsLine(const FItemData& ItemData)
{
	switch (ItemData.Category)
	{
	case EItemCategory::Weapon:
	{
		FString Line = FString::Printf(TEXT("ATK +%d"), ItemData.AttackBonus);
		if (!ItemData.EquipBest.IsEmpty())
		{
			Line += FString::Printf(TEXT(" — %s"), *ItemData.EquipBest);
			for (const FString& Other : ItemData.EquipOthers)
			{
				Line += FString::Printf(TEXT(", %s"), *Other);
			}
		}
		return FText::FromString(Line);
	}
	case EItemCategory::Armor:
	{
		FString Line;
		if (ItemData.UDF != 0)
		{
			Line += FString::Printf(TEXT("UDF +%d"), ItemData.UDF);
		}
		if (ItemData.LDF != 0)
		{
			Line += FString::Printf(TEXT("%sLDF +%d"), Line.IsEmpty() ? TEXT("") : TEXT(" / "), ItemData.LDF);
		}
		if (!ItemData.EquipCharacter.IsEmpty())
		{
			Line += FString::Printf(TEXT(" — %s"), *ItemData.EquipCharacter);
		}
		return FText::FromString(Line);
	}
	case EItemCategory::Accessory:
	{
		FString Line;
		if (!ItemData.EffectClass.IsEmpty() && ItemData.EffectValue != 0)
		{
			Line = FString::Printf(TEXT("%s +%d"), *PrettifyKey(ItemData.EffectClass), ItemData.EffectValue);
		}
		return FText::FromString(Line);
	}
	case EItemCategory::ArtBook:
	{
		FString Line;
		if (!ItemData.TeachesArt.IsNone())
		{
			Line = FString::Printf(TEXT("Teaches %s"), *PrettifyKey(ItemData.TeachesArt.ToString()));
			if (!ItemData.TeachesCharacter.IsEmpty())
			{
				Line += FString::Printf(TEXT(" — %s"), *ItemData.TeachesCharacter);
			}
		}
		return FText::FromString(Line);
	}
	default:
		// Consumíveis/waters/keys: a EffectDescription já diz o que fazem
		return FText::GetEmpty();
	}
}

void UShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Mesmo padrão do InventorySubsystem: pasta do jogo, fallback no plugin
	if (!ShopDataTable)
	{
		ShopDataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/Data/DT_Shops")));
		if (!ShopDataTable)
		{
			ShopDataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/JRPGFramework/Data/DT_Shops")));
		}

		if (ShopDataTable)
		{
			UE_LOG(LogTemp, Log, TEXT("ShopSubsystem: DataTable de lojas carregada: %s"), *ShopDataTable->GetPathName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: Nenhuma DataTable de lojas encontrada (/Game/Data/DT_Shops ou /JRPGFramework/Data/DT_Shops). Defina via SetShopDataTable."));
		}
	}
}

void UShopSubsystem::SetShopDataTable(UDataTable* NewTable)
{
	ShopDataTable = NewTable;
}

// ============================================================
// CONSULTA
// ============================================================

const FShopData* UShopSubsystem::FindShopRow(FName ShopID) const
{
	if (!ShopDataTable || ShopID.IsNone())
	{
		return nullptr;
	}
	return ShopDataTable->FindRow<FShopData>(ShopID, TEXT("ShopSubsystem"), /*bWarnIfRowMissing=*/false);
}

bool UShopSubsystem::IsRowAvailable(const FShopData& Shop) const
{
	if (Shop.RequiredFlagID.IsNone())
	{
		return true;
	}

	UWorldStateSubsystem* WorldState = GetGameInstance()->GetSubsystem<UWorldStateSubsystem>();
	if (!WorldState)
	{
		return false;
	}
	return WorldState->GetEventFlagValue(Shop.RequiredFlagID) == Shop.bRequiredFlagValue;
}

bool UShopSubsystem::IsShopAvailable(FName ShopID) const
{
	const FShopData* Shop = FindShopRow(ShopID);
	return Shop != nullptr && IsRowAvailable(*Shop);
}

bool UShopSubsystem::GetShopStock(FName ShopID, FText& OutShopName, FString& OutTown,
                                  TArray<FShopStockEntry>& OutStock) const
{
	OutStock.Reset();

	const FShopData* Shop = FindShopRow(ShopID);
	if (!Shop)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: Loja '%s' não existe na DT_Shops."), *ShopID.ToString());
		return false;
	}
	if (!IsRowAvailable(*Shop))
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: Loja '%s' indisponível (condição '%s' não satisfeita)."),
			*ShopID.ToString(), *Shop->RequiredFlagID.ToString());
		return false;
	}

	UInventorySubsystem* Inventory = GetGameInstance()->GetSubsystem<UInventorySubsystem>();
	if (!Inventory)
	{
		return false;
	}

	OutShopName = Shop->ShopName;
	OutTown = Shop->Town;

	// Itens featured só aparecem com o item-gate no inventário
	const bool bHasGateItem = Inventory->HasItem(FeaturedGateItemID);

	for (const FName& ItemID : Shop->Inventory)
	{
		const bool bFeatured = Shop->FeaturedItems.Contains(ItemID);
		if (bFeatured && !bHasGateItem)
		{
			continue;
		}

		FItemData ItemData;
		if (!Inventory->GetItemData(ItemID, ItemData))
		{
			UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: Item '%s' da loja '%s' não existe na DT_Items — ignorado."),
				*ItemID.ToString(), *ShopID.ToString());
			continue;
		}

		FShopStockEntry Entry;
		Entry.ItemID = ItemID;
		Entry.DisplayName = ItemData.Name;
		Entry.Description = ItemData.EffectDescription;
		Entry.StatsLine = BuildStatsLine(ItemData);
		Entry.Category = ItemData.Category;
		FillEquipBonuses(ItemData, Entry);
		Entry.Price = ItemData.BuyPrice;
		Entry.bFeatured = bFeatured;
		Entry.OwnedQuantity = Inventory->GetItemQuantity(ItemID);
		OutStock.Add(Entry);
	}

	return true;
}

int32 UShopSubsystem::GetSellPrice(FName ItemID) const
{
	UInventorySubsystem* Inventory = GetGameInstance()->GetSubsystem<UInventorySubsystem>();
	FItemData ItemData;
	if (!Inventory || !Inventory->GetItemData(ItemID, ItemData))
	{
		return 0;
	}
	// Regra global: venda = metade do preço de compra
	return ItemData.BuyPrice / 2;
}

bool UShopSubsystem::CanSellItem(FName ItemID) const
{
	UInventorySubsystem* Inventory = GetGameInstance()->GetSubsystem<UInventorySubsystem>();
	FItemData ItemData;
	if (!Inventory || !Inventory->GetItemData(ItemID, ItemData))
	{
		return false;
	}

	// Key items e Art Books nunca podem ser vendidos; BuyPrice=0 não é vendável
	if (ItemData.Category == EItemCategory::Key || ItemData.Category == EItemCategory::ArtBook)
	{
		return false;
	}
	return ItemData.BuyPrice > 0;
}

TArray<FShopStockEntry> UShopSubsystem::GetSellableInventory() const
{
	TArray<FShopStockEntry> Sellable;

	UInventorySubsystem* Inventory = GetGameInstance()->GetSubsystem<UInventorySubsystem>();
	if (!Inventory)
	{
		return Sellable;
	}

	// AGREGA por ItemID: equipamentos ocupam um slot por unidade no inventário,
	// mas na lista de venda aparecem como uma linha só com a quantidade total
	// (o RemoveItem já remove atravessando múltiplos slots).
	TArray<FName> SeenOrder;
	TMap<FName, int32> Totals;
	for (const FInventorySlot& Slot : Inventory->GetInventorySlots())
	{
		if (Slot.Quantity <= 0 || !CanSellItem(Slot.ItemID))
		{
			continue;
		}
		if (!Totals.Contains(Slot.ItemID))
		{
			SeenOrder.Add(Slot.ItemID);
		}
		Totals.FindOrAdd(Slot.ItemID) += Slot.Quantity;
	}

	for (const FName& ItemID : SeenOrder)
	{
		FItemData ItemData;
		if (!Inventory->GetItemData(ItemID, ItemData))
		{
			continue;
		}

		FShopStockEntry Entry;
		Entry.ItemID = ItemID;
		Entry.DisplayName = ItemData.Name;
		Entry.Description = ItemData.EffectDescription;
		Entry.StatsLine = BuildStatsLine(ItemData);
		Entry.Category = ItemData.Category;
		FillEquipBonuses(ItemData, Entry);
		Entry.Price = ItemData.BuyPrice / 2;
		Entry.OwnedQuantity = Totals[ItemID];
		Sellable.Add(Entry);
	}

	return Sellable;
}

// ============================================================
// TRANSAÇÕES
// ============================================================

bool UShopSubsystem::BuyItem(FName ShopID, FName ItemID, int32 Quantity)
{
	if (Quantity < 1 || Quantity > MaxTransactionQuantity)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: BuyItem — quantidade inválida (%d)."), Quantity);
		return false;
	}

	// Resolve o estoque com os gates aplicados — o índice pode vir do JS,
	// então NUNCA confiar: só compra o que está visível/disponível de fato
	FText ShopName;
	FString Town;
	TArray<FShopStockEntry> Stock;
	if (!GetShopStock(ShopID, ShopName, Town, Stock))
	{
		return false;
	}

	const FShopStockEntry* Entry = Stock.FindByPredicate(
		[&ItemID](const FShopStockEntry& E) { return E.ItemID == ItemID; });
	if (!Entry)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: BuyItem — item '%s' não está à venda na loja '%s'."),
			*ItemID.ToString(), *ShopID.ToString());
		return false;
	}
	if (Entry->Price <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: BuyItem — item '%s' sem preço de compra."), *ItemID.ToString());
		return false;
	}

	UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>();
	UInventorySubsystem* Inventory = GetGameInstance()->GetSubsystem<UInventorySubsystem>();
	if (!Core || !Inventory)
	{
		return false;
	}

	const int32 TotalCost = Entry->Price * Quantity;
	if (!Core->RemoveGold(TotalCost))
	{
		UE_LOG(LogTemp, Log, TEXT("ShopSubsystem: BuyItem — gold insuficiente (%d < %d)."), Core->GetGold(), TotalCost);
		return false;
	}

	if (!Inventory->AddItem(ItemID, Quantity))
	{
		// Falha inesperada ao adicionar: devolve o gold (transação atômica)
		Core->AddGold(TotalCost);
		UE_LOG(LogTemp, Error, TEXT("ShopSubsystem: BuyItem — falha ao adicionar '%s' ao inventário; gold devolvido."),
			*ItemID.ToString());
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("ShopSubsystem: Comprado %dx '%s' por %d gold na loja '%s'."),
		Quantity, *ItemID.ToString(), TotalCost, *ShopID.ToString());
	return true;
}

bool UShopSubsystem::SellItem(FName ItemID, int32 Quantity)
{
	if (Quantity < 1 || Quantity > MaxTransactionQuantity)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: SellItem — quantidade inválida (%d)."), Quantity);
		return false;
	}

	if (!CanSellItem(ItemID))
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSubsystem: SellItem — item '%s' não pode ser vendido."), *ItemID.ToString());
		return false;
	}

	UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>();
	UInventorySubsystem* Inventory = GetGameInstance()->GetSubsystem<UInventorySubsystem>();
	if (!Core || !Inventory)
	{
		return false;
	}

	// RemoveItem valida a quantidade possuída (falha sem alterar nada)
	if (!Inventory->RemoveItem(ItemID, Quantity))
	{
		UE_LOG(LogTemp, Log, TEXT("ShopSubsystem: SellItem — quantidade insuficiente de '%s'."), *ItemID.ToString());
		return false;
	}

	const int32 TotalGain = GetSellPrice(ItemID) * Quantity;
	Core->AddGold(TotalGain);

	UE_LOG(LogTemp, Log, TEXT("ShopSubsystem: Vendido %dx '%s' por %d gold."), Quantity, *ItemID.ToString(), TotalGain);
	return true;
}
