#include "Status/StatusSubsystem.h"

#include "Core/CoreSubsystem.h"
#include "Inventory/InventorySubsystem.h"
#include "Party/PartySubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

namespace
{
	/** Percentual de acessorio que mexe em cada atributo (EffectClass -> stat). */
	struct FEffeitoPercentual
	{
		const TCHAR* EffectClass;
		const TCHAR* Stat;
	};

	// Os EffectClass da DT_Items que alteram um dos 8 atributos. O resto (XP,
	// gold, encontros, drop) nao entra aqui — nao mexe em atributo.
	const FEffeitoPercentual PercentuaisDeAtributo[] =
	{
		{ TEXT("hp_max_pct"),       TEXT("HP")  },
		{ TEXT("mp_max_pct"),       TEXT("MP")  },
		{ TEXT("attack_pct"),       TEXT("ATK") },
		{ TEXT("udf_pct"),          TEXT("UDF") },
		{ TEXT("ldf_pct"),          TEXT("LDF") },
		{ TEXT("speed_pct"),        TEXT("SPD") },
		{ TEXT("agility_pct"),      TEXT("AGL") },
		{ TEXT("intelligence_pct"), TEXT("INT") },
	};

	/** defense_pct mexe nas DUAS defesas — por isso fica fora da tabela acima. */
	bool EhDefesa(FName Stat)
	{
		return Stat == TEXT("UDF") || Stat == TEXT("LDF");
	}

	/**
	 * Compara classe de arma tolerando o plural.
	 *
	 * As duas tabelas nao falam a mesma lingua: DT_Items.WeaponType e singular
	 * ("Sword", "Claw") e DT_Characters.WeaponClasses veio do extrator no plural
	 * ("Swords", "Knives"). Normalizar aqui e mais barato do que reescrever a
	 * extracao — e sobrevive se alguem reimportar a tabela antiga.
	 */
	bool MesmaClasseDeArma(const FString& A, const FString& B)
	{
		auto Normaliza = [](const FString& S)
		{
			FString N = S.ToLower();
			if (N.EndsWith(TEXT("ves")))  { return N.LeftChop(3) + TEXT("fe"); } // Knives -> knife
			if (N.EndsWith(TEXT("es")))   { return N.LeftChop(2); }             // Axes   -> axe
			if (N.EndsWith(TEXT("s")))    { return N.LeftChop(1); }             // Swords -> sword
			return N;
		};
		return !A.IsEmpty() && Normaliza(A) == Normaliza(B);
	}

	/**
	 * O extrator escreveu o literal "None" onde nao havia dono (War God Plate).
	 * Vazio e "None" significam a mesma coisa: qualquer um equipa.
	 */
	bool SemDono(const FString& S)
	{
		return S.IsEmpty() || S.Equals(TEXT("None"), ESearchCase::IgnoreCase);
	}
}

UStatusSubsystem::UStatusSubsystem()
{
}

void UStatusSubsystem::EnsureSlots(FJRPGPartyMember& Member)
{
	const int32 Quantos = static_cast<int32>(EJRPGEquipSlot::MAX);
	if (Member.Equipment.Num() != Quantos)
	{
		Member.Equipment.SetNum(Quantos);
	}
}

// ============================================================
// EQUIPAMENTO
// ============================================================

bool UStatusSubsystem::GetSlotForItem(FName ItemID, EJRPGEquipSlot& OutSlot) const
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	FItemData Item;
	if (!Inv || !Inv->GetItemData(ItemID, Item))
	{
		return false;
	}

	switch (Item.Category)
	{
	case EItemCategory::Weapon:
		OutSlot = EJRPGEquipSlot::Weapon;
		return true;

	case EItemCategory::Accessory:
		OutSlot = EJRPGEquipSlot::Accessory;
		return true;

	case EItemCategory::Armor:
		switch (Item.ArmorSlot)
		{
		case EArmorSlot::Armor:  OutSlot = EJRPGEquipSlot::Armor;  return true;
		case EArmorSlot::Helmet: OutSlot = EJRPGEquipSlot::Helmet; return true;
		case EArmorSlot::Shoes:  OutSlot = EJRPGEquipSlot::Shoes;  return true;
		default: return false;
		}

	default:
		return false;
	}
}

bool UStatusSubsystem::CanEquip(FName Character, FName ItemID) const
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Inv || !Party || !Party->IsRecruited(Character))
	{
		return false;
	}

	FItemData Item;
	EJRPGEquipSlot Slot;
	if (!Inv->GetItemData(ItemID, Item) || !GetSlotForItem(ItemID, Slot))
	{
		return false;
	}

	const FString Nome = Character.ToString();

	// Arma: a classe tem que estar entre as do personagem. As listas
	// EquipBest/EquipOthers da DT_Items são o desempate quando existem.
	if (Slot == EJRPGEquipSlot::Weapon)
	{
		if (Item.EquipBest.Equals(Nome, ESearchCase::IgnoreCase))
		{
			return true;
		}
		for (const FString& Outro : Item.EquipOthers)
		{
			if (Outro.Equals(Nome, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		// Nenhuma lista bateu. Ainda vale se a CLASSE da arma for uma das dele —
		// e o que cobre uma arma nova que ninguem se lembrou de listar.
		if (const UDataTable* Chars = Party->CharacterTable)
		{
			if (const FCharacterData* Base =
					Chars->FindRow<FCharacterData>(Character, TEXT("CanEquip"), false))
			{
				for (const FString& Classe : Base->WeaponClasses)
				{
					if (MesmaClasseDeArma(Classe, Item.WeaponType))
					{
						return true;
					}
				}
			}
		}

		// A Terra cai sempre aqui: WeaponClasses vazio, e nenhuma arma a lista.
		// E o correto — no original ela nao tem tela de equipamento nenhuma.
		return false;
	}

	// Armadura com dono declarado só serve para ele.
	if (!SemDono(Item.EquipCharacter))
	{
		return Item.EquipCharacter.Equals(Nome, ESearchCase::IgnoreCase);
	}

	// Acessório e armadura sem dono: qualquer um do roster. A Terra inclusive —
	// se um dia a UI dela existir, é por aqui que ela ganha alguma coisa.
	return true;
}

bool UStatusSubsystem::EquipItem(FName Character, FName ItemID)
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Inv || !Party)
	{
		return false;
	}

	EJRPGEquipSlot Slot;
	if (!GetSlotForItem(ItemID, Slot) || !CanEquip(Character, ItemID))
	{
		UE_LOG(LogTemp, Warning, TEXT("StatusSubsystem: %s não pode equipar %s."),
			*Character.ToString(), *ItemID.ToString());
		return false;
	}

	if (!Inv->HasItem(ItemID, 1))
	{
		UE_LOG(LogTemp, Warning, TEXT("StatusSubsystem: %s não está no inventário."),
			*ItemID.ToString());
		return false;
	}

	FJRPGPartyMember* M = Party->FindMemberForWrite(Character);
	if (!M)
	{
		return false;
	}
	EnsureSlots(*M);

	const int32 Idx = static_cast<int32>(Slot);
	const FName Antigo = M->Equipment[Idx];

	// Tira o novo do inventário e devolve o velho, nessa ordem — se o AddItem
	// falhasse primeiro o jogador ficaria com os dois.
	if (!Inv->RemoveItem(ItemID, 1))
	{
		return false;
	}
	M->Equipment[Idx] = ItemID;
	if (!Antigo.IsNone())
	{
		Inv->AddItem(Antigo, 1);
	}

	Party->NotifyMemberChanged();
	OnEquipmentChanged.Broadcast(Character);
	return true;
}

bool UStatusSubsystem::UnequipItem(FName Character, EJRPGEquipSlot Slot)
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Inv || !Party || Slot == EJRPGEquipSlot::MAX)
	{
		return false;
	}

	FJRPGPartyMember* M = Party->FindMemberForWrite(Character);
	if (!M)
	{
		return false;
	}
	EnsureSlots(*M);

	const int32 Idx = static_cast<int32>(Slot);
	const FName Atual = M->Equipment[Idx];
	if (Atual.IsNone())
	{
		return false;
	}

	M->Equipment[Idx] = NAME_None;
	Inv->AddItem(Atual, 1);

	Party->NotifyMemberChanged();
	OnEquipmentChanged.Broadcast(Character);
	return true;
}

FName UStatusSubsystem::GetEquipped(FName Character, EJRPGEquipSlot Slot) const
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	FJRPGPartyMember M;
	if (!Party || Slot == EJRPGEquipSlot::MAX || !Party->GetMember(Character, M))
	{
		return NAME_None;
	}

	const int32 Idx = static_cast<int32>(Slot);
	return M.Equipment.IsValidIndex(Idx) ? M.Equipment[Idx] : NAME_None;
}

// ============================================================
// ATRIBUTOS
// ============================================================

int32 UStatusSubsystem::GetBaseStat(FName Character, FName Stat) const
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	FJRPGPartyMember M;
	if (!Party || !Party->GetMember(Character, M))
	{
		return 0;
	}

	if (Stat == TEXT("HP"))  { return M.MaxHP; }
	if (Stat == TEXT("MP"))  { return M.MaxMP; }
	if (Stat == TEXT("AGL")) { return M.AGL; }
	if (Stat == TEXT("ATK")) { return M.ATK; }
	if (Stat == TEXT("UDF")) { return M.UDF; }
	if (Stat == TEXT("LDF")) { return M.LDF; }
	if (Stat == TEXT("SPD")) { return M.SPD; }
	if (Stat == TEXT("INT")) { return M.INT; }
	return 0;
}

int32 UStatusSubsystem::GetEquipmentBonus(FName Character, FName Stat) const
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	FJRPGPartyMember M;
	if (!Inv || !Party || !Party->GetMember(Character, M))
	{
		return 0;
	}

	int32 Soma = 0;
	for (const FName& Equipado : M.Equipment)
	{
		FItemData Item;
		if (Equipado.IsNone() || !Inv->GetItemData(Equipado, Item))
		{
			continue;
		}

		if (Stat == TEXT("ATK")) { Soma += Item.AttackBonus; }
		if (Stat == TEXT("UDF")) { Soma += Item.UDF; }
		if (Stat == TEXT("LDF")) { Soma += Item.LDF; }
	}
	return Soma;
}

int32 UStatusSubsystem::GetEffectiveStat(FName Character, FName Stat) const
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	FJRPGPartyMember M;
	if (!Inv || !Party || !Party->GetMember(Character, M))
	{
		return 0;
	}

	// base + bônus fixo do equipamento + o que o Ra-Seru concede
	int64 Valor = GetBaseStat(Character, Stat)
	            + GetEquipmentBonus(Character, Stat)
	            + GetSeruBonus(Character, Stat);

	// Percentuais de acessório entram POR ÚLTIMO, sobre o total — é o que faz
	// um anel de +20% valer mais quanto melhor for o equipamento.
	int32 Percentual = 0;
	for (const FName& Equipado : M.Equipment)
	{
		FItemData Item;
		if (Equipado.IsNone() || !Inv->GetItemData(Equipado, Item)
			|| Item.EffectClass.IsEmpty() || Item.EffectValue == 0)
		{
			continue;
		}

		for (const FEffeitoPercentual& Efeito : PercentuaisDeAtributo)
		{
			if (Item.EffectClass.Equals(Efeito.EffectClass, ESearchCase::IgnoreCase)
				&& Stat == FName(Efeito.Stat))
			{
				Percentual += Item.EffectValue;
			}
		}

		// defense_pct mexe nas duas defesas de uma vez.
		if (Item.EffectClass.Equals(TEXT("defense_pct"), ESearchCase::IgnoreCase)
			&& EhDefesa(Stat))
		{
			Percentual += Item.EffectValue;
		}
	}

	if (Percentual != 0)
	{
		Valor += (Valor * Percentual) / 100;
	}

	const int32 Teto = Core ? Core->GetStatCap(Stat) : 999;
	return static_cast<int32>(FMath::Clamp<int64>(Valor, 0, Teto));
}

void UStatusSubsystem::ApplyPermanentStatUpgrade(FName CharacterName, const FString& EffectClass, int32 EffectValue)
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!Party || !Core)
	{
		return;
	}

	// As "Waters" do original sobem o BASE, não o efetivo — por isso escrevem
	// no registro do Party em vez de virar um modificador daqui.
	FJRPGPartyMember* M = Party->FindMemberForWrite(CharacterName);
	if (!M)
	{
		UE_LOG(LogTemp, Warning, TEXT("StatusSubsystem: %s não está no roster."),
			*CharacterName.ToString());
		return;
	}

	auto Sobe = [&](int32& Campo, const TCHAR* Stat)
	{
		Campo = FMath::Min(Campo + EffectValue, Core->GetStatCap(FName(Stat)));
	};

	if      (EffectClass.Equals(TEXT("hp_max"),  ESearchCase::IgnoreCase)) { Sobe(M->MaxHP, TEXT("HP"));  M->HP += EffectValue; M->HP = FMath::Min(M->HP, M->MaxHP); }
	else if (EffectClass.Equals(TEXT("mp_max"),  ESearchCase::IgnoreCase)) { Sobe(M->MaxMP, TEXT("MP"));  M->MP += EffectValue; M->MP = FMath::Min(M->MP, M->MaxMP); }
	else if (EffectClass.Equals(TEXT("attack"),  ESearchCase::IgnoreCase)) { Sobe(M->ATK, TEXT("ATK")); }
	else if (EffectClass.Equals(TEXT("defense"), ESearchCase::IgnoreCase)) { Sobe(M->UDF, TEXT("UDF")); Sobe(M->LDF, TEXT("LDF")); }
	else if (EffectClass.Equals(TEXT("speed"),   ESearchCase::IgnoreCase)) { Sobe(M->SPD, TEXT("SPD")); }
	else if (EffectClass.Equals(TEXT("agility"), ESearchCase::IgnoreCase)) { Sobe(M->AGL, TEXT("AGL")); }
	else if (EffectClass.Equals(TEXT("intelligence"), ESearchCase::IgnoreCase)) { Sobe(M->INT, TEXT("INT")); }
	else if (EffectClass.Equals(TEXT("all_stats"), ESearchCase::IgnoreCase))
	{
		// Miracle Water e Honey. "Todos" aqui são os cinco de combate: a AGL
		// fica de fora de propósito (ela é art blocks, ver CoreSubsystem::
		// SetStatCap) e HP/MP têm as suas próprias Waters.
		Sobe(M->ATK, TEXT("ATK"));
		Sobe(M->UDF, TEXT("UDF"));
		Sobe(M->LDF, TEXT("LDF"));
		Sobe(M->SPD, TEXT("SPD"));
		Sobe(M->INT, TEXT("INT"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("StatusSubsystem: upgrade permanente '%s' desconhecido."),
			*EffectClass);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("StatusSubsystem: %s +%d permanente em %s."),
		*CharacterName.ToString(), EffectValue, *EffectClass);

	Party->NotifyMemberChanged();
}

// ============================================================
// CONDIÇÕES
// ============================================================

bool UStatusSubsystem::ApplyCondition(FName Character, FName Condition)
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party || Condition.IsNone())
	{
		return false;
	}

	FJRPGPartyMember* M = Party->FindMemberForWrite(Character);
	if (!M || M->Conditions.Contains(Condition))
	{
		return false;
	}

	M->Conditions.Add(Condition);
	Party->NotifyMemberChanged();
	OnConditionChanged.Broadcast(Character, Condition, true);
	return true;
}

bool UStatusSubsystem::RemoveCondition(FName Character, FName Condition)
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party)
	{
		return false;
	}

	FJRPGPartyMember* M = Party->FindMemberForWrite(Character);
	if (!M || M->Conditions.Remove(Condition) == 0)
	{
		return false;
	}

	Party->NotifyMemberChanged();
	OnConditionChanged.Broadcast(Character, Condition, false);
	return true;
}

bool UStatusSubsystem::HasCondition(FName Character, FName Condition) const
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	FJRPGPartyMember M;
	return Party && Party->GetMember(Character, M) && M.Conditions.Contains(Condition);
}

TArray<FName> UStatusSubsystem::GetConditions(FName Character) const
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	FJRPGPartyMember M;
	if (Party && Party->GetMember(Character, M))
	{
		return M.Conditions;
	}
	return TArray<FName>();
}

void UStatusSubsystem::ClearConditions(FName Character)
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party)
	{
		return;
	}

	FJRPGPartyMember* M = Party->FindMemberForWrite(Character);
	if (!M || M->Conditions.Num() == 0)
	{
		return;
	}

	const TArray<FName> Antigas = M->Conditions;
	M->Conditions.Empty();
	Party->NotifyMemberChanged();
	for (const FName& C : Antigas)
	{
		OnConditionChanged.Broadcast(Character, C, false);
	}
}

// ============================================================
// RA-SERU
// ============================================================

int32 UStatusSubsystem::GetSeruLevel(FName Character) const
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	FJRPGPartyMember M;
	return (Party && Party->GetMember(Character, M)) ? M.SeruLevel : 0;
}

bool UStatusSubsystem::RaiseSeruLevel(FName Character, int32 Levels)
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party || Levels <= 0)
	{
		return false;
	}

	FJRPGPartyMember* M = Party->FindMemberForWrite(Character);
	if (!M || M->SeruLevel >= SeruMaxLevel)
	{
		return false;
	}

	M->SeruLevel = FMath::Min(M->SeruLevel + Levels, SeruMaxLevel);
	Party->NotifyMemberChanged();
	return true;
}

int32 UStatusSubsystem::GetSeruBonus(FName Character, FName Stat) const
{
	// NÃO VEM DO DISCO — ver o comentário da declaração.
	//
	// Regra: o Ra-Seru fortalece a mente de quem o carrega. Cada level acima do
	// primeiro dá +2% de INT sobre o base. Nos outros sete atributos, nada.
	// Um lugar só para mudar se você decidir outra coisa.
	if (Stat != TEXT("INT"))
	{
		return 0;
	}

	const int32 Level = GetSeruLevel(Character);
	if (Level <= 1)
	{
		return 0;
	}

	const int32 Base = GetBaseStat(Character, Stat);
	return (Base * 2 * (Level - 1)) / 100;
}

// ============================================================
// AFINIDADE
// ============================================================

float UStatusSubsystem::GetElementMultiplier(FName Character, EJRPGElement Attacking) const
{
	if (Attacking == EJRPGElement::None)
	{
		return 1.0f;
	}

	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party || !Party->CharacterTable)
	{
		return 1.0f;
	}

	const FCharacterData* Base = Party->CharacterTable->FindRow<FCharacterData>(
		Character, TEXT("GetElementMultiplier"), false);
	if (!Base)
	{
		return 1.0f;
	}

	// Do original (matriz 8x8 em 0x801F53E8, valores /100): oposto 104,
	// mesmo elemento 96, resto 100. AffinityWeak é onde dói.
	if (Base->AffinityWeak.Contains(Attacking))
	{
		return 1.04f;
	}
	if (Base->AffinityStrong.Contains(Attacking))
	{
		return 0.96f;
	}
	return 1.0f;
}
