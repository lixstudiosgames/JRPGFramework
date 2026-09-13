#include "Inventory/InventorySubsystem.h"
#include "Party/PartySubsystem.h"
#include "Status/StatusSubsystem.h"
#include "Arts/ArtsSubsystem.h"
#include "Battle/BattleSubsystem.h"
#include "Engine/GameInstance.h"
#include "Math/UnrealMathUtility.h"

UInventorySubsystem::UInventorySubsystem()
	: ItemDataTable(nullptr)
{
}

void UInventorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Tenta carregar a DataTable padrão se não estiver definida
	if (!ItemDataTable)
	{
		// 1. Tenta carregar da pasta do jogo (/Game/Data/DT_Items)
		ItemDataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/Data/DT_Items")));
		
		if (!ItemDataTable)
		{
			// 2. Fallback para a pasta de conteúdo padrão do plugin (/JRPGFramework/Data/DT_Items)
			ItemDataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/JRPGFramework/Data/DT_Items")));
		}

		if (ItemDataTable)
		{
			UE_LOG(LogTemp, Log, TEXT("InventorySubsystem: DataTable de itens carregada com sucesso: %s"), *ItemDataTable->GetPathName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("InventorySubsystem: Nenhuma DataTable de itens encontrada nos caminhos padrão (/Game/Data/DT_Items ou /JRPGFramework/Data/DT_Items). Defina manualmente via SetItemDataTable."));
		}
	}
}

void UInventorySubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool UInventorySubsystem::AddItem(FName ItemID, int32 Quantity)
{
	if (ItemID.IsNone() || Quantity <= 0)
	{
		return false;
	}

	FItemData ItemData;
	if (!GetItemData(ItemID, ItemData))
	{
		UE_LOG(LogTemp, Warning, TEXT("InventorySubsystem::AddItem: Item com ID %s nao existe na DataTable."), *ItemID.ToString());
		return false;
	}

	bool bStackable = IsCategoryStackable(ItemData.Category);

	if (bStackable)
	{
		int32 Remaining = Quantity;
		
		// Tenta preencher slots existentes
		for (FInventorySlot& Slot : InventorySlots)
		{
			if (Slot.ItemID == ItemID && Slot.Quantity < 99)
			{
				int32 SpaceLeft = 99 - Slot.Quantity;
				int32 ToAdd = FMath::Min(Remaining, SpaceLeft);
				Slot.Quantity += ToAdd;
				Remaining -= ToAdd;

				// Reempilhar TAMBEM conta como coletar agora.
				Slot.AcquiredOrder = ++AcquisitionCounter;

				if (Remaining <= 0)
				{
					return true;
				}
			}
		}

		// Cria novos slots para o restante
		while (Remaining > 0)
		{
			int32 ToAdd = FMath::Min(Remaining, 99);
			InventorySlots.Add(FInventorySlot(ItemID, ToAdd));
			InventorySlots.Last().AcquiredOrder = ++AcquisitionCounter;
			Remaining -= ToAdd;
		}
	}
	else
	{
		// Não empilhável: adiciona um slot individual com quantidade 1 para cada unidade
		for (int32 i = 0; i < Quantity; ++i)
		{
			InventorySlots.Add(FInventorySlot(ItemID, 1));
			InventorySlots.Last().AcquiredOrder = ++AcquisitionCounter;
		}
	}

	return true;
}

bool UInventorySubsystem::RemoveItem(FName ItemID, int32 Quantity)
{
	if (ItemID.IsNone() || Quantity <= 0)
	{
		return false;
	}

	if (GetItemQuantity(ItemID) < Quantity)
	{
		return false;
	}

	int32 RemainingToRemove = Quantity;

	// Loop reverso para remoção segura de slots do array
	for (int32 i = InventorySlots.Num() - 1; i >= 0; --i)
	{
		if (InventorySlots[i].ItemID == ItemID)
		{
			if (InventorySlots[i].Quantity >= RemainingToRemove)
			{
				InventorySlots[i].Quantity -= RemainingToRemove;
				RemainingToRemove = 0;
			}
			else
			{
				RemainingToRemove -= InventorySlots[i].Quantity;
				InventorySlots[i].Quantity = 0;
			}

			if (InventorySlots[i].Quantity <= 0)
			{
				InventorySlots.RemoveAt(i);
			}

			if (RemainingToRemove <= 0)
			{
				break;
			}
		}
	}

	return true;
}

bool UInventorySubsystem::HasItem(FName ItemID, int32 Quantity) const
{
	return GetItemQuantity(ItemID) >= Quantity;
}

int32 UInventorySubsystem::GetItemQuantity(FName ItemID) const
{
	int32 Total = 0;
	for (const FInventorySlot& Slot : InventorySlots)
	{
		if (Slot.ItemID == ItemID)
		{
			Total += Slot.Quantity;
		}
	}
	return Total;
}

TArray<FInventorySlot> UInventorySubsystem::GetInventorySlotsByCategory(EItemCategory Category) const
{
	TArray<FInventorySlot> FilteredSlots;
	for (const FInventorySlot& Slot : InventorySlots)
	{
		FItemData ItemData;
		if (GetItemData(Slot.ItemID, ItemData))
		{
			if (ItemData.Category == Category)
			{
				FilteredSlots.Add(Slot);
			}
		}
	}
	return FilteredSlots;
}

bool UInventorySubsystem::GetItemData(FName ItemID, FItemData& OutItemData) const
{
	if (!ItemDataTable)
	{
		return false;
	}

	FItemData* Row = ItemDataTable->FindRow<FItemData>(ItemID, TEXT("InventorySubsystem"));
	if (Row)
	{
		OutItemData = *Row;
		return true;
	}

	return false;
}

bool UInventorySubsystem::CanDiscardItem(FName ItemID) const
{
	FItemData ItemData;
	if (!GetItemData(ItemID, ItemData))
	{
		return false;
	}

	// Item de missao trava a historia; Art Book apaga uma Art para sempre.
	if (ItemData.Category == EItemCategory::Key
		|| ItemData.Category == EItemCategory::ArtBook)
	{
		return false;
	}

	return HasItem(ItemID, 1);
}

void UInventorySubsystem::ResetInventory()
{
	InventorySlots.Empty();
	AcquisitionCounter = 0;
}

void UInventorySubsystem::LoadInventoryState(const TArray<FInventorySlot>& SavedSlots)
{
	InventorySlots = SavedSlots;

	// O contador precisa retomar acima do maior carimbo salvo, senao os
	// proximos itens coletados apareceriam como "antigos" no filtro.
	AcquisitionCounter = 0;
	for (const FInventorySlot& Slot : InventorySlots)
	{
		AcquisitionCounter = FMath::Max(AcquisitionCounter, Slot.AcquiredOrder);
	}
}

bool UInventorySubsystem::CanUseItem(FName ItemID, bool bInBattle) const
{
	FItemData ItemData;
	if (!GetItemData(ItemID, ItemData))
	{
		return false;
	}

	switch (ItemData.UseContext)
	{
	case EUseContext::AnyTime:
		return true;
	case EUseContext::BattleOnly:
		return bInBattle;
	case EUseContext::FieldOnly:
		return !bInBattle;
	case EUseContext::Automatic:
	case EUseContext::QuestOnly:
	default:
		return false;
	}
}

bool UInventorySubsystem::UseItem(FName ItemID, FName TargetCharacter, bool bInBattle)
{
	FItemData ItemData;
	if (!GetItemData(ItemID, ItemData))
	{
		return false;
	}

	if (!HasItem(ItemID, 1))
	{
		UE_LOG(LogTemp, Warning, TEXT("InventorySubsystem: O jogador nao possui o item %s no inventario."), *ItemID.ToString());
		return false;
	}

	if (!CanUseItem(ItemID, bInBattle))
	{
		UE_LOG(LogTemp, Warning, TEXT("InventorySubsystem: Item %s nao pode ser usado no contexto atual."), *ItemID.ToString());
		return false;
	}

	// Consome se for um item esgotável
	bool bShouldConsume = (ItemData.Category == EItemCategory::Consumable ||
						   ItemData.Category == EItemCategory::PermanentStat ||
						   ItemData.Category == EItemCategory::ArtBook ||
						   ItemData.Category == EItemCategory::FishingLure);

	if (bShouldConsume)
	{
		RemoveItem(ItemID, 1);
	}

	// Roteamento inteligente para os subsistemas em C++
	UGameInstance* GI = GetGameInstance();
	if (GI)
	{
		switch (ItemData.Category)
		{
		case EItemCategory::Consumable:
		case EItemCategory::FishingLure:
			if (bInBattle)
			{
				UBattleSubsystem* BattleSub = GI->GetSubsystem<UBattleSubsystem>();
				if (BattleSub)
				{
					BattleSub->ApplyBattleItemEffect(TargetCharacter, ItemData);
				}
			}
			else
			{
				UPartySubsystem* PartySub = GI->GetSubsystem<UPartySubsystem>();
				if (PartySub)
				{
					PartySub->ApplyConsumableEffect(TargetCharacter, ItemData);
				}
			}
			break;

		case EItemCategory::PermanentStat:
			{
				UStatusSubsystem* StatusSub = GI->GetSubsystem<UStatusSubsystem>();
				if (StatusSub)
				{
					StatusSub->ApplyPermanentStatUpgrade(TargetCharacter, ItemData.EffectClass, ItemData.EffectValue);
				}
			}
			break;

		case EItemCategory::ArtBook:
			{
				UArtsSubsystem* ArtsSub = GI->GetSubsystem<UArtsSubsystem>();
				if (ArtsSub)
				{
					ArtsSub->TeachArtToCharacter(ItemData.TeachesCharacter, ItemData.TeachesArt);
				}
			}
			break;

		default:
			break;
		}
	}

	// Dispara o delegate para listeners (Blueprints, etc.)
	if (OnItemUsed.IsBound())
	{
		OnItemUsed.Broadcast(ItemData, TargetCharacter);
	}

	return true;
}

bool UInventorySubsystem::AddItemRow(FDataTableRowHandle ItemRow, int32 Quantity)
{
	return AddItem(ItemRow.RowName, Quantity);
}

bool UInventorySubsystem::RemoveItemRow(FDataTableRowHandle ItemRow, int32 Quantity)
{
	return RemoveItem(ItemRow.RowName, Quantity);
}

bool UInventorySubsystem::HasItemRow(FDataTableRowHandle ItemRow, int32 Quantity) const
{
	return HasItem(ItemRow.RowName, Quantity);
}

int32 UInventorySubsystem::GetItemQuantityRow(FDataTableRowHandle ItemRow) const
{
	return GetItemQuantity(ItemRow.RowName);
}

bool UInventorySubsystem::UseItemRow(FDataTableRowHandle ItemRow, FName TargetCharacter, bool bInBattle)
{
	return UseItem(ItemRow.RowName, TargetCharacter, bInBattle);
}

void UInventorySubsystem::SetItemDataTable(UDataTable* NewTable)
{
	ItemDataTable = NewTable;
}

bool UInventorySubsystem::IsCategoryStackable(EItemCategory Category) const
{
	switch (Category)
	{
	case EItemCategory::Consumable:
	case EItemCategory::PermanentStat:
	case EItemCategory::FishingLure:
		return true;
	default:
		return false;
	}
}
