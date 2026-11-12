#include "Battle/BattleSubsystem.h"
#include "Battle/JRPGAnimNotify_LoopWindow.h"
#include "Battle/BattleFormulas.h"
#include "Battle/EnemyData.h"
#include "Battle/HeroBattleData.h"
#include "Battle/JRPGAnimNotify_ChainCut.h"
#include "Battle/JRPGAnimNotify_Hit.h"
#include "Battle/JRPGBattleRules.h"
#include "Arts/ArtsSubsystem.h"
#include "Audio/JRPGSettingsSave.h"
#include "Arts/JRPGArtSequence.h"
#include "Core/CoreSubsystem.h"
#include "Inventory/InventorySubsystem.h"
#include "Party/CharacterData.h"
#include "Party/PartySubsystem.h"
#include "Status/StatusSubsystem.h"
#include "World/WorldStateSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** O custo de um botão no gauge quando nenhuma tabela diz outro (o 0x1E do original). */
	constexpr int32 DefaultButtonCost = UJRPGArtsRules::DefaultButtonCost;

	/**
	 * Tempo de um clip da party em quadros lógicos, na escala do passo (o clip é tocado a GameSpeed / 4 vezes o RateScale
	 * dele). O tempo do asset (float) vira o índice de quadro do clip UMA vez (a taxa de amostragem do próprio asset,
	 * arredondando ao quadro mais próximo); dali em diante é só inteiro, como UBattleFormulas::ClipFramesToSteps:
	 * ceil(quadros * 60 * 4 / (taxa * escala)). É leitura de dado do asset, não um notify disparando a lógica.
	 */
	int32 SecondsToSteps(double Seconds, const FFrameRate& ClipRate, int32 GameSpeed)
	{
		const int64 Speed = GameSpeed > 0 ? GameSpeed : UBattleFormulas::NormalGameSpeed;
		const int64 ClipFrames = ClipRate.AsFrameTime(Seconds).RoundToFrame().Value;
		if (ClipFrames <= 0 || ClipRate.Numerator <= 0 || ClipRate.Denominator <= 0)
		{
			return 0;
		}
		const int64 Divisor = static_cast<int64>(ClipRate.Numerator) * Speed;
		const int64 Steps = (ClipFrames * UBattleFormulas::StepsPerSecond * 4 * ClipRate.Denominator + Divisor - 1) / Divisor;
		return static_cast<int32>(FMath::Min<int64>(Steps, MAX_int32));
	}

	/** Um aviso do tempo dos clips, uma vez por chave na sessão (o ClipTiming roda a cada ação planejada). */
	void LogClipTimingOnce(const FString& Key, const FString& Message)
	{
		static TSet<FString> Logged;
		if (!Logged.Contains(Key))
		{
			Logged.Add(Key);
			UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: %s"), *Message);
		}
	}
}

UBattleSubsystem::UBattleSubsystem()
	: FTickableGameObject(ETickableTickType::Never)
{
	// A janela de reação dos heróis: o flinch do Vahn mede beat 3 / rate 1 no original (12 quadros)
	HeroFlinch.Beat = 3;
	HeroFlinch.Rate = 1;
	HeroBlock.Beat = 3;
	HeroBlock.Rate = 1;
	// O beat da entrada de flinch e de block (a mesma em cada herói) dos arquivos de batalha do disco: Vahn 3, Noa 2, Gala 4,
	// todas a rate 1 (as entradas 2, 3 e 0x0B de PROT 0863..0865). Quem não está na lista usa HeroFlinch / HeroBlock
	HeroReactionBeats.Add(TEXT("Vahn"), 3);
	HeroReactionBeats.Add(TEXT("Noa"), 2);
	HeroReactionBeats.Add(TEXT("Gala"), 4);
}

void UBattleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Um UObject só começa a ser tickado depois de construído (e nunca o CDO): ver o comentário de Tickable.h
	SetTickableTickType(ETickableTickType::Conditional);

	// A opção Battle Camera salva nas configurações (Normal sem o slot)
	if (const UJRPGSettingsSave* Settings = UJRPGSettingsSave::LoadOrCreate())
	{
		BattleCameraOption = static_cast<EJRPGBattleCameraOption>(FMath::Min<int32>(Settings->BattleCameraOption,
			static_cast<int32>(EJRPGBattleCameraOption::Far)));
	}
}

void UBattleSubsystem::SetBattleCameraOption(EJRPGBattleCameraOption Option)
{
	if (Option == BattleCameraOption)
	{
		return;
	}
	BattleCameraOption = Option;
	// Grava só este campo: o resto do "JRPGSettings" (os volumes) fica como está
	if (UJRPGSettingsSave* Settings = UJRPGSettingsSave::LoadOrCreate())
	{
		Settings->BattleCameraOption = static_cast<uint8>(Option);
		if (!UJRPGSettingsSave::Store(Settings))
		{
			UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: falha ao gravar a opção Battle Camera no JRPGSettings"));
		}
	}
}

void UBattleSubsystem::Deinitialize()
{
	SetTickableTickType(ETickableTickType::Never);
	Resolver.Halt();
	bInBattle = false;
	Super::Deinitialize();
}

void UBattleSubsystem::ApplyBattleItemEffect(FName CharacterName, const FItemData& ItemData)
{
	// TODO: Implementar lógica de aplicar buff de item temporário na batalha (Ex: Power Elixir)
	UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: Aplicando efeito temporario do item %s no combate para o personagem %s"), *ItemData.Name.ToString(), *CharacterName.ToString());
}

bool UBattleSubsystem::TryStartEncounter(const TArray<FJRPGEncounterCombatant>& Enemies)
{
	if (bInBattle)
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: encontro recusado (já há uma luta)"));
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (IsInEncounterGrace())
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: encontro recusado (carência: o último foi há %.1fs, o Field voltou há %.1fs)"),
			Now - LastEncounterTime, Now - (PostBattleGraceUntil - PostBattleGraceTime));
		return false;
	}
	if (Enemies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: encontro sem inimigos recusado — o encounter está vazio?"));
		return false;
	}

	LastEncounterTime = Now;
	UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: encontro com %d inimigo(s)"), Enemies.Num());
	OnEncounterStarted.Broadcast(Enemies);
	return true;
}

void UBattleSubsystem::ApplyHeroSwings(FJRPGCombatant& Hero, const UDataTable* Table, FName Weapon, FName Shoes, int32 RaSeruLevel)
{
	if (!Table || Table->GetRowStruct() != FJRPGHeroSwingData::StaticStruct())
	{
		return;
	}
	static const TCHAR* const ButtonNames[4] = { TEXT("Arms"), TEXT("RaSeru"), TEXT("High"), TEXT("Low") };
	static const FName DefaultItem(TEXT("default"));
	// O item que dirige cada botão, na ordem de EJRPGArtButton (Arms, Ra-Seru, High, Low). Sem o Ra-Seru (level 0) o braço dele é
	// um braço vazio, como a mão sem arma: a linha <Herói>_default_RaSeru
	const FName RaSeruItem = RaSeruLevel > 0 ? FName(*FString::Printf(TEXT("seru_%d"), RaSeruLevel)) : NAME_None;
	const FName Items[4] = { Weapon, RaSeruItem, Shoes, Shoes };

	Hero.SwingBytes.Init(0, 4);
	Hero.SwingProfiles.SetNum(4);
	if (Hero.ButtonCost.Num() < 4)
	{
		Hero.ButtonCost.Init(DefaultButtonCost, 4);
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto FindRow = [&](FName Item) -> const FJRPGHeroSwingData*
		{
			if (Item.IsNone())
			{
				return nullptr;
			}
			const FName RowKey(*FString::Printf(TEXT("%s_%s_%s"), *Hero.Key.ToString(), *Item.ToString(), ButtonNames[Index]));
			return Table->FindRow<FJRPGHeroSwingData>(RowKey, TEXT("BattleSnapshot"), false);
		};
		const FJRPGHeroSwingData* OwnRow = FindRow(Items[Index]);
		// O Ra-Seru acima do teto original do herói (Terra 8, Ozma 7; o remake vai até 10): o golpe do último tier que ele tem
		for (int32 Tier = RaSeruLevel - 1; !OwnRow && Index == static_cast<int32>(EJRPGArtButton::RaSeru) && Tier >= 1; --Tier)
		{
			OwnRow = FindRow(FName(*FString::Printf(TEXT("seru_%d"), Tier)));
		}
		const FJRPGHeroSwingData* Row = OwnRow ? OwnRow : FindRow(DefaultItem);
		if (!Row || Row->PowerBytes.IsEmpty())
		{
			continue;
		}
		FJRPGBattleSwingProfile& Profile = Hero.SwingProfiles[Index];
		Profile.PowerBytes = Row->PowerBytes;
		if (Row->HitBeats.Num() == Row->PowerBytes.Num())
		{
			Profile.Beats = Row->HitBeats;
			Profile.Rate = FMath::Max(Row->Rate, 1);
		}
		Hero.SwingBytes[Index] = Row->PowerBytes[0];
		// O custo vem do registro do próprio item; um item que a tabela não traz mantém o custo de antes (DT_WeaponCosts)
		if ((OwnRow || Items[Index].IsNone()) && Row->Cost > 0)
		{
			Hero.ButtonCost[Index] = Row->Cost;
		}
	}
}

bool UBattleSubsystem::IsInEncounterGrace() const
{
	const double Now = FPlatformTime::Seconds();
	return Now < PostBattleGraceUntil || Now - LastEncounterTime < EncounterGraceTime;
}

void UBattleSubsystem::NoteFieldReturned()
{
	// A carência de depois da luta conta daqui, de quando o Field aparece de novo (o fim da luta vem ~2 s antes: a queda, o
	// fade), e não do EndBattle: senão o inimigo ao lado do jogador encostava logo que a tela abria
	PostBattleGraceUntil = FPlatformTime::Seconds() + PostBattleGraceTime;
}

bool UBattleSubsystem::ConsumeEncounterStep(int32 Rate)
{
	if (EncounterCounter < 0)
	{
		EncounterCounter = UJRPGEncounterLibrary::RerollEncounterCounter(FMath::Rand(), FMath::Rand());
	}
	if (!UJRPGEncounterLibrary::StepEncounterCounter(EncounterCounter, Rate))
	{
		return false;
	}
	// A luta sai: o contador volta a 488..1460 (os dois sorteios só no gatilho, como no original)
	EncounterCounter = UJRPGEncounterLibrary::RerollEncounterCounter(FMath::Rand(), FMath::Rand());
	return true;
}

void UBattleSubsystem::TopUpEncounterCounterOnMapEntry()
{
	EncounterCounter = EncounterCounter < 0
		? UJRPGEncounterLibrary::RerollEncounterCounter(FMath::Rand(), FMath::Rand())
		: UJRPGEncounterLibrary::TopUpEncounterCounter(EncounterCounter, FMath::Rand(), FMath::Rand());
}

// =================================================================================================
// A foto
// =================================================================================================

FJRPGCombatant UBattleSubsystem::MakeEnemyCombatant(const FJRPGEnemyData& Row, const TArray<FJRPGEnemyActionData>& Actions, int32 Level,
	bool bScriptedFight, EJRPGRuleset Ruleset, const UCoreSubsystem* Core)
{
	FJRPGCombatant C;
	C.Side = EJRPGBattleSide::Enemy;
	C.Type = EJRPGCombatantType::Enemy;
	C.Team = 1;
	C.OwnerPlayer = -1;
	C.Key = Row.Key;
	C.Level = Level;

	// O registro cru pelas regras da versão (o boost da USA, nada na japonesa) e, por cima, a dificuldade
	FJRPGEnemyBattleStats Stats = UJRPGBattleRules::InstallEnemyStats(Row, bScriptedFight, Ruleset);
	if (Core)
	{
		Stats.HP = Core->ScaleEnemyHP(Stats.HP);
		Stats.ATK = Core->ScaleEnemyATK(Stats.ATK);
		Stats.UDF = Core->ScaleEnemyDEF(Stats.UDF);
		Stats.LDF = Core->ScaleEnemyDEF(Stats.LDF);
	}
	C.HP = C.MaxHP = Stats.HP;
	C.MP = C.MaxMP = Stats.MP;
	C.ATK = Stats.ATK;
	C.UDF = Stats.UDF;
	C.LDF = Stats.LDF;
	C.SPD = Stats.SPD;
	C.INT = Stats.INT;
	C.AGL = Stats.AGL;
	C.Element = Row.Element;
	C.TargetClass = Row.TargetClass;
	C.SizeClass = Row.SizeClass;
	C.AP = 0;
	C.MaxAP = 0;
	C.MaxCommandGauge = Stats.AGL;
	C.CommandGauge = Stats.AGL;

	// As ações do monstro na ordem do arquivo: o block, o levantar, os dois flinch (que dão a janela do juggle) e os
	// golpes que a IA sorteia
	TArray<const FJRPGEnemyActionData*> Sorted;
	for (const FJRPGEnemyActionData& Action : Actions)
	{
		Sorted.Add(&Action);
	}
	Sorted.Sort([](const FJRPGEnemyActionData& A, const FJRPGEnemyActionData& B) { return A.ActionIndex < B.ActionIndex; });
	for (const FJRPGEnemyActionData* Action : Sorted)
	{
		const int32 FirstBeat = Action->HitFrames.IsValidIndex(0) ? Action->HitFrames[0] : 0;
		// O comprimento do clip em quadros lógicos na escala 8 (4 * N / R): a espera da próxima ação pela reação (dado do disco,
		// não o clip tocado). Sem o tamanho do clip, 0: vale o padrão do resolvedor
		const int32 ClipFrames = UBattleFormulas::ClipFramesToSteps(Action->FrameCount, UBattleFormulas::NormalGameSpeed, FMath::Max(Action->Rate, 1));
		switch (Action->Tag)
		{
		case 0x0B:
			C.bHasBlock = true;
			C.BlockClip.Beat = FirstBeat;
			C.BlockClip.Rate = FMath::Max(Action->Rate, 1);
			C.BlockClip.FrameCount = Action->FrameCount;
			C.Reactions.BlockFrames = ClipFrames;
			break;
		case 5:
			C.bHasGetUp = true;
			C.Reactions.GetUpFrames = ClipFrames;
			break;
		case 4:
			C.Reactions.KnockdownFrames = ClipFrames;
			break;
		case 2:
			C.FlinchHigh.Beat = FirstBeat;
			C.FlinchHigh.Rate = FMath::Max(Action->Rate, 1);
			C.FlinchHigh.FrameCount = Action->FrameCount;
			C.Reactions.FlinchHighFrames = ClipFrames;
			break;
		case 3:
			C.FlinchLow.Beat = FirstBeat;
			C.FlinchLow.Rate = FMath::Max(Action->Rate, 1);
			C.FlinchLow.FrameCount = Action->FrameCount;
			C.Reactions.FlinchLowFrames = ClipFrames;
			break;
		default:
			break;
		}
		if (Action->IsSwing())
		{
			FJRPGBattleActionEntry& Swing = C.Swings.AddDefaulted_GetRef();
			Swing.ActionIndex = Action->ActionIndex;
			Swing.Tag = Action->Tag;
			Swing.Power = Action->Power;
			Swing.HitFrames = Action->HitFrames;
			Swing.AGLCost = Action->AGLCost;
			Swing.Gate = Action->Gate;
			Swing.Rate = FMath::Max(Action->Rate, 1);
			Swing.FrameCount = Action->FrameCount;
			Swing.Clip = FName(*Action->Clip);
		}
	}
	return C;
}

FJRPGCombatant UBattleSubsystem::MakeHeroCombatant(FName Key, int32 SideIndex) const
{
	FJRPGCombatant C;
	C.Side = EJRPGBattleSide::Party;
	C.SideIndex = SideIndex;
	C.Type = EJRPGCombatantType::Character;
	C.Team = 0;
	C.OwnerPlayer = 0;   // o jogo local: o jogador 0 digita os comandos de toda a party
	C.Key = Key;
	C.MaxAP = 100;

	UGameInstance* GI = GetGameInstance();
	const UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	const UStatusSubsystem* Status = GI ? GI->GetSubsystem<UStatusSubsystem>() : nullptr;
	const UInventorySubsystem* Inventory = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	const UArtsSubsystem* Arts = GI ? GI->GetSubsystem<UArtsSubsystem>() : nullptr;

	FJRPGPartyMember Member;
	const bool bInRoster = Party && Party->GetMember(Key, Member);
	const UDataTable* Characters = UCoreSubsystem::ResolveDataTable(TEXT("DT_Characters"));
	const FCharacterData* Row = Characters ? Characters->FindRow<FCharacterData>(Key, TEXT("BattleSnapshot"), false) : nullptr;

	if (bInRoster)
	{
		C.Level = Member.Level;
		C.HP = Member.HP;
		C.MaxHP = Member.MaxHP;
		C.MP = Member.MP;
		C.MaxMP = Member.MaxMP;
		C.AP = Member.AP;
		// O ATK é o BASE: a dobra do equipamento é por golpe (UBattleFormulas::EquipmentFold)
		C.ATK = Member.ATK;
		// Os outros saem do valor efetivo (equipamento e Ra-Seru já dobrados na carga, como o FUN_80053CB8)
		auto Effective = [&](const TCHAR* Stat, int32 Base)
		{
			const int32 Value = Status ? Status->GetEffectiveStat(Key, FName(Stat)) : 0;
			return Value > 0 ? Value : Base;
		};
		C.UDF = Effective(TEXT("UDF"), Member.UDF);
		C.LDF = Effective(TEXT("LDF"), Member.LDF);
		C.SPD = Effective(TEXT("SPD"), Member.SPD);
		C.INT = Effective(TEXT("INT"), Member.INT);
		C.AGL = Effective(TEXT("AGL"), Member.AGL);
	}
	else if (Row)
	{
		// PIE direto num mapa, sem new game: os atributos de nível 1 do DT_Characters
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: %s não está no roster — entra com os atributos iniciais do DT_Characters"), *Key.ToString());
		C.HP = C.MaxHP = Row->BaseHP;
		C.MP = C.MaxMP = Row->BaseMP;
		C.ATK = Row->BaseATK;
		C.UDF = Row->BaseUDF;
		C.LDF = Row->BaseLDF;
		C.SPD = Row->BaseSPD;
		C.INT = Row->BaseINT;
		C.AGL = Row->BaseAGL;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BattleSubsystem: %s não está no roster nem no DT_Characters — entra sem atributos"), *Key.ToString());
	}
	C.MaxCommandGauge = C.AGL;
	C.CommandGauge = C.AGL;
	// O Ra-Seru: 0 = ainda não tem (o começo do jogo; fora do roster também). O elemento do herói é o dele
	const int32 RaSeruLevel = bInRoster ? Member.RaSeruLevel : 0;
	C.Element = Row && RaSeruLevel > 0 ? Row->RaSeruElement : EJRPGElement::None;
	C.bLeftHanded = Row && Row->bLeftHanded;

	// As cinco peças, cada uma com o ATK do equipamento do papel dela (o Ra-Seru é o do tier; sem ele, 0)
	C.PieceATK.Init(0, UBattleFormulas::PieceCount);
	auto EquippedAt = [&](EJRPGEquipSlot Slot) -> FName
	{
		return bInRoster && Member.Equipment.IsValidIndex(static_cast<int32>(Slot)) ? Member.Equipment[static_cast<int32>(Slot)] : NAME_None;
	};
	// O que a tela de comando mostra em cada braço: Arms com arma, Ra-Seru com ele, senão Left / Right
	C.bHasWeapon = !EquippedAt(EJRPGEquipSlot::Weapon).IsNone();
	if (Inventory)
	{
		auto SlotATK = [&](EJRPGEquipSlot Slot)
		{
			FItemData Item;
			const FName ItemKey = EquippedAt(Slot);
			return !ItemKey.IsNone() && Inventory->GetItemData(ItemKey, Item) ? Item.AttackBonus : 0;
		};
		C.PieceATK[UBattleFormulas::PieceWeapon] = SlotATK(EJRPGEquipSlot::Weapon);
		C.PieceATK[UBattleFormulas::PieceShoes] = SlotATK(EJRPGEquipSlot::Shoes);
		C.PieceATK[UBattleFormulas::PieceBody] = SlotATK(EJRPGEquipSlot::Armor);
		C.PieceATK[UBattleFormulas::PieceHead] = SlotATK(EJRPGEquipSlot::Helmet);
	}
	if (const UDataTable* Tiers = UCoreSubsystem::ResolveDataTable(TEXT("DT_RaSeruTiers")))
	{
		const FJRPGRaSeruTierData* Tier = Tiers->FindRow<FJRPGRaSeruTierData>(Key, TEXT("BattleSnapshot"), false);
		if (Tier && !Tier->TierATK.IsEmpty() && RaSeruLevel > 0)
		{
			C.PieceATK[UBattleFormulas::PieceRaSeru] = Tier->TierATK[FMath::Clamp(RaSeruLevel - 1, 0, Tier->TierATK.Num() - 1)];
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: DT_RaSeruTiers não encontrada — o Ra-Seru entra com 0 de ATK (Docs/Guides/ENEMIES.md, seção 6)"));
	}

	// O custo de cada botão no gauge: 30, e o botão de Arms por arma (42 / 54 fora da classe favorita)
	C.ButtonCost.Init(DefaultButtonCost, 4);
	if (const UDataTable* Costs = UCoreSubsystem::ResolveDataTable(TEXT("DT_WeaponCosts")))
	{
		const FName Weapon = EquippedAt(EJRPGEquipSlot::Weapon);
		if (!Weapon.IsNone())
		{
			const FName RowKey(*FString::Printf(TEXT("%s_%s"), *Key.ToString(), *Weapon.ToString()));
			if (const FJRPGWeaponCostData* Cost = Costs->FindRow<FJRPGWeaponCostData>(RowKey, TEXT("BattleSnapshot"), false))
			{
				C.ButtonCost[static_cast<int32>(EJRPGArtButton::Arms)] = Cost->ArmsCost;
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: DT_WeaponCosts não encontrada — todo botão custa %d"), DefaultButtonCost);
	}

	// O golpe simples de cada botão (byte de poder, quadro de golpe, custo) do que ele tem equipado; sem a tabela, o byte
	// padrão do herói e o quadro padrão da timeline, como antes
	if (const UDataTable* Swings = UCoreSubsystem::ResolveDataTable(TEXT("DT_HeroSwings")))
	{
		ApplyHeroSwings(C, Swings, EquippedAt(EJRPGEquipSlot::Weapon), EquippedAt(EJRPGEquipSlot::Shoes), RaSeruLevel);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: DT_HeroSwings não encontrada — golpes simples com o byte e o quadro padrão (Docs/Guides/BATTLE.md)"));
	}

	// Quanto duram as reações dele (flinch, block, knockdown + levantar, morrer): é o que a luta espera depois de um golpe antes da
	// próxima ação. Sem a tabela ou sem a linha, 0 em tudo e valem os números do Vahn do resolvedor (FJRPGTimelineDefaults)
	if (const UDataTable* ReactionTable = UCoreSubsystem::ResolveDataTable(TEXT("DT_HeroReactions")))
	{
		if (const FJRPGHeroReactionData* ReactionRow = ReactionTable->FindRow<FJRPGHeroReactionData>(Key, TEXT("BattleSnapshot"), false))
		{
			C.Reactions.FlinchHighFrames = ReactionRow->FlinchHighFrames;
			C.Reactions.FlinchLowFrames = ReactionRow->FlinchLowFrames;
			C.Reactions.BlockFrames = ReactionRow->BlockFrames;
			C.Reactions.KnockdownFrames = ReactionRow->KnockdownFrames;
			C.Reactions.GetUpFrames = ReactionRow->GetUpFrames;
			C.Reactions.DyingFrames = ReactionRow->DyingFrames;
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: %s não tem linha na DT_HeroReactions — espera pelas reações com os números do Vahn"), *Key.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: DT_HeroReactions não encontrada — espera pelas reações com os números do Vahn (Docs/BATTLE_PHASE5_BUGS.md)"));
	}

	// As Arts: a lista que ele sabe, o Ra-Seru, a chance de aprender no uso (a foto das regras das Arts)
	if (Arts)
	{
		const FJRPGArtsContext Context = Arts->MakeContext(Key);
		C.KnownArts = Context.KnownArts;
		C.bHasRaSeru = Context.bHasRaSeru;
		C.LearnOnUseChance = Context.LearnOnUseChance;
	}
	else
	{
		C.KnownArts = bInRoster ? Member.LearnedArts : TArray<FName>();
		C.bHasRaSeru = Row && !Row->RaSeru.IsNone() && RaSeruLevel > 0;
	}

	// Os heróis têm os clips de block e de levantar; a janela de reação é a do original (Vahn: beat 3 / rate 1)
	C.bHasBlock = true;
	C.bHasGetUp = true;
	FJRPGBattleReactionClip Flinch = HeroFlinch;
	FJRPGBattleReactionClip BlockWindow = HeroBlock;
	if (const int32* Beat = HeroReactionBeats.Find(Key))
	{
		Flinch.Beat = *Beat;
		BlockWindow.Beat = *Beat;
	}
	C.FlinchHigh = Flinch;
	C.FlinchLow = Flinch;
	C.BlockClip = BlockWindow;
	return C;
}

bool UBattleSubsystem::BuildSnapshot(const FJRPGBattleSetup& Setup, TArray<FJRPGCombatant>& OutCombatants) const
{
	OutCombatants.Reset();
	for (int32 i = 0; i < Setup.Party.Num(); ++i)
	{
		OutCombatants.Add(MakeHeroCombatant(Setup.Party[i], i));
	}

	UGameInstance* GI = GetGameInstance();
	const UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	const EJRPGRuleset Ruleset = Core ? Core->GetRuleset() : EJRPGRuleset::USA;

	// As linhas do DT_EnemyActions de quem entra na luta
	TSet<FName> Needed;
	for (const FJRPGEncounterCombatant& Enemy : Setup.Enemies)
	{
		Needed.Add(Enemy.EnemyKey);
	}
	TMap<FName, TArray<FJRPGEnemyActionData>> ActionsByEnemy;
	if (const UDataTable* Actions = UCoreSubsystem::ResolveDataTable(TEXT("DT_EnemyActions")))
	{
		Actions->ForeachRow<FJRPGEnemyActionData>(TEXT("BattleSnapshot"), [&](const FName&, const FJRPGEnemyActionData& Row)
		{
			if (Needed.Contains(Row.Enemy))
			{
				ActionsByEnemy.FindOrAdd(Row.Enemy).Add(Row);
			}
		});
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: DT_EnemyActions não encontrada — os inimigos batem com o golpe padrão (Docs/Guides/ENEMIES.md, seção 5)"));
	}

	for (int32 i = 0; i < Setup.Enemies.Num(); ++i)
	{
		const FJRPGEncounterCombatant& Enemy = Setup.Enemies[i];
		FJRPGEnemyData Row;
		if (!UJRPGEncounterLibrary::GetEnemyData(Enemy.EnemyKey, Row))
		{
			UE_LOG(LogTemp, Error, TEXT("BattleSubsystem: o inimigo %s não está no DT_Enemies — entra como um placeholder de 1 HP"), *Enemy.EnemyKey.ToString());
			Row.Key = Enemy.EnemyKey;
			Row.BaseHP = 1;
		}
		FJRPGCombatant C = MakeEnemyCombatant(Row, ActionsByEnemy.FindRef(Enemy.EnemyKey), Enemy.Level, Setup.bScriptedFight, Ruleset, Core);
		C.SideIndex = i;
		OutCombatants.Add(C);
	}
	for (int32 Slot = 0; Slot < OutCombatants.Num(); ++Slot)
	{
		OutCombatants[Slot].SlotIndex = Slot;
	}
	return true;
}

// =================================================================================================
// Começo e fim
// =================================================================================================

bool UBattleSubsystem::BeginBattle(const FJRPGBattleSetup& Setup)
{
	if (bInBattle)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: BeginBattle com uma luta em andamento — ignorado"));
		return false;
	}
	if (Setup.Enemies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: BeginBattle sem inimigos — ignorado"));
		return false;
	}

	TArray<FName> Party = Setup.Party;
	if (Party.Num() == 0)
	{
		if (const UPartySubsystem* PartySubsystem = GetGameInstance()->GetSubsystem<UPartySubsystem>())
		{
			Party = PartySubsystem->GetActiveParty();
		}
	}
	if (Party.Num() == 0)
	{
		Party = FallbackParty;
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: nenhuma party ativa — usando a FallbackParty (%d)"), Party.Num());
	}
	if (Party.Num() > MaxPartySlots)
	{
		Party.SetNum(MaxPartySlots);
	}

	CurrentSetup = Setup;
	CurrentSetup.Party = Party;
	if (CurrentSetup.Enemies.Num() > MaxEnemySlots)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: %d inimigos, MaxEnemySlots %d — os de sobra ficam de fora"), CurrentSetup.Enemies.Num(), MaxEnemySlots);
		CurrentSetup.Enemies.SetNum(MaxEnemySlots);
	}
	// A semente é sorteada aqui, fora das regras (como o sorteio do encontro), e guardada: a luta pode ser repetida
	if (CurrentSetup.Seed == 0)
	{
		CurrentSetup.Seed = FMath::RandRange(1, MAX_int32 - 1);
	}

	TArray<FJRPGCombatant> Combatants;
	BuildSnapshot(CurrentSetup, Combatants);

	// As regras: o DT_Arts, os quadros padrão e o tempo dos clips da party (lido do asset como dado)
	ArtsTable = UCoreSubsystem::ResolveDataTable(TEXT("DT_Arts"));
	if (!ArtsTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: DT_Arts não encontrada — toda entrada de Arts vira golpe simples"));
	}
	Resolver.ArtsTable = ArtsTable;
	// PvP: TimelineDefaults (editável) e os tempos lidos dos clips abaixo entram nas contas da linha do tempo, então
	// fazem parte do SETUP DA PARTIDA — hoje cada máquina lê os dela; no PvP têm de viajar com a semente
	// (Docs/BATTLE_ROADMAP.md, "door open").
	Resolver.Defaults = TimelineDefaults;
	// PvP: o primeiro golpe de cada ação espera a aproximação (as duas máquinas com a mesma flag)
	Resolver.bRequireApproach = bWaitForApproachInput;
	// Os quadros de golpe das Arts, do disco: os clips das Arts não têm UJRPGAnimNotify_Hit, e sem eles os golpes caíam no
	// espaçamento padrão, fora do clip (a reação do alvo vinha atrasada)
	ArtHitsTable = UCoreSubsystem::ResolveDataTable(TEXT("DT_ArtHits"));
	if (!ArtHitsTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: DT_ArtHits não encontrada — os golpes das Arts sem marcadores no clip usam o espaçamento padrão"));
	}
	const UDataTable* ArtHits = ArtHitsTable;
	Resolver.ClipTiming = [ArtHits](FName Hero, const FJRPGClipQuery& Query, FJRPGClipTiming& OutTiming) -> bool
	{
		UAnimSequence* Clip = nullptr;
		if (Query.Kind == EJRPGActionStepKind::Art)
		{
			if (const FJRPGArtData* Row = UJRPGArtSequenceLibrary::FindArt(Query.ArtKey))
			{
				Clip = Row->Anim.LoadSynchronous();
			}
		}
		else
		{
			Clip = UJRPGArtSequenceLibrary::LoadHeroClip(Hero, Query.ClipName.ToString());
		}
		if (!Clip)
		{
			return false;
		}
		const FFrameRate ClipRate = Clip->GetSamplingFrameRate();
		// O clip mais as voltas do trecho de laço dele (UJRPGAnimNotify_LoopWindow: o "agachado" do SpecialStarter do Vahn repete
		// 5 vezes); o 255 (até trocar de clip, as poses de vitória) não conta aqui
		double PlaySeconds = Clip->GetPlayLength();
		float LoopStart = 0.0f;
		float LoopEnd = 0.0f;
		int32 Loops = 0;
		if (UJRPGAnimNotify_LoopWindow::FindLoopWindow(Clip, LoopStart, LoopEnd, Loops) && Loops < UJRPGAnimNotify_LoopWindow::Forever)
		{
			PlaySeconds += static_cast<double>(Loops) * static_cast<double>(LoopEnd - LoopStart);
		}
		OutTiming.Frames = SecondsToSteps(PlaySeconds, ClipRate, Query.GameSpeed);
		const TArray<float> Hits = UJRPGAnimNotify_Hit::FindHitTimes(Clip);
		if (Hits.Num() == Query.ExpectedHits)
		{
			for (const float Time : Hits)
			{
				OutTiming.HitFrames.Add(SecondsToSteps(Time, ClipRate, Query.GameSpeed));
			}
		}
		else
		{
			// Bug 29: marcadores de golpe em quantidade diferente da que a ação espera são ignorados em silêncio; agora avisa
			if (!Hits.IsEmpty())
			{
				LogClipTimingOnce(Clip->GetPathName() + TEXT("#notifies"), FString::Printf(
					TEXT("o clip %s tem %d marcador(es) de golpe (UJRPGAnimNotify_Hit), mas a ação espera %d: os marcadores são ignorados"),
					*Clip->GetName(), Hits.Num(), Query.ExpectedHits));
			}
			if (Query.Kind == EJRPGActionStepKind::Art && ArtHits)
			{
				// Sem marcadores no clip: os quadros de golpe do registro da Art no disco, na conta do golpe simples
				const FJRPGArtHitData* Row = ArtHits->FindRow<FJRPGArtHitData>(Query.ArtKey, TEXT("ArtHits"), false);
				if (Row && Row->HitBeats.Num() == Query.ExpectedHits && Query.ExpectedHits > 0)
				{
					const int32 Rate = FMath::Max(Row->Rate, 1);
					// O asset vem do .glb do RE, assado a 7,5 x rate quadros do disco por segundo: o quadro B - 1 do disco cai em
					// (B - 1) / (7,5 x rate) segundos do asset, e a duração do asset é um número inteiro de quadros do disco
					const double DiscFramesPerSecond = 7.5 * static_cast<double>(Rate);
					const double AssetDiscFrames = static_cast<double>(Clip->GetPlayLength()) * DiscFramesPerSecond;
					// A importação arredonda o clip para cima até um quadro inteiro da amostragem (30 fps): o HurricaneKick3 de 1,111 s
					// vira 1,133 s. Só avisa quando nenhum número inteiro de quadros do disco cabe nesse último quadro (bug 73)
					const double SampleRate = ClipRate.AsDecimal() > 0.0 ? ClipRate.AsDecimal() : 30.0;
					const double ShortestDiscFrames = (static_cast<double>(Clip->GetPlayLength()) - 1.0 / SampleRate) * DiscFramesPerSecond;
					if (FMath::FloorToDouble(AssetDiscFrames + 0.2) < ShortestDiscFrames - 0.2)
					{
						LogClipTimingOnce(Clip->GetPathName() + TEXT("#rate"), FString::Printf(
							TEXT("a Art %s: o clip %s dura %.3f s, %.2f quadros do disco no rate %d (deveria ser inteiro): o asset parece assado em outro rate, e os golpes do DT_ArtHits caem fora do impacto que se vê"),
							*Query.ArtKey.ToString(), *Clip->GetName(), Clip->GetPlayLength(), AssetDiscFrames, Rate));
					}
					for (int32 Index = 0; Index < Row->HitBeats.Num(); ++Index)
					{
						const int32 Beat = Row->HitBeats[Index];
						const int32 Frame = UBattleFormulas::ClipFramesToSteps(FMath::Max(Beat - 1, 0), Query.GameSpeed, Rate);
						OutTiming.HitFrames.Add(Frame);
						// O clip nunca acaba antes do último golpe dele
						OutTiming.Frames = FMath::Max(OutTiming.Frames, Frame + 1);
						const double HitSeconds = static_cast<double>(FMath::Max(Beat - 1, 0)) / DiscFramesPerSecond;
						if (HitSeconds > static_cast<double>(Clip->GetPlayLength()) + 1.0 / 30.0)
						{
							LogClipTimingOnce(Clip->GetPathName() + FString::Printf(TEXT("#hit%d"), Index), FString::Printf(
								TEXT("a Art %s: o golpe %d (quadro %d do disco) cai em %.3f s, depois do fim do clip %s (%.3f s): o asset é mais curto que o clip do disco, o golpe toca sem o corpo"),
								*Query.ArtKey.ToString(), Index + 1, Beat, HitSeconds, *Clip->GetName(), Clip->GetPlayLength()));
						}
					}
				}
				else if (Row && Query.ExpectedHits > 0)
				{
					LogClipTimingOnce(Query.ArtKey.ToString() + TEXT("#count"), FString::Printf(
						TEXT("a Art %s: o DT_ArtHits tem %d golpe(s), o DT_Arts (HitPower) %d: os golpes ficam espaçados pelo padrão, fora do clip"),
						*Query.ArtKey.ToString(), Row->HitBeats.Num(), Query.ExpectedHits));
				}
			}
		}
		// O trecho de laço nos golpes (bug 83): o cursor que chega ao fim do trecho volta ao começo dele, Loops vezes, então os golpes
		// depois do trecho atrasam as voltas (o Burning Flare segura um quadro 5 vezes antes dos golpes 11..14). Os golpes do trecho não
		// se repetem: os finalizadores das Supers, que repetiriam, não têm o trecho (o original não repete; ps1_party_sounds.py)
		if (Query.Kind == EJRPGActionStepKind::Art && Loops > 0 && Loops < UJRPGAnimNotify_LoopWindow::Forever && LoopEnd > LoopStart)
		{
			const int32 WindowEnd = SecondsToSteps(LoopEnd, ClipRate, Query.GameSpeed);
			const int32 WindowSteps = FMath::Max(WindowEnd - SecondsToSteps(LoopStart, ClipRate, Query.GameSpeed), 1);
			for (int32& Frame : OutTiming.HitFrames)
			{
				if (Frame >= WindowEnd)
				{
					Frame += Loops * WindowSteps;
				}
			}
			if (!OutTiming.HitFrames.IsEmpty())
			{
				OutTiming.Frames = FMath::Max(OutTiming.Frames, OutTiming.HitFrames.Last() + 1);
			}
		}
		const float Cut = UJRPGAnimNotify_ChainCut::FindCutTime(Clip);
		if (Cut >= 0.0f)
		{
			OutTiming.CutFrame = SecondsToSteps(Cut, ClipRate, Query.GameSpeed);
		}
		return true;
	};

	bInBattle = true;
	// Toda luta (aleatória, inimigo do Field, de script) sorteia de novo o contador dos encontros aleatórios: senão, depois de uma
	// luta por toque, um passo no Field já podia disparar outra (bug 91). Cosmético, fora do Rng da luta
	EncounterCounter = UJRPGEncounterLibrary::RerollEncounterCounter(FMath::Rand(), FMath::Rand());
	bLogicHeld = false;
	Accumulator = 0.0;
	FlushedEvents = 0;
	FramesSinceFinish = 0;
	bVictorySettled = false;
	VictoryResults = FJRPGVictoryResults();
	bVictoryEndHeld = false;
	bEndVictoryNow = false;
	PresentationFrame = 0;
	PresentationOffset = 0;
	ResolverStallFrames = 0;
	LastNotifiedSlot = -2;
	LastNotifiedStage = EJRPGBattleStage::None;
	LastEncounterTime = FPlatformTime::Seconds();
	if (UWorldStateSubsystem* WorldState = GetGameInstance()->GetSubsystem<UWorldStateSubsystem>())
	{
		bWasInField = WorldState->IsInField();
		WorldState->SetIsInField(false);
	}

	UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: luta começou — %d herói(s) contra %d inimigo(s), semente %d%s"), Party.Num(),
		CurrentSetup.Enemies.Num(), CurrentSetup.Seed, CurrentSetup.bScriptedFight ? TEXT(", luta roteirizada") : TEXT(""));

	// O ataque pelas costas forçado do sorteio: o monstro 0xA7 em qualquer lugar (FUN_80051D84), ou a flag da luta (as abelhas
	// 0x3D..0x3F nos mapas 0x0C / 0x15, que só quem monta a luta sabe)
	bool bForcedBackAttack = CurrentSetup.bForceBackAttack;
	for (const FJRPGEncounterCombatant& Enemy : CurrentSetup.Enemies)
	{
		FJRPGEnemyData Row;
		if (!bForcedBackAttack && UJRPGEncounterLibrary::GetEnemyData(Enemy.EnemyKey, Row) && Row.MonsterArchiveID == ForcedBackAttackMonsterID)
		{
			bForcedBackAttack = true;
		}
	}

	// A rodada 1 abre aqui (RoundStart); os comandos esperam o jogador e nada anda até alguém comandar
	Resolver.Begin(Combatants, CurrentSetup.Seed, CurrentSetup.bScriptedFight, CurrentSetup.FormationAdvantage, bForcedBackAttack);
	if (Resolver.GetState().FormationAdvantage != 0)
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: %s"), Resolver.GetState().FormationAdvantage == 1
			? TEXT("ataque pelas costas — a party não comanda na rodada 1") : TEXT("ataque surpresa — os inimigos perdem a rodada 1"));
	}
	OnBattleStarted.Broadcast(CurrentSetup, Resolver.GetCombatants());
	FlushEvents();
	return true;
}

int32 UBattleSubsystem::ParseFormationAdvantage(const FString& Word)
{
	const FString Lower = Word.ToLower();
	if (Lower == TEXT("none") || Lower == TEXT("0"))
	{
		return 0;
	}
	if (Lower == TEXT("back") || Lower == TEXT("1"))
	{
		return 1;
	}
	if (Lower == TEXT("first") || Lower == TEXT("2"))
	{
		return 2;
	}
	return -1;
}

EJRPGBattleTransitionStyle UBattleSubsystem::ParseTransitionStyle(const FString& Word, int32& OutSub)
{
	FString Lower = Word.ToLower();
	OutSub = -1;
	if (Lower.Len() > 1 && FChar::IsDigit(Lower[Lower.Len() - 1]))
	{
		OutSub = Lower[Lower.Len() - 1] - TEXT('0');
		Lower.LeftChopInline(1);
	}
	if (Lower == TEXT("scatter")) { return EJRPGBattleTransitionStyle::Scatter; }
	if (Lower == TEXT("spinup"))  { return EJRPGBattleTransitionStyle::SpinUp; }
	if (Lower == TEXT("shatter")) { return EJRPGBattleTransitionStyle::Shatter; }
	if (Lower == TEXT("curtain")) { return EJRPGBattleTransitionStyle::Curtain; }
	if (Lower == TEXT("swirl"))   { return EJRPGBattleTransitionStyle::Swirl; }
	if (Lower == TEXT("fade"))    { return EJRPGBattleTransitionStyle::Fade; }
	if (Lower == TEXT("cut"))     { return EJRPGBattleTransitionStyle::Cut; }
	return EJRPGBattleTransitionStyle::Auto;
}

void UBattleSubsystem::EndBattle(EJRPGBattleResult Result)
{
	if (!bInBattle)
	{
		return;
	}
	// Primeiro de tudo: uma segunda chamada (a vitória que chega duas vezes, o EndBattle de dentro de um listener) não faz nada
	bInBattle = false;
	Resolver.Halt();
	// O jogador volta ao Field em cima de onde a luta começou: sem a carência, o inimigo do lado
	// começaria outra na mesma hora
	LastEncounterTime = FPlatformTime::Seconds();
	if (bWasInField)
	{
		if (UWorldStateSubsystem* WorldState = GetGameInstance()->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->SetIsInField(true);
		}
	}

	// A vitória já escrita e paga no quadro dos resultados (SettleVictory) não repete nada
	if (!(Result == EJRPGBattleResult::Victory && bVictorySettled))
	{
		WriteBack(Result);
		if (Result == EJRPGBattleResult::Victory)
		{
			PayRewards();
		}
	}
	bVictoryEndHeld = false;
	bEndVictoryNow = false;

	UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: luta acabou (%s)"), *StaticEnum<EJRPGBattleResult>()->GetNameStringByValue(static_cast<int64>(Result)));
	OnBattleEnded.Broadcast(Result);
}

void UBattleSubsystem::EndBattleForDebug(EJRPGBattleResult Result, bool bPayEveryEnemy)
{
	bDebugPayEveryEnemy = bPayEveryEnemy;
	EndBattle(Result);
	bDebugPayEveryEnemy = false;
}

void UBattleSubsystem::WriteBack(EJRPGBattleResult Result)
{
	// Aborted não escreve nada. Vitória, fuga e derrota gravam o HP e o MP da luta na ficha, com quem caiu levantando com 1 de HP
	// (RE battle_exit_party_reset; na derrota o piso vale para todos no quadro do fade, e é por isso que uma derrota roteirizada
	// volta ao campo de pé). A derrota grava SÓ o HP e o MP (o AP e as Arts aprendidas na luta não ficam)
	if (Result == EJRPGBattleResult::Aborted)
	{
		return;
	}
	const bool bKeepsProgress = Result == EJRPGBattleResult::Victory || Result == EJRPGBattleResult::Escaped;
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	UArtsSubsystem* Arts = GI ? GI->GetSubsystem<UArtsSubsystem>() : nullptr;
	if (!Party)
	{
		return;
	}

	for (const FJRPGCombatant& C : Resolver.GetCombatants())
	{
		FJRPGPartyMember Member;
		if (C.Side != EJRPGBattleSide::Party || !Party->GetMember(C.Key, Member))
		{
			continue;
		}
		// Quem caiu levanta com 1 de HP em toda saída (o original não deixa ninguém em 0 depois da luta, e não cura nada)
		const int32 HP = FMath::Max(C.HP, 1);
		if (HP > Member.HP)
		{
			if (Member.HP <= 0)
			{
				Party->ReviveMember(C.Key, HP);
			}
			else
			{
				Party->HealHP(C.Key, HP - Member.HP);
			}
		}
		else if (HP < Member.HP)
		{
			Party->ApplyDamage(C.Key, Member.HP - HP);
		}
		// O MP da luta, para cima ou para baixo (antes o gasto não voltava para a ficha)
		if (C.MP > Member.MP)
		{
			Party->RestoreMP(C.Key, C.MP - Member.MP);
		}
		else if (C.MP < Member.MP)
		{
			Party->SpendMP(C.Key, Member.MP - C.MP);
		}
		if (!bKeepsProgress)
		{
			continue;
		}
		Party->SetAP(C.Key, C.AP);

		// As Artes aprendidas na luta: a resolução só as pôs no combatente, é o UArtsSubsystem quem escreve na lista do save
		if (Arts)
		{
			for (const FName& ArtKey : C.KnownArts)
			{
				if (!Member.LearnedArts.Contains(ArtKey))
				{
					Arts->LearnArt(C.Key, ArtKey);
				}
			}
		}
	}
}

void UBattleSubsystem::PayRewards()
{
	// Pago uma vez só: o flag fica no estado da luta
	if (CurrentSetup.bNoRewards || !Resolver.ClaimRewards())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	const EJRPGRuleset Ruleset = Core ? Core->GetRuleset() : EJRPGRuleset::USA;

	// O EXP cru e o Gold cru dos inimigos derrotados (DT_Enemies); a versão e a dificuldade entram depois. O fim de debug
	// (JRPG.Battle.End victory) paga todos, vivos também, para dar para testar a recompensa sem derrotar a luta inteira
	int32 RawEXP = 0;
	TArray<int32> Golds;
	for (const FJRPGCombatant& C : Resolver.GetCombatants())
	{
		FJRPGEnemyData Row;
		if (C.Side == EJRPGBattleSide::Enemy && (!C.bAlive || bDebugPayEveryEnemy) && UJRPGEncounterLibrary::GetEnemyData(C.Key, Row))
		{
			RawEXP += Row.EXP;
			Golds.Add(Row.Gold);
		}
	}

	// O EXP vai para os heróis DA LUTA que estão vivos agora (o roster já recebeu o HP de cada um)
	TArray<FName> Participants;
	for (const FJRPGCombatant& C : Resolver.GetCombatants())
	{
		FJRPGPartyMember Member;
		if (C.Side == EJRPGBattleSide::Party && Party && Party->GetMember(C.Key, Member) && !Member.IsDead())
		{
			Participants.Add(C.Key);
		}
	}
	// A ficha de cada um antes do EXP (a tela de resultados conta de um para o outro)
	VictoryResults = FJRPGVictoryResults();
	VictoryResults.bValid = true;
	for (const FName& Key : Participants)
	{
		FJRPGPartyMember Member;
		if (Party->GetMember(Key, Member))
		{
			FJRPGVictoryHeroResult& Hero = VictoryResults.Heroes.AddDefaulted_GetRef();
			Hero.Key = Key;
			Hero.LevelBefore = Member.Level;
			Hero.EXPBefore = Member.ExperienceTotal;
			Hero.MaxHPBefore = Member.MaxHP;
			Hero.MaxMPBefore = Member.MaxMP;
		}
	}
	const int32 PerHero = Party ? Party->AwardBattleXPToParticipants(RawEXP, Participants) : 0;
	// E depois: o level, o EXP, o HP / MP (o level-up sobe o máximo) e o EXP de cada level cruzado, mais o do próximo
	for (FJRPGVictoryHeroResult& Hero : VictoryResults.Heroes)
	{
		FJRPGPartyMember Member;
		if (!Party->GetMember(Hero.Key, Member))
		{
			continue;
		}
		Hero.LevelAfter = Member.Level;
		Hero.EXPAfter = Member.ExperienceTotal;
		Hero.EXPGained = FMath::Max(Hero.EXPAfter - Hero.EXPBefore, 0);
		Hero.HP = Member.HP;
		Hero.MaxHPAfter = Member.MaxHP;
		Hero.MP = Member.MP;
		Hero.MaxMPAfter = Member.MaxMP;
		if (Core)
		{
			const int32 Top = FMath::Min(Hero.LevelAfter + 1, Core->GetMaxLevel());
			for (int32 Level = Hero.LevelBefore + 1; Level <= Top; ++Level)
			{
				Hero.LevelThresholds.Add(Core->GetXPForLevelForSlot(Level, Member.XPCurveSlot));
			}
		}
	}

	// TODO(Fase 7): o Golden Book (o bônus de Gold) entra aqui quando o item e o equipamento dos heróis forem lidos
	// da luta; até lá o Gold sai sempre sem ele.
	int32 Gold = UJRPGBattleRules::VictoryGold(Golds, false, Ruleset);
	if (Core)
	{
		Gold = Core->ScaleGoldReward(Gold);
		// Uma vitória que paga 0 (inimigo sem Gold) não chama o AddGold: ele reclama de quantia inválida
		if (Gold > 0)
		{
			Core->AddGold(Gold);
		}
	}
	VictoryResults.Gold = FMath::Max(Gold, 0);
	UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: recompensa — EXP cru %d (%d cada, %d herói(s) vivo(s)), Gold %d"), RawEXP, PerHero, Participants.Num(), Gold);
}

// =================================================================================================
// A luta andando
// =================================================================================================

bool UBattleSubsystem::SubmitApproach(int32 ActionIndex, int32 AttackRamp, int32 GuardRamp)
{
	if (!bInBattle)
	{
		return false;
	}
	FJRPGApproachInput Approach;
	Approach.ActionIndex = ActionIndex;
	Approach.AttackRamp = AttackRamp;
	Approach.GuardRamp = GuardRamp;
	FString Why;
	if (!Resolver.SubmitApproach(Approach, &Why))
	{
		UE_LOG(LogTemp, Verbose, TEXT("BattleSubsystem: aproximação recusada — %s"), *Why);
		return false;
	}
	return true;
}

void UBattleSubsystem::StallResolver(int32 Frames)
{
	if (bInBattle && Frames > 0)
	{
		ResolverStallFrames += Frames;
	}
}

void UBattleSubsystem::HoldLogic(bool bHold)
{
	bLogicHeld = bHold;
	if (!bInBattle)
	{
		return;
	}
	// Soltar pode começar a execução (comandos já entrados durante a espera): os eventos novos saem daqui
	Resolver.SetHoldExecution(bHold);
	if (!bHold)
	{
		FlushEvents();
	}
}

bool UBattleSubsystem::SubmitCommand(const FJRPGBattleCommand& Command)
{
	if (!bInBattle)
	{
		return false;
	}
	FString Why;
	const bool bAccepted = Resolver.SubmitCommand(Command, &Why);
	if (!bAccepted)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: comando recusado — %s"), *Why);
		// A recusa também vai para quem escuta (a tela mostra, ao lado do prompt): senão a luta parece travada
		OnCommandRefused.Broadcast(Command.Slot, Why);
	}
	FlushEvents();
	return bAccepted;
}

bool UBattleSubsystem::SubmitInput(const FJRPGBattleInput& Input)
{
	if (!bInBattle)
	{
		return false;
	}
	FString Why;
	const bool bAccepted = Resolver.SubmitInput(Input, &Why);
	if (!bAccepted)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: input recusado — %s"), *Why);
	}
	return bAccepted;
}

UWorld* UBattleSubsystem::GetTickableGameObjectWorld() const
{
	// O mundo do jogo: sem ele o subsistema cairia na chamada sem mundo do UGameEngine::Tick, que ignora a pausa
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetWorld() : nullptr;
}

void UBattleSubsystem::Tick(float DeltaTime)
{
	if (!bInBattle || bLogicHeld)
	{
		return;
	}
	// Por segurança (o mundo ainda não existe, a viagem de mapa, a pausa): o relógio da luta não anda
	const UWorld* World = GetTickableGameObjectWorld();
	if (!World || World->IsPaused())
	{
		return;
	}
	// O DeltaTime só enche o acumulador; as regras andam em quadros fixos de 1/60 s
	Accumulator += DeltaTime;
	int32 Steps = 0;
	while (bInBattle && Accumulator >= LogicStep && Steps < MaxStepsPerTick)
	{
		Accumulator -= LogicStep;
		++Steps;
		StepFrame();
	}
	if (Steps >= MaxStepsPerTick)
	{
		Accumulator = 0.0;   // uma pausa longa não vira uma rajada de golpes
	}
}

void UBattleSubsystem::StepFrame()
{
	if (!bInBattle)
	{
		return;
	}
	// O relógio de apresentação anda 1 por passo, sempre (o Tick já parou com a pausa e com o HoldLogic). O resolvedor só anda na
	// Execution: na digitação dos comandos e no hold da vitória / derrota o State.Frame fica parado e a diferença (o offset) cresce,
	// para os eventos e as timelines das regras caírem no mesmo relógio que os pawns recebem
	++PresentationFrame;
	bHeldForApproach = false;
	// A caminhada do atacante passa dos LeadFrames (StallResolver): as regras esperam, a apresentação segue
	if (ResolverStallFrames > 0)
	{
		--ResolverStallFrames;
	}
	else if (Resolver.IsWaitingForApproach())
	{
		// PvP: o primeiro golpe espera a aproximação da autoridade (as regras não andam; a apresentação segue e a cena atrasa
		// a ação do pawn o mesmo tanto)
		bHeldForApproach = true;
	}
	else
	{
		Resolver.StepFrame();
	}
	PresentationOffset = PresentationFrame - Resolver.GetState().Frame;
	// Dev (battle.ap on): o AP volta ao máximo a cada quadro, também na digitação dos comandos
	if (bDebugFullAP && !Resolver.IsFinished())
	{
		Resolver.DebugFillPartyAP();
	}
	FlushEvents();
	// Decidida a luta (depois da reação do alvo e da faixa Done), o relógio das regras para, mas a apresentação segue (hold da vitória / derrota)
	if (bInBattle && Resolver.IsFinished())
	{
		++FramesSinceFinish;
	}
	OnLogicFrame.Broadcast(PresentationFrame);
	// Segura a tela de resultados (a pose de vitória, 256 vsyncs no original) antes de fechar. Nossa tela de resultados segura mais
	// (HoldVictoryEnd) ou pede a saída já (EndVictoryNow, o "pular")
	if (bInBattle && Resolver.IsFinished())
	{
		const bool bVictory = Resolver.GetResult() == EJRPGBattleResult::Victory;
		if (bVictory && bEndVictoryNow)
		{
			EndBattle(Resolver.GetResult());
		}
		else if (FramesSinceFinish >= FinishHoldFor(Resolver.GetResult()) && !(bVictory && bVictoryEndHeld))
		{
			EndBattle(Resolver.GetResult());
		}
	}
}

const FJRPGVictoryResults& UBattleSubsystem::SettleVictory()
{
	if (!bInBattle || !Resolver.IsFinished() || Resolver.GetResult() != EJRPGBattleResult::Victory)
	{
		static const FJRPGVictoryResults Invalid;
		return Invalid;
	}
	if (!bVictorySettled)
	{
		bVictorySettled = true;
		WriteBack(EJRPGBattleResult::Victory);
		PayRewards();
	}
	return VictoryResults;
}

int32 UBattleSubsystem::FinishHoldFor(EJRPGBattleResult Result) const
{
	switch (Result)
	{
	case EJRPGBattleResult::Victory: return VictoryLoadFrames + VictoryHoldFrames;   // a espera antes da pose + a pose
	case EJRPGBattleResult::Defeat:  return DefeatHoldFrames;
	default:                         return 0;
	}
}

void UBattleSubsystem::FlushEvents()
{
	const TArray<FJRPGBattleEvent>& Events = Resolver.GetEvents();
	while (FlushedEvents < Events.Num())
	{
		// Uma cópia: um listener pode mandar um comando e acrescentar eventos no meio
		FJRPGBattleEvent Event = Events[FlushedEvents++];
		UE_LOG(LogTemp, Log, TEXT("Battle: %s"), *Resolver.DescribeEvent(Event));
		// O quadro vai no relógio de apresentação (o das regras parou durante a digitação); o log e o GetEventLog() ficam com o cru
		Event.Frame += PresentationOffset;
		OnBattleEvent.Broadcast(Event);
	}

	const int32 Slot = Resolver.GetActiveSlot();
	const EJRPGBattleStage Stage = Resolver.GetState().Stage;
	if (Slot != LastNotifiedSlot || Stage != LastNotifiedStage)
	{
		LastNotifiedSlot = Slot;
		LastNotifiedStage = Stage;
		OnSlotChanged.Broadcast(Slot, Stage);
	}
}
