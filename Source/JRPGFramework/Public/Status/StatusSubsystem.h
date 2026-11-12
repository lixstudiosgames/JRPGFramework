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
	 * Para acessório devolve o primeiro dos três (Accessory); qualquer um dos
	 * três serve (IsAccessorySlot).
	 *
	 * Devolve false se o item não é equipável.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Equip")
	bool GetSlotForItem(FName ItemID, EJRPGEquipSlot& OutSlot) const;

	/** Accessory, Accessory2 ou Accessory3. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|Equip")
	static bool IsAccessorySlot(EJRPGEquipSlot Slot);

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
	 * Acessório vai para o primeiro slot de acessório vazio; com os três
	 * ocupados, troca o do primeiro.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Equip")
	bool EquipItem(FName Character, FName ItemID);

	/**
	 * Equipa num slot escolhido (a tela de equipamento escolhendo entre os
	 * três de acessório). O slot tem que aceitar o item: o do GetSlotForItem,
	 * ou qualquer slot de acessório para um acessório.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|Equip")
	bool EquipItemInSlot(FName Character, FName ItemID, EJRPGEquipSlot Slot);

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

	/** Level do Ra-Seru (o registro é do Party; a leitura passa por aqui). 0 = ainda não tem. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|RaSeru")
	int32 GetRaSeruLevel(FName Character) const;

	/** Tem o Ra-Seru (RaSeruLevel > 0). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|RaSeru")
	bool HasRaSeru(FName Character) const;

	/** Sobe o level do Ra-Seru, limitado a RaSeruMaxLevel. Sem o Ra-Seru (0), subir 1 é recebê-lo. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|RaSeru")
	bool RaiseRaSeruLevel(FName Character, int32 Levels = 1);

	/** Põe o level do Ra-Seru (0 = tira, 1 = a história o dá; até RaSeruMaxLevel). False se não está no roster ou já era esse. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Status|RaSeru")
	bool SetRaSeruLevel(FName Character, int32 Level);

	/**
	 * O que o level do Ra-Seru CONCEDE de atributo.
	 *
	 * ATENÇÃO — ISTO NÃO VEM DO DISCO. No original o level do Seru alimenta a
	 * POTÊNCIA DA MAGIA (FUN_801dd864), não os atributos do personagem. O bônus
	 * abaixo é uma decisão nossa, mantida pequena e num lugar só para ser fácil
	 * de mudar ou remover.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Status|RaSeru")
	int32 GetRaSeruBonus(FName Character, FName Stat) const;

	/**
	 * Teto do level do Ra-Seru, o mesmo para os três. O original para em Meta 9, Terra 8, Ozma 7; o remake vai até 10, pronto para
	 * expandir. Acima do teto original de cada um, a luta repete o último tier dele (o braço, o golpe e o ATK).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Status|RaSeru")
	int32 RaSeruMaxLevel = 10;

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
