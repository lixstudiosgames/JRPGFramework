#include "Arts/ArtsSubsystem.h"
#include "Arts/JRPGArtSequence.h"
#include "Core/CoreSubsystem.h"
#include "Party/CharacterData.h"
#include "Party/PartySubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	UDataTable* ArtsTable() { return UCoreSubsystem::ResolveDataTable(TEXT("DT_Arts")); }

	const FCharacterData* CharacterRow(FName Character)
	{
		const UDataTable* Table = UCoreSubsystem::ResolveDataTable(TEXT("DT_Characters"));
		return Table ? Table->FindRow<FCharacterData>(Character, TEXT("ArtsSubsystem"), false) : nullptr;
	}

	UArtsSubsystem* GetArts(const UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UArtsSubsystem>() : nullptr;
	}

	/** O herói e os botões de um comando de console: <Herói> <botões...>. */
	bool ParseHeroAndButtons(const TArray<FString>& Args, FName& OutHero, TArray<EJRPGArtButton>& OutButtons)
	{
		if (Args.Num() < 2)
		{
			return false;
		}
		OutHero = FName(*Args[0]);
		TArray<FString> Rest(Args);
		Rest.RemoveAt(0);
		return UJRPGArtsRules::ParseButtons(FString::Join(Rest, TEXT(" ")), UArtsSubsystem::IsLeftHanded(OutHero), OutButtons);
	}

	/** JRPG.Arts.Try <Herói> <botões>: o turno que os botões dariam agora, sem aprender nada. */
	void TryCommand(const TArray<FString>& Args, UWorld* World)
	{
		UArtsSubsystem* Arts = GetArts(World);
		FName Hero;
		TArray<EJRPGArtButton> Buttons;
		if (!Arts || !ParseHeroAndButtons(Args, Hero, Buttons))
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPG.Arts.Try <Herói> <botões>   ex.: JRPG.Arts.Try Vahn Arms Ra-Seru Arms   (só no jogo)"));
			return;
		}
		const FJRPGArtChain Chain = Arts->PreviewArtsChain(Hero, Buttons);
		UE_LOG(LogTemp, Display, TEXT("JRPG.Arts.Try %s:\n%s"), *Hero.ToString(), *UJRPGArtsRules::DescribeChain(Chain));
	}

	/** JRPG.Arts.Commit <Herói> <botões>: fecha a cadeia de verdade (aprende as Regular novas). */
	void CommitCommand(const TArray<FString>& Args, UWorld* World)
	{
		UArtsSubsystem* Arts = GetArts(World);
		FName Hero;
		TArray<EJRPGArtButton> Buttons;
		if (!Arts || !ParseHeroAndButtons(Args, Hero, Buttons))
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPG.Arts.Commit <Herói> <botões>   (só no jogo)"));
			return;
		}
		bool bValid = false;
		const FJRPGArtChain Chain = Arts->CommitArtsChain(Hero, UJRPGArtsRules::EncodeArtBytes(Buttons), bValid);
		UE_LOG(LogTemp, Display, TEXT("JRPG.Arts.Commit %s:\n%s"), *Hero.ToString(), *UJRPGArtsRules::DescribeChain(Chain));
	}

	/** JRPG.Arts.Learn <Herói> <ArtKey>: ensina, como um livro ou um NPC. */
	void LearnCommand(const TArray<FString>& Args, UWorld* World)
	{
		UArtsSubsystem* Arts = GetArts(World);
		if (!Arts || Args.Num() < 2)
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPG.Arts.Learn <Herói> <ArtKey>   (só no jogo)"));
			return;
		}
		const bool bNew = Arts->LearnArt(FName(*Args[0]), FName(*Args[1]));
		UE_LOG(LogTemp, Display, TEXT("JRPG.Arts.Learn %s %s: %s"), *Args[0], *Args[1], bNew ? TEXT("aprendida") : TEXT("nada novo"));
	}

	/** JRPG.Arts.Known <Herói>: a lista de Arts que ele sabe. */
	void KnownCommand(const TArray<FString>& Args, UWorld* World)
	{
		UArtsSubsystem* Arts = GetArts(World);
		if (!Arts || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPG.Arts.Known <Herói>   (só no jogo)"));
			return;
		}
		const TArray<FName> Known = Arts->GetKnownArts(FName(*Args[0]));
		FString List;
		for (const FName& Key : Known)
		{
			List += TEXT("\n  ") + Key.ToString();
		}
		UE_LOG(LogTemp, Display, TEXT("JRPG.Arts.Known %s: %d Art(s)%s"), *Args[0], Known.Num(), *List);
	}

	FAutoConsoleCommandWithWorldAndArgs GArtsTryCommand(
		TEXT("JRPG.Arts.Try"),
		TEXT("O turno que os botões dariam (sem aprender): JRPG.Arts.Try <Herói> <botões>, ex. Vahn Arms Ra-Seru Arms ou Vahn ← → ←"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TryCommand));

	FAutoConsoleCommandWithWorldAndArgs GArtsCommitCommand(
		TEXT("JRPG.Arts.Commit"),
		TEXT("Fecha a cadeia e aprende as Regular novas: JRPG.Arts.Commit <Herói> <botões>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CommitCommand));

	FAutoConsoleCommandWithWorldAndArgs GArtsLearnCommand(
		TEXT("JRPG.Arts.Learn"),
		TEXT("Ensina uma Art (livro / NPC): JRPG.Arts.Learn <Herói> <ArtKey>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LearnCommand));

	FAutoConsoleCommandWithWorldAndArgs GArtsKnownCommand(
		TEXT("JRPG.Arts.Known"),
		TEXT("As Arts que o herói sabe: JRPG.Arts.Known <Herói>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KnownCommand));
}

UArtsSubsystem::UArtsSubsystem()
	: LearnRng(0x4C454741)
{
}

UPartySubsystem* UArtsSubsystem::GetParty() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
}

// --- Botões ----------------------------------------------------------------------------------

bool UArtsSubsystem::IsLeftHanded(FName Character)
{
	const FCharacterData* Row = CharacterRow(Character);
	return Row && Row->bLeftHanded;
}

EJRPGArtButton UArtsSubsystem::ButtonForPad(FName Character, EJRPGPadButton Pad)
{
	return UJRPGArtsRules::ButtonForPad(IsLeftHanded(Character), Pad);
}

FText UArtsSubsystem::GetArtDisplayName(FName ArtKey)
{
	if (ArtKey.IsNone())
	{
		return FText::GetEmpty();
	}
	const FJRPGArtData* Row = UJRPGArtSequenceLibrary::FindArt(ArtKey);
	return Row && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromName(ArtKey);
}

const FJRPGArtData* UArtsSubsystem::FindHeroArt(FName Character, FName ArtKey)
{
	const FJRPGArtData* Row = UJRPGArtSequenceLibrary::FindArt(ArtKey);
	return Row && Row->Character == Character ? Row : nullptr;
}

// --- A lista ---------------------------------------------------------------------------------

TArray<FName> UArtsSubsystem::GetKnownArts(FName Character) const
{
	FJRPGPartyMember Member;
	const UPartySubsystem* Party = GetParty();
	return Party && Party->GetMember(Character, Member) ? Member.LearnedArts : TArray<FName>();
}

bool UArtsSubsystem::HasLearnedArt(FName Character, FName ArtKey) const
{
	return GetKnownArts(Character).Contains(ArtKey);
}

bool UArtsSubsystem::AddLearnedArt(FJRPGPartyMember& Member, FName ArtKey)
{
	const FJRPGArtData* Row = FindHeroArt(Member.Key, ArtKey);
	if (!Row || Row->Kind == EJRPGArtKind::Part || Member.LearnedArts.Contains(ArtKey))
	{
		return false;
	}
	Member.LearnedArts.Add(ArtKey);
	return true;
}

bool UArtsSubsystem::LearnArt(FName Character, FName ArtKey)
{
	const FJRPGArtData* Row = FindHeroArt(Character, ArtKey);
	if (!Row || Row->Kind == EJRPGArtKind::Part)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArtsSubsystem: %s não é uma Art de %s no DT_Arts"), *ArtKey.ToString(), *Character.ToString());
		return false;
	}
	UPartySubsystem* Party = GetParty();
	FJRPGPartyMember* Member = Party ? Party->FindMemberForWrite(Character) : nullptr;
	if (!Member)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArtsSubsystem: %s não está no roster — %s não foi aprendida"), *Character.ToString(), *ArtKey.ToString());
		return false;
	}
	if (!AddLearnedArt(*Member, ArtKey))
	{
		return false;
	}
	Party->NotifyMemberChanged();
	UE_LOG(LogTemp, Log, TEXT("ArtsSubsystem: %s aprendeu %s"), *Character.ToString(), *ArtKey.ToString());
	OnArtLearned.Broadcast(Character, ArtKey);
	return true;
}

bool UArtsSubsystem::ForgetArt(FName Character, FName ArtKey)
{
	UPartySubsystem* Party = GetParty();
	FJRPGPartyMember* Member = Party ? Party->FindMemberForWrite(Character) : nullptr;
	if (!Member || Member->LearnedArts.Remove(ArtKey) == 0)
	{
		return false;
	}
	Party->NotifyMemberChanged();
	return true;
}

void UArtsSubsystem::TeachArtToCharacter(const FString& CharacterName, FName ArtKey)
{
	LearnArt(FName(*CharacterName), ArtKey);
}

// --- Cadeia ----------------------------------------------------------------------------------

FJRPGArtsContext UArtsSubsystem::MakeContext(FName Character) const
{
	FJRPGArtsContext Context;
	Context.Character = Character;
	Context.LearnOnUseChance = LearnOnUseChance;
	FJRPGPartyMember Member;
	const UPartySubsystem* Party = GetParty();
	if (Party && Party->GetMember(Character, Member))
	{
		Context.KnownArts = Member.LearnedArts;
		Context.AP = Member.AP;
	}
	// O Ra-Seru do original é um byte de equipamento; aqui, o Ra-Seru fundido ao herói no DT_Characters
	const FCharacterData* Row = CharacterRow(Character);
	Context.bHasRaSeru = Row && !Row->RaSeru.IsNone();
	return Context;
}

TArray<FJRPGRecognizedArt> UArtsSubsystem::RecognizeArtSequence(FName Character, const TArray<EJRPGArtButton>& Input) const
{
	return PreviewArtsChain(Character, Input).Recognized;
}

FJRPGArtChain UArtsSubsystem::PreviewArtsChain(FName Character, const TArray<EJRPGArtButton>& Input) const
{
	// A prévia não consome o sorteio de verdade
	FRandomStream PreviewRng = LearnRng;
	return UJRPGArtsRules::BuildChain(ArtsTable(), MakeContext(Character), Input, PreviewRng);
}

TArray<FJRPGArtAvailability> UArtsSubsystem::GetSpecialsAvailability(FName Character, const TArray<EJRPGArtButton>& TypedSoFar) const
{
	return UJRPGArtsRules::GetSpecialsAvailability(ArtsTable(), MakeContext(Character), TypedSoFar);
}

FJRPGArtChain UArtsSubsystem::CommitArtsChain(FName Character, const TArray<uint8>& ArtBytes, bool& bValid)
{
	TArray<EJRPGArtButton> Buttons;
	bValid = UJRPGArtsRules::DecodeArtBytes(ArtBytes, Buttons);
	if (!bValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArtsSubsystem: cadeia de %s recusada (bytes inválidos)"), *Character.ToString());
		FJRPGArtChain Empty;
		Empty.Character = Character;
		return Empty;
	}
	const FJRPGArtChain Chain = UJRPGArtsRules::BuildChain(ArtsTable(), MakeContext(Character), Buttons, LearnRng);
	for (const FName& Key : Chain.NewlyLearned)
	{
		LearnArt(Character, Key);
	}
	return Chain;
}
