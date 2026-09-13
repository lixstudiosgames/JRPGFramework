#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ItemData.generated.h"

class UTexture2D;

/**
 * Categorias de itens compatíveis com os formatos TOML e referências do jogo.
 */
UENUM(BlueprintType)
enum class EItemCategory : uint8
{
	Consumable      UMETA(DisplayName = "Consumable"),
	PermanentStat   UMETA(DisplayName = "Permanent Stat Upgrade"),
	ArtBook         UMETA(DisplayName = "Art Book"),
	Key             UMETA(DisplayName = "Key Item"),
	FishingLure     UMETA(DisplayName = "Fishing Lure"),
	Weapon          UMETA(DisplayName = "Weapon"),
	Armor           UMETA(DisplayName = "Armor"),
	Accessory       UMETA(DisplayName = "Accessory")
};

/**
 * Contextos de uso dos itens definindo quando podem ser ativados.
 */
UENUM(BlueprintType)
enum class EUseContext : uint8
{
	AnyTime         UMETA(DisplayName = "Any Time"),
	BattleOnly      UMETA(DisplayName = "Battle Only"),
	FieldOnly       UMETA(DisplayName = "Field Only"),
	Automatic       UMETA(DisplayName = "Automatic (Passive/Equip)"),
	QuestOnly       UMETA(DisplayName = "Quest Only")
};

/**
 * Slots de equipamentos de defesa (Armaduras).
 */
UENUM(BlueprintType)
enum class EArmorSlot : uint8
{
	None            UMETA(DisplayName = "None"),
	Armor           UMETA(DisplayName = "Armor (Body)"),
	Helmet          UMETA(DisplayName = "Helmet (Head)"),
	Shoes           UMETA(DisplayName = "Shoes (Feet)")
};

/**
 * FItemData
 * Representa os dados consolidados de qualquer item do jogo (consumíveis, chaves, armas, armaduras, acessórios).
 * Mapeado para ser usado como linha de DataTable (UDataTable).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FItemData : public FTableRowBase
{
	GENERATED_BODY()

	// --- Propriedades Comuns ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Common")
	FName Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Common")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Common")
	EItemCategory Category;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Common")
	int32 BuyPrice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Common")
	FText EffectDescription;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Common")
	EUseContext UseContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Common")
	TSoftObjectPtr<UTexture2D> Icon;

	// --- Atributos de Consumíveis / Buffs / Status ---
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Effects")
	int32 HealHP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Effects")
	int32 HealMP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Effects")
	bool bHealAP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Effects")
	bool bCureStatus;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Effects")
	bool bRevive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Effects")
	bool bTargetAll;

	// --- Art Book specific fields ---
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Art Book")
	FName TeachesArt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Art Book")
	FString TeachesCharacter;

	// --- Weapon specific fields ---
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Weapon")
	FString WeaponType; // e.g. Sword, Claw, Axe

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Weapon")
	FString EquipBest; // Character who is best suited (full ATK)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Weapon")
	TArray<FString> EquipOthers; // Secondary characters who can equip

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Weapon")
	int32 AttackBonus;

	// --- Armor specific fields ---
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Armor")
	EArmorSlot ArmorSlot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Armor")
	FString EquipCharacter; // Character who can equip this armor

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Armor")
	int32 UDF; // Upper Defense Factor

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Armor")
	int32 LDF; // Lower Defense Factor

	// --- Accessory specific fields ---
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory")
	FString EffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory")
	int32 EffectValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory")
	FString StatusType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory")
	FString ElementType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Accessory")
	FName Summons;

	FItemData()
		: Key(NAME_None)
		, Category(EItemCategory::Consumable)
		, BuyPrice(0)
		, UseContext(EUseContext::AnyTime)
		, HealHP(0)
		, HealMP(0)
		, bHealAP(false)
		, bCureStatus(false)
		, bRevive(false)
		, bTargetAll(false)
		, TeachesArt(NAME_None)
		, AttackBonus(0)
		, ArmorSlot(EArmorSlot::None)
		, UDF(0)
		, LDF(0)
		, EffectValue(0)
		, Summons(NAME_None)
	{}
};

/**
 * FInventorySlot
 * Representa um único slot do inventário do jogador, contendo a referência (ID) do item
 * e a sua quantidade atual carregada.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 Quantity;

	/**
	 * Carimbo de quando este slot foi mexido pela ultima vez (contador que so
	 * cresce). Serve ao filtro "ultimo coletado" da tela de Itens.
	 *
	 * NAO da para usar a ordem do array: ela guarda a PRIMEIRA aquisicao, entao
	 * pegar mais 10 de um item que voce ja tinha nao mexeria a pilha de lugar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 AcquiredOrder = 0;

	FInventorySlot()
		: ItemID(NAME_None)
		, Quantity(0)
	{}

	FInventorySlot(FName InItemID, int32 InQuantity)
		: ItemID(InItemID)
		, Quantity(InQuantity)
	{}
};
