#include "Arts/JRPGArtSequence.h"
#include "Battle/JRPGAnimNotify_ChainCut.h"
#include "Party/CharacterData.h"
#include "Core/CoreSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"

namespace
{
	UDataTable* ArtsTable() { return UCoreSubsystem::ResolveDataTable(TEXT("DT_Arts")); }
	UDataTable* CharactersTable() { return UCoreSubsystem::ResolveDataTable(TEXT("DT_Characters")); }

	/** Escala de velocidade do jogo: 8 = o ritmo normal (o RateScale 2 dos clips de golpe). */
	constexpr int32 SpeedNormal = 8;
	constexpr int32 SpeedArt = 4;
	constexpr int32 SpeedSpecial = 2;

	const TCHAR* ButtonLabel(EJRPGArtButton Button)
	{
		switch (Button)
		{
		case EJRPGArtButton::Arms:   return TEXT("Arms");
		case EJRPGArtButton::RaSeru: return TEXT("Ra-Seru");
		case EJRPGArtButton::High:   return TEXT("High");
		default:                     return TEXT("Low");
		}
	}
}

const FJRPGArtData* UJRPGArtSequenceLibrary::FindArt(FName ArtKey)
{
	const UDataTable* Table = ArtsTable();
	return Table && !ArtKey.IsNone() ? Table->FindRow<FJRPGArtData>(ArtKey, TEXT("ArtSequence"), false) : nullptr;
}

TArray<FName> UJRPGArtSequenceLibrary::HeroArts(FName Hero, bool bIncludeParts)
{
	TArray<FName> Out;
	if (const UDataTable* Table = ArtsTable())
	{
		Table->ForeachRow<FJRPGArtData>(TEXT("ArtSequence"), [&](const FName& Key, const FJRPGArtData& Row)
		{
			if (Row.Character == Hero && (bIncludeParts || Row.Kind != EJRPGArtKind::Part))
			{
				Out.Add(Key);
			}
		});
	}
	return Out;
}

UAnimSequence* UJRPGArtSequenceLibrary::LoadHeroClip(FName Hero, const FString& ClipName)
{
	const UDataTable* Table = CharactersTable();
	const FCharacterData* Row = Table ? Table->FindRow<FCharacterData>(Hero, TEXT("ArtSequence"), false) : nullptr;
	if (!Row || Row->BattleMesh.IsNull())
	{
		return nullptr;
	}
	// /Game/.../Vahn/SK_Vahn_Battle -> /Game/.../Vahn/AS_Vahn_Battle_<ClipName>
	const FString MeshPackage = Row->BattleMesh.ToSoftObjectPath().GetLongPackageName();
	FString MeshName = FPackageName::GetShortName(MeshPackage);
	MeshName.RemoveFromStart(TEXT("SK_"));
	const FString AssetName = TEXT("AS_") + MeshName + TEXT("_") + ClipName;
	const FString Package = FPackageName::GetLongPackagePath(MeshPackage) / AssetName;
	return Cast<UAnimSequence>(FSoftObjectPath(Package + TEXT(".") + AssetName).TryLoad());
}

FString UJRPGArtSequenceLibrary::SwingClipName(EJRPGArtButton Button, bool bLeftHanded)
{
	// Destro: Arms = esquerda, Ra-Seru = direita. Canhoto (Noa): o contrário.
	switch (Button)
	{
	case EJRPGArtButton::Arms:   return bLeftHanded ? TEXT("SwingR") : TEXT("SwingL");
	case EJRPGArtButton::RaSeru: return bLeftHanded ? TEXT("SwingL") : TEXT("SwingR");
	case EJRPGArtButton::High:   return TEXT("SwingU");
	default:                     return TEXT("SwingD");
	}
}

void UJRPGArtSequenceLibrary::AddStep(TArray<FJRPGArtStep>& Steps, const FString& Label, UAnimSequence* Clip, EJRPGArtStepKind Kind, FName ArtKey)
{
	FJRPGArtStep& Step = Steps.AddDefaulted_GetRef();
	Step.Label = Clip ? Label : Label + TEXT(" (sem clip)");
	Step.Clip = Clip;
	Step.Kind = Kind;
	Step.ArtKey = ArtKey;
	Step.CutTime = UJRPGAnimNotify_ChainCut::FindCutTime(Clip);
	if (!Clip)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArtSequence: %s sem clip"), *Label);
	}
}

void UJRPGArtSequenceLibrary::AppendButtonSwing(FName Hero, EJRPGArtButton Button, bool bLeftHanded, TArray<FJRPGArtStep>& Steps)
{
	const FString Swing = SwingClipName(Button, bLeftHanded);
	AddStep(Steps, FString::Printf(TEXT("%s (%s)"), ButtonLabel(Button), *Swing), LoadHeroClip(Hero, Swing),
		EJRPGArtStepKind::Swing, NAME_None);
}

void UJRPGArtSequenceLibrary::AddSequenceStep(TArray<FJRPGArtStep>& Steps, FName Hero, FName Step, FName SelfKey, const FJRPGArtData& Self, int32 Depth)
{
	if (Step == SelfKey)
	{
		AddStep(Steps, Self.DisplayName.ToString(), Self.Anim.LoadSynchronous(), EJRPGArtStepKind::Art, SelfKey);
		return;
	}
	if (const FJRPGArtData* Other = FindArt(Step))
	{
		if (!Other->Anim.IsNull() || Other->Sequence.IsEmpty() || Depth >= 4)
		{
			AddStep(Steps, Other->DisplayName.ToString(), Other->Anim.LoadSynchronous(), EJRPGArtStepKind::Art, Step);
			return;
		}
		for (const FName& Inner : Other->Sequence)
		{
			AddSequenceStep(Steps, Hero, Inner, Step, *Other, Depth + 1);
		}
		return;
	}
	// Clip básico do herói: SwingL..U, ArtStarter, SpecialStarter
	const FString Name = Step.ToString();
	const EJRPGArtStepKind Kind = Name == TEXT("ArtStarter") ? EJRPGArtStepKind::ArtStarter
		: Name == TEXT("SpecialStarter") ? EJRPGArtStepKind::SpecialStarter
		: EJRPGArtStepKind::Swing;
	AddStep(Steps, Name, LoadHeroClip(Hero, Name), Kind, Kind == EJRPGArtStepKind::Swing ? FName() : SelfKey);
}

bool UJRPGArtSequenceLibrary::AppendArtSteps(FName ArtKey, bool bLeftHanded, bool bLeadingSwings, TArray<FJRPGArtStep>& Steps)
{
	const FJRPGArtData* Row = FindArt(ArtKey);
	if (!Row)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArtSequence: %s não está no DT_Arts"), *ArtKey.ToString());
		return false;
	}
	const FName Hero = Row->Character;
	if (Row->Kind == EJRPGArtKind::Part)
	{
		AddStep(Steps, Row->DisplayName.ToString(), Row->Anim.LoadSynchronous(), EJRPGArtStepKind::Art, ArtKey);
		return true;
	}
	// Super e Miracle: a Sequence é o turno inteiro. Na Super os botões das Arts se sobrepõem (o
	// último de uma é o primeiro da seguinte) e cada Art entra no seu último botão; os botões do meio
	// já são os golpes conectores da Sequence. Só a primeira Art tem golpes antes, os seus botões
	// menos o último: Back Punch x3 = SwingU, SwingD, ArtStarter, Iron Head, SwingU, ArtStarter,
	// Flying Knee Attack, SwingR, SpecialStarter, finalizador x3 (RE: super_art.rs, o "find" casa o
	// fim da fila). A Miracle troca a fila inteira: sem golpes antes.
	if (Row->Kind == EJRPGArtKind::Super || Row->Kind == EJRPGArtKind::Miracle)
	{
		if (bLeadingSwings && Row->Kind == EJRPGArtKind::Super && Row->Sequence.Num() >= 2
			&& (Row->Sequence[0] == TEXT("ArtStarter") || Row->Sequence[0] == TEXT("SpecialStarter")))
		{
			const FJRPGArtData* First = FindArt(Row->Sequence[1]);
			if (First && First->Kind == EJRPGArtKind::Regular)
			{
				for (int32 b = 0; b + 1 < First->Command.Num(); ++b)
				{
					AppendButtonSwing(Hero, First->Command[b], bLeftHanded, Steps);
				}
			}
		}
		for (const FName& Step : Row->Sequence)
		{
			AddSequenceStep(Steps, Hero, Step, ArtKey, *Row, 0);
		}
		return true;
	}
	// Regular: os botões antes do último saem como golpes; o último vira starter + Art. A Hyper não
	// tem golpes: entra direto pelo SpecialStarter (a Sequence dela).
	if (bLeadingSwings && Row->Kind == EJRPGArtKind::Regular)
	{
		for (int32 i = 0; i + 1 < Row->Command.Num(); ++i)
		{
			AppendButtonSwing(Hero, Row->Command[i], bLeftHanded, Steps);
		}
	}
	if (!Row->Sequence.IsEmpty())
	{
		// A Sequence traz o próprio starter (SpecialStarter na Hyper)
		for (const FName& Step : Row->Sequence)
		{
			AddSequenceStep(Steps, Hero, Step, ArtKey, *Row, 0);
		}
		return true;
	}
	AddStep(Steps, TEXT("ArtStarter"), LoadHeroClip(Hero, TEXT("ArtStarter")), EJRPGArtStepKind::ArtStarter, ArtKey);
	AddStep(Steps, Row->DisplayName.ToString(), Row->Anim.LoadSynchronous(), EJRPGArtStepKind::Art, ArtKey);
	return true;
}

void UJRPGArtSequenceLibrary::ApplyGameSpeeds(TArray<FJRPGArtStep>& Steps)
{
	// A Art (e o starter especial) entram pelo braço de Art do commit (FUN_8004AD80), que põe 2 com
	// o modo especial ligado e 4 sem. Golpes e o ArtStarter ficam no ritmo normal (8), também os
	// conectores no meio de uma Super. O SpecialStarter liga o modo especial depois do próprio commit.
	bool bSpecial = false;
	for (FJRPGArtStep& Step : Steps)
	{
		switch (Step.Kind)
		{
		case EJRPGArtStepKind::SpecialStarter:
			Step.GameSpeed = bSpecial ? SpeedSpecial : SpeedArt;
			bSpecial = true;
			break;
		case EJRPGArtStepKind::Art:
			Step.GameSpeed = bSpecial ? SpeedSpecial : SpeedArt;
			break;
		default:
			Step.GameSpeed = SpeedNormal;
			break;
		}
	}
}

float UJRPGArtSequenceLibrary::StepPlayRate(const FJRPGArtStep& Step)
{
	// Os clips trazem a velocidade no RateScale: 2 = escala 8, 1 = 4, 0,5 = 2
	return Step.Clip && Step.Clip->RateScale > KINDA_SMALL_NUMBER
		? Step.GameSpeed / (4.0f * Step.Clip->RateScale)
		: 1.0f;
}
