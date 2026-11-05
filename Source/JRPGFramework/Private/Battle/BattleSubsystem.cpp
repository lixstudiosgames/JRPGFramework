#include "Battle/BattleSubsystem.h"
#include "Battle/BattleFormulas.h"
#include "Battle/EnemyData.h"
#include "Battle/HeroBattleData.h"
#include "Battle/JRPGAnimNotify_ChainCut.h"
#include "Battle/JRPGAnimNotify_Hit.h"
#include "Battle/JRPGBattleRules.h"
#include "Arts/ArtsSubsystem.h"
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

void UBattleSubsystem::ApplyHeroSwings(FJRPGCombatant& Hero, const UDataTable* Table, FName Weapon, FName Shoes, int32 SeruLevel)
{
	if (!Table || Table->GetRowStruct() != FJRPGHeroSwingData::StaticStruct())
	{
		return;
	}
	static const TCHAR* const ButtonNames[4] = { TEXT("Arms"), TEXT("RaSeru"), TEXT("High"), TEXT("Low") };
	static const FName DefaultItem(TEXT("default"));
	// O item que dirige cada botão, na ordem de EJRPGArtButton (Arms, Ra-Seru, High, Low)
	const FName Items[4] = { Weapon, FName(*FString::Printf(TEXT("seru_%d"), FMath::Max(SeruLevel, 1))), Shoes, Shoes };

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
	C.Element = Row ? Row->RaSeruElement : EJRPGElement::None;
	C.bLeftHanded = Row && Row->bLeftHanded;

	// As cinco peças, cada uma com o ATK do equipamento do papel dela (o Ra-Seru é o do tier)
	C.PieceATK.Init(0, UBattleFormulas::PieceCount);
	auto EquippedAt = [&](EJRPGEquipSlot Slot) -> FName
	{
		return bInRoster && Member.Equipment.IsValidIndex(static_cast<int32>(Slot)) ? Member.Equipment[static_cast<int32>(Slot)] : NAME_None;
	};
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
		if (Tier && !Tier->TierATK.IsEmpty())
		{
			const int32 Level = bInRoster ? Member.SeruLevel : 1;
			C.PieceATK[UBattleFormulas::PieceRaSeru] = Tier->TierATK[FMath::Clamp(Level - 1, 0, Tier->TierATK.Num() - 1)];
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
		ApplyHeroSwings(C, Swings, EquippedAt(EJRPGEquipSlot::Weapon), EquippedAt(EJRPGEquipSlot::Shoes), bInRoster ? Member.SeruLevel : 1);
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
		C.bHasRaSeru = Row && !Row->RaSeru.IsNone();
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
		OutTiming.Frames = SecondsToSteps(Clip->GetPlayLength(), ClipRate, Query.GameSpeed);
		const TArray<float> Hits = UJRPGAnimNotify_Hit::FindHitTimes(Clip);
		if (Hits.Num() == Query.ExpectedHits)
		{
			for (const float Time : Hits)
			{
				OutTiming.HitFrames.Add(SecondsToSteps(Time, ClipRate, Query.GameSpeed));
			}
		}
		else if (Query.Kind == EJRPGActionStepKind::Art && ArtHits)
		{
			// Sem marcadores no clip: os quadros de golpe do registro da Art no disco, na conta do golpe simples
			const FJRPGArtHitData* Row = ArtHits->FindRow<FJRPGArtHitData>(Query.ArtKey, TEXT("ArtHits"), false);
			if (Row && Row->HitBeats.Num() == Query.ExpectedHits && Query.ExpectedHits > 0)
			{
				for (const int32 Beat : Row->HitBeats)
				{
					const int32 Frame = UBattleFormulas::ClipFramesToSteps(FMath::Max(Beat - 1, 0), Query.GameSpeed, FMath::Max(Row->Rate, 1));
					OutTiming.HitFrames.Add(Frame);
					// O clip nunca acaba antes do último golpe dele
					OutTiming.Frames = FMath::Max(OutTiming.Frames, Frame + 1);
				}
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
	bLogicHeld = false;
	Accumulator = 0.0;
	FlushedEvents = 0;
	FramesSinceFinish = 0;
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

	// A rodada 1 abre aqui (RoundStart); os comandos esperam o jogador e nada anda até alguém comandar
	Resolver.Begin(Combatants, CurrentSetup.Seed, CurrentSetup.bScriptedFight, CurrentSetup.FormationAdvantage);
	OnBattleStarted.Broadcast(CurrentSetup, Resolver.GetCombatants());
	FlushEvents();
	return true;
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

	WriteBack(Result);
	if (Result == EJRPGBattleResult::Victory)
	{
		PayRewards();
	}

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
	const int32 PerHero = Party ? Party->AwardBattleXPToParticipants(RawEXP, Participants) : 0;

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
	FlushEvents();
	// Decidida a luta (depois da reação do alvo e da faixa Done), o relógio das regras para, mas a apresentação segue (hold da vitória / derrota)
	if (bInBattle && Resolver.IsFinished())
	{
		++FramesSinceFinish;
	}
	OnLogicFrame.Broadcast(PresentationFrame);
	// Segura a tela de resultados (a pose de vitória, 256 vsyncs no original) antes de fechar
	if (bInBattle && Resolver.IsFinished() && FramesSinceFinish >= FinishHoldFor(Resolver.GetResult()))
	{
		EndBattle(Resolver.GetResult());
	}
}

int32 UBattleSubsystem::FinishHoldFor(EJRPGBattleResult Result) const
{
	switch (Result)
	{
	case EJRPGBattleResult::Victory: return VictoryHoldFrames;
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
