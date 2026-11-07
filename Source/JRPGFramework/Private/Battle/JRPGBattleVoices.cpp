#include "Battle/JRPGBattleVoices.h"

#include "Arts/ArtsData.h"
#include "Audio/AudioSubsystem.h"
#include "Battle/HeroBattleData.h"
#include "Components/AudioComponent.h"
#include "Core/CoreSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	UDataTable* LoadTable(const TCHAR* Name, const UScriptStruct* RowStruct)
	{
		UDataTable* Table = UCoreSubsystem::ResolveDataTable(Name);
		if (Table && Table->GetRowStruct() != RowStruct)
		{
			UE_LOG(LogTemp, Warning, TEXT("BattleVoices: %s não é de %s — as vozes da luta ficam mudas"), Name, *RowStruct->GetName());
			return nullptr;
		}
		return Table;
	}
}

void FJRPGBattleVoices::Begin()
{
	Banks = LoadTable(TEXT("DT_BattleVoices"), FJRPGBattleVoiceBankData::StaticStruct());
	ArtVoices = LoadTable(TEXT("DT_ArtVoices"), FJRPGArtVoiceData::StaticStruct());
	Arts = UCoreSubsystem::ResolveDataTable(TEXT("DT_Arts"));
	if (!HasData())
	{
		UE_LOG(LogTemp, Log, TEXT("BattleVoices: sem DT_BattleVoices / DT_ArtVoices (Extras/ps1_battle_voices.py) — sem grito, fanfarra nem grunhido"));
	}
	Random.Initialize(FMath::Rand());
	LastShoutChannel = INDEX_NONE;
	StopAll();
}

void FJRPGBattleVoices::End()
{
	StopAll();
	Banks = nullptr;
	ArtVoices = nullptr;
	Arts = nullptr;
}

const FJRPGBattleVoiceBankData* FJRPGBattleVoices::FindBank(FName Character) const
{
	return Banks && !Character.IsNone() ? Banks->FindRow<FJRPGBattleVoiceBankData>(Character, TEXT("BattleVoices"), false) : nullptr;
}

const FJRPGArtVoiceData* FJRPGBattleVoices::FindArtVoice(FName ArtKey) const
{
	return ArtVoices && !ArtKey.IsNone() ? ArtVoices->FindRow<FJRPGArtVoiceData>(ArtKey, TEXT("BattleVoices"), false) : nullptr;
}

bool FJRPGBattleVoices::IsLatchedBy(const FJRPGBattleVoiceBankData& Bank, const FJRPGActionStep* Previous) const
{
	// A trava é o byte +0x87 do clip que o commit anterior pôs (FUN_8004E13C), lida antes do commit deste guardar o dele. No disco,
	// só o SpecialStarter (1) e as finalizações das Supers (2) a ligam; o golpe, o ArtStarter e as outras Arts a desligam
	if (!Previous)
	{
		return false;
	}
	switch (Previous->Kind)
	{
	case EJRPGActionStepKind::SpecialStarter:
		return Bank.bSpecialStarterLatch;
	case EJRPGActionStepKind::Art:
	{
		const FJRPGArtVoiceData* Row = FindArtVoice(Previous->ArtKey);
		return Row && Row->bRaisesLatch;
	}
	default:
		return false;
	}
}

bool FJRPGBattleVoices::IsNewRegularStarter(const FJRPGActionTimeline& Timeline, const FJRPGActionStep* Step) const
{
	if (!Step || Step->Kind != EJRPGActionStepKind::SpecialStarter)
	{
		return false;
	}
	const FJRPGArtData* Row = Arts ? Arts->FindRow<FJRPGArtData>(Step->ArtKey, TEXT("BattleVoices"), false) : nullptr;
	if (!Row || Row->Kind != EJRPGArtKind::Regular)
	{
		return false;
	}
	const FName Key = Step->ArtKey;
	return Timeline.Marks.ContainsByPredicate([Key](const FJRPGActionMark& Mark)
	{
		return Mark.Kind == EJRPGActionMarkKind::ArtLearned && Mark.ArtKey == Key;
	});
}

float FJRPGBattleVoices::CommitStep(UWorld* World, FName Character, const FJRPGActionTimeline& Timeline, int32 StepIndex, EJRPGBattleVoiceSlow& OutSlow)
{
	OutSlow = EJRPGBattleVoiceSlow::None;
	if (!World || !Timeline.Steps.IsValidIndex(StepIndex))
	{
		return 0.0f;
	}
	const FJRPGBattleVoiceBankData* Bank = FindBank(Character);
	if (!HasData() || !Bank)
	{
		return -1.0f;   // sem vozes para este herói: quem chama decide o slow sem elas
	}
	const TArray<FJRPGActionStep>& Steps = Timeline.Steps;
	const FJRPGActionStep& Step = Steps[StepIndex];
	const bool bLatched = IsLatchedBy(*Bank, StepIndex > 0 ? &Steps[StepIndex - 1] : nullptr);

	// O commit de FUN_8004AD80 olha o byte da fila antes do cursor (0x8004BCF0..0x8004BD6C): o 0x19 do starter conhecido chama o
	// grito da Art que vem atrás dele; outro byte só chama com a trava ligada, e aí é o grito da própria Art. O flag é o byte depois
	// da Art: 0 quando ela fecha a fila do turno
	switch (Step.Kind)
	{
	case EJRPGActionStepKind::ArtStarter:
		if (Steps.IsValidIndex(StepIndex + 1) && Steps[StepIndex + 1].Kind == EJRPGActionStepKind::Art)
		{
			OutSlow = EJRPGBattleVoiceSlow::KnownArt;
			return Shout(World, *Bank, Steps[StepIndex + 1].ArtKey, Steps.IsValidIndex(StepIndex + 2), bLatched);
		}
		return 0.0f;
	case EJRPGActionStepKind::SpecialStarter:
	{
		// O 0x1A não é o 0x19, e o clip antes dele (um golpe) não trava: nenhum grito aqui, só a fanfarra
		const float Seconds = Fanfare(World, *Bank, Timeline, StepIndex);
		if (Seconds > 0.0f)
		{
			const FJRPGArtData* Row = Arts ? Arts->FindRow<FJRPGArtData>(Step.ArtKey, TEXT("BattleVoices"), false) : nullptr;
			if (IsNewRegularStarter(Timeline, &Step))
			{
				OutSlow = EJRPGBattleVoiceSlow::NewArt;
			}
			else if (Row && (Row->Kind == EJRPGArtKind::Super || Row->Kind == EJRPGArtKind::Miracle))
			{
				OutSlow = EJRPGBattleVoiceSlow::SuperMiracle;
			}
			else if (Row && Row->Kind == EJRPGArtKind::Hyper)
			{
				OutSlow = EJRPGBattleVoiceSlow::Hyper;
			}
		}
		return Seconds;
	}
	case EJRPGActionStepKind::Art:
	{
		if (!bLatched)
		{
			return 0.0f;
		}
		const float Seconds = Shout(World, *Bank, Step.ArtKey, Steps.IsValidIndex(StepIndex + 1), true);
		if (Seconds > 0.0f && StepIndex > 0 && IsNewRegularStarter(Timeline, &Steps[StepIndex - 1]))
		{
			OutSlow = EJRPGBattleVoiceSlow::NewArt;
		}
		return Seconds;
	}
	default:
		return 0.0f;
	}
}

float FJRPGBattleVoices::Shout(UWorld* World, const FJRPGBattleVoiceBankData& Bank, FName ArtKey, bool bFollowed, bool bLatched)
{
	const FJRPGArtVoiceData* Row = FindArtVoice(ArtKey);
	if (!Row)
	{
		return 0.0f;   // a Art não grita (a Hyper, a Miracle, as constantes fora das duas metades)
	}
	const TArray<int32>* Pool = nullptr;
	if (Row->bFinisherTable)
	{
		// A segunda metade: com outra coisa depois, um som no lugar do clip; senão, a lista única
		if (bFollowed)
		{
			if (USoundBase* Cue = Bank.FinisherCue.LoadSynchronous())
			{
				UGameplayStatics::PlaySound2D(World, Cue, SFXVolume(World));
			}
			return 0.0f;
		}
		Pool = &Row->Shout;
	}
	else
	{
		Pool = bLatched ? &Row->ShoutLatched : (bFollowed ? &Row->ShoutFollowed : &Row->Shout);
	}
	if (Pool->Num() == 0)
	{
		return 0.0f;
	}

	// Sorteia um da lista, sem repetir o último que saiu
	TArray<int32> Candidates;
	for (const int32 Channel : *Pool)
	{
		if (Channel != LastShoutChannel)
		{
			Candidates.Add(Channel);
		}
	}
	if (Candidates.Num() == 0)
	{
		Candidates = *Pool;
	}
	const int32 Channel = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
	LastShoutChannel = Channel;
	if (!Bank.Shouts.IsValidIndex(Channel))
	{
		return 0.0f;
	}
	const float Seconds = Bank.ShoutSeconds.IsValidIndex(Channel) ? Bank.ShoutSeconds[Channel] : 1.0f;
	PlayVoice(World, Bank.Shouts[Channel], false);
	return Seconds;
}

float FJRPGBattleVoices::Fanfare(UWorld* World, const FJRPGBattleVoiceBankData& Bank, const FJRPGActionTimeline& Timeline, int32 StepIndex)
{
	// O original descarta a fanfarra com o drive ainda lendo a voz anterior (FUN_8004FCC8 / FUN_8003DE7C(1)); aqui ela sempre toca
	// e corta só a fanfarra anterior (bug 84: a segunda de duas Hypers seguidas ficava muda)
	const TArray<FJRPGActionStep>& Steps = Timeline.Steps;
	const FName StarterArt = Steps[StepIndex].ArtKey;

	// A Super e a Miracle (a marca da Super no assento) e a Art nova (o scratch da fila) tocam a do herói, o canal 1; a Hyper, um
	// cara ou coroa entre os dois canais dela, pela Art que vem atrás do 0x1A (0x8004B74C..0x8004BB44)
	const FJRPGArtData* ArtRow = Arts ? Arts->FindRow<FJRPGArtData>(StarterArt, TEXT("BattleVoices"), false) : nullptr;
	bool bGeneric = ArtRow && (ArtRow->Kind == EJRPGArtKind::Super || ArtRow->Kind == EJRPGArtKind::Miracle);
	bGeneric = bGeneric || Timeline.Marks.ContainsByPredicate([StarterArt](const FJRPGActionMark& Mark)
	{
		return Mark.Kind == EJRPGActionMarkKind::ArtLearned && Mark.ArtKey == StarterArt;
	});

	int32 Channel = INDEX_NONE;
	if (bGeneric)
	{
		Channel = Bank.GenericFanfareChannel;
	}
	else if (Steps.IsValidIndex(StepIndex + 1) && Steps[StepIndex + 1].Kind == EJRPGActionStepKind::Art)
	{
		const FJRPGArtVoiceData* Row = FindArtVoice(Steps[StepIndex + 1].ArtKey);
		if (Row && Row->Fanfare.Num() > 0)
		{
			Channel = Row->Fanfare[Random.RandRange(0, Row->Fanfare.Num() - 1)];
		}
	}
	if (!Bank.Fanfares.IsValidIndex(Channel))
	{
		return 0.0f;
	}
	const float Seconds = Bank.FanfareSeconds.IsValidIndex(Channel) ? Bank.FanfareSeconds[Channel] : 2.0f;
	PlayVoice(World, Bank.Fanfares[Channel], true);
	return Seconds;
}

void FJRPGBattleVoices::NoteBlocked(UWorld* World, FName Attacker)
{
	// O grunhido do XA30 pelo herói que bateu (0x801EEB44); monstro não tem. Toca por cima do que soar (sem o drive do original)
	const FJRPGBattleVoiceBankData* Bank = World && HasData() ? FindBank(Attacker) : nullptr;
	if (!Bank)
	{
		return;
	}
	PlayVoice(World, Bank->BlockGrunt, false);
}

void FJRPGBattleVoices::PlayVoice(UWorld* World, const TSoftObjectPtr<USoundBase>& Sound, bool bFanfare)
{
	USoundBase* Loaded = Sound.LoadSynchronous();
	if (!Loaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleVoices: a voz %s não está no projeto (Extras/ue_import_battle_voices.py)"), *Sound.ToString());
		return;
	}
	// A fanfarra nova corta a anterior (a que soa e a que ainda esperava a latência); grito e grunhido não cortam nada
	if (bFanfare)
	{
		StopFanfare();
	}
	PendingSounds.Add(Loaded);
	PendingTimes.Add(World->GetRealTimeSeconds() + FMath::Max(VoiceLatencySeconds, 0.0f));
	PendingFanfare.Add(bFanfare);
	Tick(World);
}

void FJRPGBattleVoices::Tick(UWorld* World)
{
	if (!World || PendingSounds.Num() == 0)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	for (int32 i = 0; i < PendingSounds.Num();)
	{
		if (Now < PendingTimes[i])
		{
			++i;
			continue;
		}
		USoundBase* Sound = PendingSounds[i];
		const bool bFanfare = PendingFanfare[i];
		PendingSounds.RemoveAt(i);
		PendingTimes.RemoveAt(i);
		PendingFanfare.RemoveAt(i);
		StartVoice(World, Sound, bFanfare);
	}
}

void FJRPGBattleVoices::StartVoice(UWorld* World, USoundBase* Sound, bool bFanfare)
{
	UAudioComponent* Component = Sound ? UGameplayStatics::CreateSound2D(World, Sound, SFXVolume(World)) : nullptr;
	if (!Component)
	{
		return;
	}
	if (bFanfare)
	{
		FanfareVoice = Component;
	}
	else
	{
		// O componente se destrói sozinho quando a voz acaba (CreateSound2D, bAutoDestroy): solta os que já foram
		OtherVoices.RemoveAll([](const TObjectPtr<UAudioComponent>& Other) { return !IsValid(Other); });
		OtherVoices.Add(Component);
	}
	Component->Play();
}

void FJRPGBattleVoices::StopFanfare()
{
	for (int32 i = PendingSounds.Num() - 1; i >= 0; --i)
	{
		if (PendingFanfare[i])
		{
			PendingSounds.RemoveAt(i);
			PendingTimes.RemoveAt(i);
			PendingFanfare.RemoveAt(i);
		}
	}
	if (IsValid(FanfareVoice))
	{
		FanfareVoice->Stop();
	}
	FanfareVoice = nullptr;
}

void FJRPGBattleVoices::StopAll()
{
	StopFanfare();
	PendingSounds.Reset();
	PendingTimes.Reset();
	PendingFanfare.Reset();
	for (const TObjectPtr<UAudioComponent>& Other : OtherVoices)
	{
		if (IsValid(Other))
		{
			Other->Stop();
		}
	}
	OtherVoices.Reset();
}

float FJRPGBattleVoices::SFXVolume(const UWorld* World)
{
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UAudioSubsystem* Audio = GameInstance ? GameInstance->GetSubsystem<UAudioSubsystem>() : nullptr;
	return Audio ? Audio->GetEffectiveSFXVolume() : 1.0f;
}
