#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Party/JRPGPartyTypes.h"
#include "Party/CharacterData.h"
#include "StatusSubsystem.generated.h"

/** Disparado quando o equipamento de alguém muda. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEquipmentChangedSignature, FName, Character);

/** Disparado quando uma condição entra ou sai. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnConditionChangedSignature,
	FName, Character, FName, Condition, bool, bApplied);

/**
 * UStatusSubsystem
 * Quem CALCULA e MODIFICA o estado de um personagem.
 *
 * A divisão com o UPartySubsystem é a mesma que o original mostra na tela de
 * status, no formato `efetivo ( base )`:
 *
 *   PARTY   é dono do REGISTRO — roster, level, XP, atributos BASE, e os campos
 *           de equipamento e condições. O save é um array só, dele.
 *   STATUS  é dono da REGRA — equipar, aplicar condição, e responder quanto vale
 *           o atributo EFETIVO depois de somar tudo por cima.
 *
 * Este subsystem NÃO guarda registro próprio. Ele lê e escreve no do Party, e é
 * o ÚNICO que deveria escrever em Equipment e Conditions. Dividir o dado em dois
 * donos criaria dois saves para manter em sincronia.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UStatusSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UStatusSubsystem();

	// ============================================================
	// EQUIPAMENTO
	// ============================================================

	/**
	 * Em que slot este item entra, deduzido da DT_Items: arma pela categoria,
	 * armadura pelo ArmorSlot (Armor/Helmet/Shoes), acessório pela categoria.
	 *
	 * Devolve false se o item não é equipável.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Equip")
	bool GetSlotForItem(FName ItemID, EJRPGEquipSlot& OutSlot) const;

	/**
	 * Este personagem pode usar este item?
	 *
	 * Arma: o WeaponType tem que estar nas WeaponClasses do personagem, OU ele
	 * tem que estar em EquipBest/EquipOthers. Armadura: respeita EquipCharacter
	 * quando preenchido. Acessório: todo mundo.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Equip")
	bool CanEquip(FName Character, FName ItemID) const;

	/**
	 * Equipa. Tira o item do inventário e devolve o que estava no slot.
	 * Recusa se o personagem não pode usar ou se o item não está no inventário.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Equip")
	bool EquipItem(FName Character, FName ItemID);

	/** Desequipa e devolve o item ao inventário. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Equip")
	bool UnequipItem(FName Character, EJRPGEquipSlot Slot);

	/** O que está no slot (NAME_None se vazio). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Equip")
	FName GetEquipped(FName Character, EJRPGEquipSlot Slot) const;

	// ============================================================
	// ATRIBUTOS — o `efetivo ( base )` do original
	// ============================================================

	/**
	 * O atributo BASE: o número que o personagem tem por level, sem nada
	 * somado. É o que aparece entre parênteses na tela de status.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Stats")
	int32 GetBaseStat(FName Character, FName Stat) const;

	/** Só o que o equipamento acrescenta (pode ser negativo). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Stats")
	int32 GetEquipmentBonus(FName Character, FName Stat) const;

	/**
	 * O atributo EFETIVO: base + equipamento + percentual de acessório,
	 * limitado pelo teto do CoreSubsystem. É o número branco da tela de status
	 * e o que a batalha deve usar.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Stats")
	int32 GetEffectiveStat(FName Character, FName Stat) const;

	/**
	 * Sobe o atributo BASE de forma permanente — são as "Waters" do original.
	 * Chamado pelo InventorySubsystem quando se usa um item PermanentStat.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Stats")
	void ApplyPermanentStatUpgrade(FName CharacterName, const FString& EffectClass, int32 EffectValue);

	// ============================================================
	// CONDIÇÕES
	// ============================================================

	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Condition")
	bool ApplyCondition(FName Character, FName Condition);

	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Condition")
	bool RemoveCondition(FName Character, FName Condition);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Condition")
	bool HasCondition(FName Character, FName Condition) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Condition")
	TArray<FName> GetConditions(FName Character) const;

	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Condition")
	void ClearConditions(FName Character);

	// ============================================================
	// RA-SERU
	// ============================================================

	/** Level do Ra-Seru (o registro é do Party; a leitura passa por aqui). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Seru")
	int32 GetSeruLevel(FName Character) const;

	/** Sobe o level do Ra-Seru, limitado a SeruMaxLevel. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Seru")
	bool RaiseSeruLevel(FName Character, int32 Levels = 1);

	/**
	 * O que o level do Ra-Seru CONCEDE de atributo.
	 *
	 * ATENÇÃO — ISTO NÃO VEM DO DISCO. No original o level do Seru alimenta a
	 * POTÊNCIA DA MAGIA (FUN_801dd864), não os atributos do personagem. O bônus
	 * abaixo é uma decisão nossa, mantida pequena e num lugar só para ser fácil
	 * de mudar ou remover.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Seru")
	int32 GetSeruBonus(FName Character, FName Stat) const;

	/** Teto do level do Ra-Seru. 9 é o do original. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Status|Seru")
	int32 SeruMaxLevel = 9;

	// ============================================================
	// AFINIDADE ELEMENTAL
	// ============================================================

	/**
	 * Multiplicador de dano de um elemento contra este personagem.
	 *
	 * Do original: 1.04 quando os elementos são opostos, 0.96 quando são
	 * iguais, 1.00 no resto (matriz 8x8 em 0x801F53E8, valores /100). As listas
	 * AffinityStrong/AffinityWeak de DT_Characters escolhem o lado.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Affinity")
	float GetElementMultiplier(FName Character, EJRPGElement Attacking) const;

	// ============================================================
	// EVENTOS
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Status")
	FOnEquipmentChangedSignature OnEquipmentChanged;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Status")
	FOnConditionChangedSignature OnConditionChanged;

private:
	/** Garante que o array de Equipment tem um slot por EJRPGEquipSlot. */
	static void EnsureSlots(FJRPGPartyMember& Member);
};
