#include "Party/PartySubsystem.h"

#include "Core/CoreSubsystem.h"
#include "Party/CharacterData.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

namespace
{
	/** Carrega /Game/Data/<Nome> e, se não achar, /JRPGFramework/Data/<Nome>. */
	UDataTable* ResolveCharacterTable()
	{
		if (UDataTable* Tabela = Cast<UDataTable>(StaticLoadObject(
				UDataTable::StaticClass(), nullptr, TEXT("/Game/Data/DT_Characters"))))
		{
			return Tabela;
		}
		return Cast<UDataTable>(StaticLoadObject(
			UDataTable::StaticClass(), nullptr, TEXT("/JRPGFramework/Data/DT_Characters")));
	}
}

UPartySubsystem::UPartySubsystem()
{
}

void UPartySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!CharacterTable)
	{
		CharacterTable = ResolveCharacterTable();
	}

	if (!CharacterTable)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PartySubsystem: DT_Characters não encontrada — RecruitCharacter vai falhar. ")
			TEXT("Importe Docs/Data/DT_Characters.csv em /Game/Data/DT_Characters."));
	}
}

void UPartySubsystem::SetCharacterTable(UDataTable* NewTable)
{
	CharacterTable = NewTable;
}

// ============================================================
// RECRUTAMENTO
// ============================================================

bool UPartySubsystem::RecruitCharacter(FName Character)
{
	if (Character.IsNone())
	{
		return false;
	}

	// JÁ É DO JOGADOR: não reseta nada. Só volta a ficar disponível.
	// É esta linha que protege o progresso de quem sai e volta na história.
	if (FJRPGPartyMember* Existente = Roster.Find(Character))
	{
		if (!Existente->bAvailable)
		{
			Existente->bAvailable = true;
			RebuildActiveOrder();
			OnPartyChanged.Broadcast();
		}
		return true;
	}

	if (!CharacterTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("PartySubsystem: RecruitCharacter(%s) sem DT_Characters."),
			*Character.ToString());
		return false;
	}

	const FCharacterData* Base =
		CharacterTable->FindRow<FCharacterData>(Character, TEXT("RecruitCharacter"), false);
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("PartySubsystem: '%s' não existe em DT_Characters."),
			*Character.ToString());
		return false;
	}

	FJRPGPartyMember Novo;
	Novo.Key = Character;
	Novo.bRecruited = true;
	Novo.bAvailable = true;
	ResetMemberToBase(Novo);

	Roster.Add(Character, Novo);

	// Entra na formação se ainda houver vaga.
	if (ActiveOrder.Num() < MaxActiveMembers)
	{
		Roster[Character].bActive = true;
	}
	RebuildActiveOrder();

	UE_LOG(LogTemp, Log, TEXT("PartySubsystem: %s recrutado (LV1, HP %d, MP %d)%s."),
		*Character.ToString(), Novo.MaxHP, Novo.MaxMP,
		Roster[Character].bActive ? TEXT(", na formação") : TEXT(", fora da formação"));

	OnPartyChanged.Broadcast();
	return true;
}

bool UPartySubsystem::IsRecruited(FName Character) const
{
	const FJRPGPartyMember* M = Roster.Find(Character);
	return M && M->bRecruited;
}

void UPartySubsystem::SetAvailable(FName Character, bool bAvailable)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	if (!M || M->bAvailable == bAvailable)
	{
		return;
	}

	M->bAvailable = bAvailable;
	if (!bAvailable)
	{
		// Sai da formação, mas o registro fica inteiro.
		M->bActive = false;
	}

	RebuildActiveOrder();
	OnPartyChanged.Broadcast();
}

bool UPartySubsystem::IsAvailable(FName Character) const
{
	const FJRPGPartyMember* M = Roster.Find(Character);
	return M && M->bRecruited && M->bAvailable;
}

// ============================================================
// FORMAÇÃO
// ============================================================

bool UPartySubsystem::SetActive(FName Character, bool bActive)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	if (!M || !M->bRecruited)
	{
		return false;
	}

	if (bActive)
	{
		if (!M->bAvailable)
		{
			UE_LOG(LogTemp, Warning, TEXT("PartySubsystem: %s não está disponível na história."),
				*Character.ToString());
			return false;
		}
		if (!M->bActive && ActiveOrder.Num() >= MaxActiveMembers)
		{
			UE_LOG(LogTemp, Warning, TEXT("PartySubsystem: formação cheia (%d de %d)."),
				ActiveOrder.Num(), MaxActiveMembers);
			return false;
		}
	}
	else if (M->bActive && ActiveOrder.Num() <= 1)
	{
		// A formação nunca fica vazia — senão não há com quem jogar.
		UE_LOG(LogTemp, Warning, TEXT("PartySubsystem: %s é o último da formação, não dá para tirar."),
			*Character.ToString());
		return false;
	}

	if (M->bActive == bActive)
	{
		return true;
	}

	M->bActive = bActive;
	RebuildActiveOrder();
	OnPartyChanged.Broadcast();
	return true;
}

bool UPartySubsystem::IsActive(FName Character) const
{
	const FJRPGPartyMember* M = Roster.Find(Character);
	return M && M->bActive;
}

bool UPartySubsystem::SetActiveParty(const TArray<FName>& Characters)
{
	// Valida tudo ANTES de mexer: ou a formação inteira entra, ou nada muda.
	TArray<FName> Validos;
	for (const FName& Key : Characters)
	{
		const FJRPGPartyMember* M = Roster.Find(Key);
		if (M && M->bRecruited && M->bAvailable && !Validos.Contains(Key))
		{
			Validos.Add(Key);
		}
	}

	if (Validos.Num() == 0 || Validos.Num() > MaxActiveMembers)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PartySubsystem: SetActiveParty recusado — %d válidos (precisa de 1 a %d)."),
			Validos.Num(), MaxActiveMembers);
		return false;
	}

	for (TPair<FName, FJRPGPartyMember>& Par : Roster)
	{
		Par.Value.bActive = Validos.Contains(Par.Key);
	}

	ActiveOrder = Validos;
	OnPartyChanged.Broadcast();
	return true;
}

TArray<FJRPGPartyMember> UPartySubsystem::GetActiveMembers() const
{
	TArray<FJRPGPartyMember> Out;
	for (const FName& Key : ActiveOrder)
	{
		if (const FJRPGPartyMember* M = Roster.Find(Key))
		{
			Out.Add(*M);
		}
	}
	return Out;
}

TArray<FJRPGPartyMember> UPartySubsystem::GetRoster() const
{
	TArray<FJRPGPartyMember> Out;
	Roster.GenerateValueArray(Out);
	return Out;
}

bool UPartySubsystem::GetMember(FName Character, FJRPGPartyMember& OutMember) const
{
	if (const FJRPGPartyMember* M = Roster.Find(Character))
	{
		OutMember = *M;
		return true;
	}
	return false;
}

FJRPGPartyMember* UPartySubsystem::FindMemberForWrite(FName Character)
{
	return Roster.Find(Character);
}

void UPartySubsystem::NotifyMemberChanged()
{
	OnPartyChanged.Broadcast();
}

void UPartySubsystem::SetMaxActiveMembers(int32 NewMax)
{
	MaxActiveMembers = FMath::Max(1, NewMax);

	// Encolheu? Tira os excedentes do fim, preservando os registros.
	while (ActiveOrder.Num() > MaxActiveMembers)
	{
		const FName Removido = ActiveOrder.Pop();
		if (FJRPGPartyMember* M = Roster.Find(Removido))
		{
			M->bActive = false;
		}
	}
	OnPartyChanged.Broadcast();
}

void UPartySubsystem::RebuildActiveOrder()
{
	// Mantém a ordem de quem já estava e acrescenta os novos no fim.
	TArray<FName> Nova;
	for (const FName& Key : ActiveOrder)
	{
		const FJRPGPartyMember* M = Roster.Find(Key);
		if (M && M->bRecruited && M->bAvailable && M->bActive)
		{
			Nova.Add(Key);
		}
	}
	for (const TPair<FName, FJRPGPartyMember>& Par : Roster)
	{
		if (Par.Value.bRecruited && Par.Value.bAvailable && Par.Value.bActive
			&& !Nova.Contains(Par.Key))
		{
			Nova.Add(Par.Key);
		}
	}
	ActiveOrder = Nova;
}

// ============================================================
// EXPERIÊNCIA
// ============================================================

int32 UPartySubsystem::AwardBattleXP(int32 EnemyXPSum)
{
	if (EnemyXPSum <= 0)
	{
		return 0;
	}

	// Só quem está lutando E vivo entra na conta.
	TArray<FName> Vivos;
	for (const FName& Key : ActiveOrder)
	{
		const FJRPGPartyMember* M = Roster.Find(Key);
		if (M && M->IsFighting())
		{
			Vivos.Add(Key);
		}
	}

	if (Vivos.Num() == 0)
	{
		return 0;
	}

	// FUN_8004E568: total = soma * 3/4, depois dividido entre os VIVOS.
	int32 Total = EnemyXPSum - (EnemyXPSum >> 2);

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UCoreSubsystem* Core = GI->GetSubsystem<UCoreSubsystem>())
		{
			Total = Core->ScaleXPReward(Total);
		}
	}

	const int32 PorMembro = Total / Vivos.Num();
	for (const FName& Key : Vivos)
	{
		GrantXP(Key, PorMembro);
	}

	UE_LOG(LogTemp, Log, TEXT("PartySubsystem: %d XP dividido entre %d vivo(s) = %d cada."),
		Total, Vivos.Num(), PorMembro);

	return PorMembro;
}

void UPartySubsystem::GrantXP(FName Character, int32 Amount)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	if (!M || Amount <= 0 || M->IsDead())
	{
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!Core)
	{
		return;
	}

	M->ExperienceTotal += Amount;

	// Sobe todos os levels que couberem de uma vez.
	while (M->Level < Core->GetMaxLevel()
		&& M->ExperienceTotal >= Core->GetXPForLevelForSlot(M->Level + 1, M->XPCurveSlot))
	{
		ApplyLevelUp(*M);
	}
}

void UPartySubsystem::ApplyLevelUp(FJRPGPartyMember& Member)
{
	const UGameInstance* GI = GetGameInstance();
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!Core)
	{
		return;
	}

	const int32 De = Member.Level;
	Member.Level = De + 1;

	// RollStatGain inclui o jitter do original — é por isso que os atributos
	// ficam guardados no registro em vez de recalculados pela fórmula.
	auto Sobe = [&](const TCHAR* Stat, int32& Campo)
	{
		const int32 Ganho = Core->RollStatGain(Member.Key, FName(Stat), De);
		Campo = FMath::Min(Campo + Ganho, Core->GetStatCap(FName(Stat)));
	};

	const int32 HPAntes = Member.MaxHP;
	const int32 MPAntes = Member.MaxMP;

	Sobe(TEXT("HP"), Member.MaxHP);
	Sobe(TEXT("MP"), Member.MaxMP);
	Sobe(TEXT("AGL"), Member.AGL);
	Sobe(TEXT("ATK"), Member.ATK);
	Sobe(TEXT("UDF"), Member.UDF);
	Sobe(TEXT("LDF"), Member.LDF);
	Sobe(TEXT("SPD"), Member.SPD);
	Sobe(TEXT("INT"), Member.INT);

	// Subir de level NÃO cura no original: só o que o máximo cresceu é somado
	// ao atual, então quem estava ferido continua ferido.
	Member.HP = FMath::Min(Member.HP + (Member.MaxHP - HPAntes), Member.MaxHP);
	Member.MP = FMath::Min(Member.MP + (Member.MaxMP - MPAntes), Member.MaxMP);

	UE_LOG(LogTemp, Log, TEXT("PartySubsystem: %s subiu para LV%d (HP %d, ATK %d)."),
		*Member.Key.ToString(), Member.Level, Member.MaxHP, Member.ATK);

	OnMemberLevelUp.Broadcast(Member.Key, Member.Level);
}

bool UPartySubsystem::ResetMemberToBase(FJRPGPartyMember& Member)
{
	if (!CharacterTable)
	{
		return false;
	}
	const FCharacterData* Base =
		CharacterTable->FindRow<FCharacterData>(Member.Key, TEXT("ResetMemberToBase"), false);
	if (!Base)
	{
		return false;
	}

	Member.XPCurveSlot = Base->XPCurveSlot;
	Member.Level = 1;
	Member.ExperienceTotal = 0;

	// Em Legaia o atributo HP é o próprio máximo, e o personagem entra cheio.
	Member.MaxHP = Base->BaseHP;
	Member.HP = Base->BaseHP;
	Member.MaxMP = Base->BaseMP;
	Member.MP = Base->BaseMP;
	Member.AP = 0;
	Member.AGL = Base->BaseAGL;
	Member.ATK = Base->BaseATK;
	Member.UDF = Base->BaseUDF;
	Member.LDF = Base->BaseLDF;
	Member.SPD = Base->BaseSPD;
	Member.INT = Base->BaseINT;
	Member.SeruLevel = 1;

	// Um slot por EJRPGEquipSlot, todos vazios. O UStatusSubsystem conta com
	// o array já dimensionado.
	Member.Equipment.Init(NAME_None, static_cast<int32>(EJRPGEquipSlot::MAX));
	Member.Conditions.Empty();
	return true;
}

int32 UPartySubsystem::LevelUpMember(FName Character, int32 Levels)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	const UGameInstance* GI = GetGameInstance();
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!M || !Core || Levels <= 0)
	{
		return 0;
	}

	int32 Subiu = 0;
	while (Subiu < Levels && M->Level < Core->GetMaxLevel())
	{
		ApplyLevelUp(*M);
		++Subiu;
	}

	// O XP acompanha o level alcançado, senão a barra de "falta X" fica errada.
	M->ExperienceTotal = FMath::Max(M->ExperienceTotal,
		Core->GetXPForLevelForSlot(M->Level, M->XPCurveSlot));

	if (Subiu > 0)
	{
		OnPartyChanged.Broadcast();
	}
	return Subiu;
}

bool UPartySubsystem::SetMemberLevel(FName Character, int32 TargetLevel)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	const UGameInstance* GI = GetGameInstance();
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!M || !Core)
	{
		return false;
	}

	TargetLevel = FMath::Clamp(TargetLevel, 1, Core->GetMaxLevel());

	// Baixar exige refazer: o jitter dos levels já ganhos não tem como voltar.
	if (TargetLevel < M->Level)
	{
		const bool bAtivo = M->bActive;
		const bool bDisp = M->bAvailable;
		if (!ResetMemberToBase(*M))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("PartySubsystem: SetMemberLevel(%s) precisa de DT_Characters para refazer."),
				*Character.ToString());
			return false;
		}
		M->bActive = bAtivo;
		M->bAvailable = bDisp;
	}

	while (M->Level < TargetLevel)
	{
		ApplyLevelUp(*M);
	}

	M->ExperienceTotal = Core->GetXPForLevelForSlot(M->Level, M->XPCurveSlot);
	M->HP = M->MaxHP;
	M->MP = M->MaxMP;

	OnPartyChanged.Broadcast();
	return true;
}

bool UPartySubsystem::SetMemberStat(FName Character, FName Stat, int32 Value)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	const UGameInstance* GI = GetGameInstance();
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!M || !Core)
	{
		return false;
	}

	const int32 V = FMath::Clamp(Value, 0, Core->GetStatCap(Stat));

	// HP e MP escrevem o MÁXIMO; o atual é ajustado para não passar dele.
	if (Stat == TEXT("HP"))       { M->MaxHP = V; M->HP = FMath::Min(M->HP, V); }
	else if (Stat == TEXT("MP"))  { M->MaxMP = V; M->MP = FMath::Min(M->MP, V); }
	else if (Stat == TEXT("AP"))  { M->AP = FMath::Clamp(Value, 0, 100); }
	else if (Stat == TEXT("AGL")) { M->AGL = V; }
	else if (Stat == TEXT("ATK")) { M->ATK = V; }
	else if (Stat == TEXT("UDF")) { M->UDF = V; }
	else if (Stat == TEXT("LDF")) { M->LDF = V; }
	else if (Stat == TEXT("SPD")) { M->SPD = V; }
	else if (Stat == TEXT("INT")) { M->INT = V; }
	else
	{
		return false;
	}

	OnPartyChanged.Broadcast();
	return true;
}

bool UPartySubsystem::RemoveFromRoster(FName Character)
{
	if (Roster.Remove(Character) == 0)
	{
		return false;
	}
	ActiveOrder.Remove(Character);
	RebuildActiveOrder();
	OnPartyChanged.Broadcast();
	return true;
}

int32 UPartySubsystem::GetXPToNextLevel(FName Character) const
{
	const FJRPGPartyMember* M = Roster.Find(Character);
	const UGameInstance* GI = GetGameInstance();
	const UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!M || !Core || M->Level >= Core->GetMaxLevel())
	{
		return 0;
	}

	const int32 Limiar = Core->GetXPForLevelForSlot(M->Level + 1, M->XPCurveSlot);
	return FMath::Max(0, Limiar - M->ExperienceTotal);
}

// ============================================================
// HP / MP / AP
// ============================================================

void UPartySubsystem::ApplyDamage(FName Character, int32 Amount)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	if (!M || Amount <= 0)
	{
		return;
	}
	M->HP = FMath::Max(0, M->HP - Amount);
	if (M->HP == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("PartySubsystem: %s caiu."), *Character.ToString());
	}
	OnPartyChanged.Broadcast();
}

void UPartySubsystem::HealHP(FName Character, int32 Amount)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	if (!M || Amount <= 0 || M->IsDead())
	{
		return;
	}
	M->HP = FMath::Min(M->MaxHP, M->HP + Amount);
	OnPartyChanged.Broadcast();
}

void UPartySubsystem::RestoreMP(FName Character, int32 Amount)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	if (!M || Amount <= 0)
	{
		return;
	}
	M->MP = FMath::Min(M->MaxMP, M->MP + Amount);
	OnPartyChanged.Broadcast();
}

void UPartySubsystem::SetAP(FName Character, int32 NewAP)
{
	if (FJRPGPartyMember* M = Roster.Find(Character))
	{
		M->AP = FMath::Clamp(NewAP, 0, 100);
		OnPartyChanged.Broadcast();
	}
}

void UPartySubsystem::ReviveMember(FName Character, int32 WithHP)
{
	FJRPGPartyMember* M = Roster.Find(Character);
	if (!M || !M->IsDead())
	{
		return;
	}
	M->HP = FMath::Clamp(WithHP, 1, M->MaxHP);
	OnPartyChanged.Broadcast();
}

void UPartySubsystem::FullRestoreAll()
{
	for (TPair<FName, FJRPGPartyMember>& Par : Roster)
	{
		Par.Value.HP = Par.Value.MaxHP;
		Par.Value.MP = Par.Value.MaxMP;
	}
	OnPartyChanged.Broadcast();
}

// ============================================================
// ITENS
// ============================================================

void UPartySubsystem::ApplyConsumableEffect(FName CharacterName, const FItemData& ItemData)
{
	FJRPGPartyMember* M = Roster.Find(CharacterName);
	if (!M)
	{
		UE_LOG(LogTemp, Warning, TEXT("PartySubsystem: %s não está no roster."),
			*CharacterName.ToString());
		return;
	}

	if (ItemData.bRevive)
	{
		// Revive com metade do HP máximo, como é praxe no gênero.
		ReviveMember(CharacterName, FMath::Max(1, M->MaxHP / 2));
	}

	if (ItemData.HealHP > 0)
	{
		HealHP(CharacterName, ItemData.HealHP);
	}
	if (ItemData.HealMP > 0)
	{
		RestoreMP(CharacterName, ItemData.HealMP);
	}
	if (ItemData.bHealAP)
	{
		SetAP(CharacterName, 100);
	}
	if (ItemData.bCureStatus)
	{
		M->Conditions.Empty();
		OnPartyChanged.Broadcast();
	}
}

// ============================================================
// SAVE / NEW GAME
// ============================================================

void UPartySubsystem::ResetParty()
{
	Roster.Empty();
	ActiveOrder.Empty();
	OnPartyChanged.Broadcast();
}

void UPartySubsystem::LoadPartyState(const TArray<FJRPGPartyMember>& SavedRoster)
{
	Roster.Empty();
	ActiveOrder.Empty();

	for (const FJRPGPartyMember& M : SavedRoster)
	{
		if (!M.Key.IsNone())
		{
			Roster.Add(M.Key, M);
		}
	}

	RebuildActiveOrder();
	OnPartyChanged.Broadcast();
}

TArray<FJRPGPartyMember> UPartySubsystem::GetPartyStateForSave() const
{
	// A formação primeiro, na ordem, depois o resto — assim o save preserva a
	// ordem escolhida pelo jogador sem precisar de outro campo.
	TArray<FJRPGPartyMember> Out;
	for (const FName& Key : ActiveOrder)
	{
		if (const FJRPGPartyMember* M = Roster.Find(Key))
		{
			Out.Add(*M);
		}
	}
	for (const TPair<FName, FJRPGPartyMember>& Par : Roster)
	{
		if (!ActiveOrder.Contains(Par.Key))
		{
			Out.Add(Par.Value);
		}
	}
	return Out;
}
