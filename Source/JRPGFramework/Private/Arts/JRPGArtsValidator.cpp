#include "Arts/JRPGArtsValidator.h"
#include "Arts/JRPGArtsRules.h"
#include "Battle/JRPGAnimNotify_Hit.h"
#include "Core/CoreSubsystem.h"
#include "Party/CharacterData.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	void AddIssue(TArray<FJRPGArtIssue>& Out, EJRPGArtIssueSeverity Severity, FName Key, const FString& Message)
	{
		FJRPGArtIssue& Issue = Out.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.ArtKey = Key;
		Issue.Message = Message;
	}

	void IssueError(TArray<FJRPGArtIssue>& Out, FName Key, const FString& Message) { AddIssue(Out, EJRPGArtIssueSeverity::Error, Key, Message); }
	void IssueWarning(TArray<FJRPGArtIssue>& Out, FName Key, const FString& Message) { AddIssue(Out, EJRPGArtIssueSeverity::Warning, Key, Message); }

	FString CommandText(const TArray<EJRPGArtButton>& Command)
	{
		FString Text;
		for (const EJRPGArtButton Button : Command)
		{
			Text += (Text.IsEmpty() ? TEXT("") : TEXT(", ")) + UJRPGArtsRules::ButtonName(Button);
		}
		return Text;
	}

	FString KeysText(const TArray<FName>& Keys)
	{
		FString Text;
		for (const FName& Key : Keys)
		{
			Text += (Text.IsEmpty() ? TEXT("") : TEXT(", ")) + Key.ToString();
		}
		return Text.IsEmpty() ? TEXT("(nenhuma)") : Text;
	}

	const FJRPGArtData* FindArtRow(const UDataTable* Arts, FName Key)
	{
		return Arts && !Key.IsNone() ? Arts->FindRow<FJRPGArtData>(Key, TEXT("ArtsValidator"), false) : nullptr;
	}

	/** A Sequence de Step leva de volta a Target? (passos que citam a própria linha não contam) */
	bool ReachesRow(const UDataTable* Arts, FName Step, FName Target, TSet<FName>& Visited)
	{
		if (Step == Target)
		{
			return true;
		}
		if (Visited.Contains(Step))
		{
			return false;
		}
		Visited.Add(Step);
		const FJRPGArtData* Row = FindArtRow(Arts, Step);
		if (!Row)
		{
			return false;
		}
		for (const FName& Inner : Row->Sequence)
		{
			if (Inner != Step && ReachesRow(Arts, Inner, Target, Visited))
			{
				return true;
			}
		}
		return false;
	}
}

bool UJRPGArtsValidator::IsBasicStep(FName Step)
{
	static const FName Basic[] = {
		TEXT("SwingL"), TEXT("SwingR"), TEXT("SwingD"), TEXT("SwingU"), TEXT("ArtStarter"), TEXT("SpecialStarter")
	};
	for (const FName& Name : Basic)
	{
		if (Step == Name)
		{
			return true;
		}
	}
	return false;
}

const TMap<FName, TArray<FName>>& UJRPGArtsValidator::DiscSuperChains()
{
	// O "find" de cada Super do original, como Arts (RE docs/formats/art-data.md › Super Arts). Não é a
	// Sequence: o "replace" põe o finalizador no lugar da última Art
	struct FChain
	{
		const TCHAR* Super;
		const TCHAR* Arts[3];
	};
	static const FChain Table[] = {
		{ TEXT("vahn_tri_somersault"),     { TEXT("vahn_somersault"), TEXT("vahn_cyclone"), TEXT("vahn_somersault") } },
		{ TEXT("vahn_maximum_blow"),       { TEXT("vahn_charging_scorch"), TEXT("vahn_slash_kick"), TEXT("vahn_power_punch") } },
		{ TEXT("vahn_fire_tackle"),        { TEXT("vahn_hyper_elbow"), TEXT("vahn_power_punch"), TEXT("vahn_charging_scorch") } },
		{ TEXT("vahn_power_slash"),        { TEXT("vahn_charging_scorch"), TEXT("vahn_somersault"), TEXT("vahn_slash_kick") } },
		{ TEXT("vahn_rolling_combo"),      { TEXT("vahn_spin_combo"), TEXT("vahn_power_punch"), TEXT("vahn_pk_combo") } },
		{ TEXT("noa_triple_lizard"),       { TEXT("noa_bird_step"), TEXT("noa_swan_driver"), TEXT("noa_lizard_tail") } },
		{ TEXT("noa_super_javelin"),       { TEXT("noa_rushing_gale"), TEXT("noa_sonic_javelin"), nullptr } },
		{ TEXT("noa_super_tempest"),       { TEXT("noa_dolphin_attack"), TEXT("noa_tempest_break"), nullptr } },
		{ TEXT("noa_love_you"),            { TEXT("noa_mirage_lancer"), TEXT("noa_lizard_tail"), TEXT("noa_tough_love") } },
		{ TEXT("noa_dragon_fangs"),        { TEXT("noa_lizard_tail"), TEXT("noa_swan_driver"), TEXT("noa_acrobatic_blitz") } },
		{ TEXT("gala_back_punch_x3"),      { TEXT("gala_ironhead"), TEXT("gala_flying_knee_attack"), TEXT("gala_back_punch") } },
		{ TEXT("gala_super_ironhead"),     { TEXT("gala_flying_knee_attack"), TEXT("gala_head_splitter"), TEXT("gala_ironhead") } },
		{ TEXT("gala_rushing_crush"),      { TEXT("gala_battering_ram"), TEXT("gala_flying_knee_attack"), TEXT("gala_head_splitter") } },
		{ TEXT("gala_heavens_drop"),       { TEXT("gala_flying_knee_attack"), TEXT("gala_head_splitter"), TEXT("gala_black_rain") } },
		{ TEXT("gala_neo_static_raising"), { TEXT("gala_back_punch"), TEXT("gala_guillotine"), TEXT("gala_neo_raising") } },
	};
	static const TMap<FName, TArray<FName>> Chains = []()
	{
		TMap<FName, TArray<FName>> Map;
		for (const FChain& Entry : Table)
		{
			TArray<FName>& Arts = Map.Add(FName(Entry.Super));
			for (const TCHAR* Art : Entry.Arts)
			{
				if (Art)
				{
					Arts.Add(FName(Art));
				}
			}
		}
		return Map;
	}();
	return Chains;
}

void UJRPGArtsValidator::ValidateRowLocal(FName Key, const FJRPGArtData& Row, TArray<FJRPGArtIssue>& Out)
{
	const bool bPart = Row.Kind == EJRPGArtKind::Part;
	const bool bSpecial = Row.Kind == EJRPGArtKind::Hyper || Row.Kind == EJRPGArtKind::Super || Row.Kind == EJRPGArtKind::Miracle;

	if (!Row.Key.IsNone() && Row.Key != Key)
	{
		IssueWarning(Out, Key, FString::Printf(TEXT("a coluna Key (%s) não é o row name"), *Row.Key.ToString()));
	}
	if (Row.Character.IsNone())
	{
		IssueError(Out, Key, TEXT("sem Character"));
	}
	if (bPart && !Row.Command.IsEmpty())
	{
		IssueError(Out, Key, TEXT("uma Part não se digita: o Command tem que ficar vazio"));
	}
	if (Row.Command.Num() > UJRPGArtsRules::MaxChainButtons)
	{
		IssueError(Out, Key, FString::Printf(TEXT("Command de %d botões: a cadeia leva no máximo %d"),
			Row.Command.Num(), UJRPGArtsRules::MaxChainButtons));
	}
	if (!bPart && Row.Command.IsEmpty())
	{
		IssueError(Out, Key, TEXT("sem Command: ninguém consegue digitar esta Art (só uma Part não tem)"));
	}
	if (bSpecial && Row.APCost <= 0)
	{
		IssueError(Out, Key, TEXT("um especial (Hyper / Super / Miracle) precisa de APCost"));
	}
	if (Row.Kind == EJRPGArtKind::Regular && Row.APCost > 0)
	{
		IssueError(Out, Key, TEXT("uma Regular soma AP (APGain), não gasta: APCost tem que ser 0"));
	}
	if (Row.HitPower.Num() != Row.HitTarget.Num())
	{
		IssueError(Out, Key, FString::Printf(TEXT("HitPower tem %d golpe(s) e HitTarget %d"), Row.HitPower.Num(), Row.HitTarget.Num()));
	}
	if (Row.SpeedScale < 0)
	{
		IssueError(Out, Key, TEXT("SpeedScale negativo (0 = a regra do original)"));
	}
	if (Row.Unlock == EJRPGArtUnlock::ChainKnown && (Row.Kind == EJRPGArtKind::Regular || Row.Kind == EJRPGArtKind::Hyper))
	{
		IssueWarning(Out, Key, TEXT("Unlock = ChainKnown numa Art solta não tem cadeia: vale como Taught"));
	}
	if (bPart && Row.Unlock != EJRPGArtUnlock::ByKind)
	{
		IssueWarning(Out, Key, TEXT("uma Part não é aprendida: o Unlock dela não faz nada"));
	}
}

void UJRPGArtsValidator::ValidateRow(const UDataTable* Arts, FName Key, const FJRPGArtData& Row, bool bCheckAssets, TArray<FJRPGArtIssue>& Out)
{
	ValidateRowLocal(Key, Row, Out);
	if (!Arts)
	{
		return;
	}
	const bool bPart = Row.Kind == EJRPGArtKind::Part;

	// --- Command: único no herói, e alcançável -------------------------------------------------
	if (!bPart && !Row.Command.IsEmpty())
	{
		for (const TPair<FName, uint8*>& Pair : Arts->GetRowMap())
		{
			const FJRPGArtData* Other = reinterpret_cast<const FJRPGArtData*>(Pair.Value);
			if (Pair.Key != Key && Other && Other->Character == Row.Character && Other->Kind != EJRPGArtKind::Part
				&& Other->Command == Row.Command)
			{
				IssueError(Out, Key, FString::Printf(TEXT("mesmo Command (%s) que %s"), *CommandText(Row.Command), *Pair.Key.ToString()));
			}
		}
		if (Row.Kind == EJRPGArtKind::Regular || Row.Kind == EJRPGArtKind::Hyper)
		{
			// Digitar o Command sozinho tem que dar esta Art, terminando no último botão
			const TArray<FJRPGRecognizedArt> Tokens = UJRPGArtsRules::TokenizeAll(Arts, Row.Character, Row.Command);
			const bool bReached = Tokens.ContainsByPredicate([&](const FJRPGRecognizedArt& T)
			{
				return T.ArtKey == Key && T.FirstButton == 0 && T.LastButton == Row.Command.Num() - 1;
			});
			if (!bReached)
			{
				TArray<FName> Got;
				for (const FJRPGRecognizedArt& T : Tokens)
				{
					Got.Add(T.ArtKey);
				}
				IssueError(Out, Key, FString::Printf(TEXT("nunca é reconhecida: %s dá %s"), *CommandText(Row.Command), *KeysText(Got)));
			}
		}
	}

	// --- Sequence ------------------------------------------------------------------------------
	for (const FName& Step : Row.Sequence)
	{
		if (Step == Key)
		{
			if (Row.Anim.IsNull())
			{
				IssueError(Out, Key, TEXT("a Sequence cita a própria linha, mas ela não tem Anim"));
			}
			continue;
		}
		if (IsBasicStep(Step))
		{
			continue;
		}
		const FJRPGArtData* Other = FindArtRow(Arts, Step);
		if (!Other)
		{
			IssueError(Out, Key, FString::Printf(TEXT("passo da Sequence %s não existe (nem Art, nem clip básico)"), *Step.ToString()));
			continue;
		}
		if (Other->Character != Row.Character)
		{
			IssueError(Out, Key, FString::Printf(TEXT("passo da Sequence %s é de %s"), *Step.ToString(), *Other->Character.ToString()));
		}
		TSet<FName> Visited;
		Visited.Add(Step);
		for (const FName& Inner : Other->Sequence)
		{
			if (Inner != Step && ReachesRow(Arts, Inner, Key, Visited))
			{
				IssueError(Out, Key, FString::Printf(TEXT("passo da Sequence %s volta para esta linha (laço)"), *Step.ToString()));
				break;
			}
		}
	}

	// --- Supers --------------------------------------------------------------------------------
	if (Row.Kind == EJRPGArtKind::Super && !Row.Command.IsEmpty())
	{
		const TArray<FName> Own = UJRPGArtsRules::GetSpecialOwnArts(Arts, Key);
		if (Own.IsEmpty() && UJRPGArtsRules::EffectiveUnlock(Row) == EJRPGArtUnlock::ChainKnown)
		{
			IssueWarning(Out, Key, TEXT("o Command não tem Art nenhuma: como ChainKnown sairia sem o jogador saber nada (dê Unlock = Taught)"));
		}
		if (const TArray<FName>* Original = DiscSuperChains().Find(Key))
		{
			if (Own != *Original)
			{
				IssueError(Out, Key, FString::Printf(TEXT("o Command tokeniza em %s; no original a cadeia é %s"), *KeysText(Own), *KeysText(*Original)));
			}
		}
	}

	// --- Assets --------------------------------------------------------------------------------
	if (bCheckAssets && !Row.Anim.IsNull())
	{
		const UAnimSequence* Clip = Row.Anim.LoadSynchronous();
		if (!Clip)
		{
			IssueError(Out, Key, FString::Printf(TEXT("o Anim %s não carrega"), *Row.Anim.ToString()));
			return;
		}
		const UDataTable* Characters = UCoreSubsystem::ResolveDataTable(TEXT("DT_Characters"));
		const FCharacterData* Hero = Characters ? Characters->FindRow<FCharacterData>(Row.Character, TEXT("ArtsValidator"), false) : nullptr;
		const USkeletalMesh* Mesh = Hero ? Hero->BattleMesh.LoadSynchronous() : nullptr;
		if (Mesh && Mesh->GetSkeleton() && Clip->GetSkeleton() != Mesh->GetSkeleton())
		{
			IssueError(Out, Key, FString::Printf(TEXT("o Anim não é do esqueleto de batalha de %s (%s)"),
				*Row.Character.ToString(), *GetNameSafe(Mesh->GetSkeleton())));
		}
		const int32 Hits = UJRPGAnimNotify_Hit::FindHitTimes(Clip).Num();
		if (Hits > 0 && Hits != Row.HitPower.Num())
		{
			IssueWarning(Out, Key, FString::Printf(TEXT("o clip tem %d notify(s) de golpe e o HitPower %d golpe(s)"), Hits, Row.HitPower.Num()));
		}
	}
}

TArray<FJRPGArtIssue> UJRPGArtsValidator::ValidateArts(const UDataTable* Arts, bool bCheckAssets)
{
	TArray<FJRPGArtIssue> Out;
	if (!Arts || Arts->GetRowStruct() != FJRPGArtData::StaticStruct())
	{
		IssueError(Out, NAME_None, TEXT("a tabela não é um DT_Arts (FJRPGArtData)"));
		return Out;
	}
	for (const TPair<FName, uint8*>& Pair : Arts->GetRowMap())
	{
		if (const FJRPGArtData* Row = reinterpret_cast<const FJRPGArtData*>(Pair.Value))
		{
			ValidateRow(Arts, Pair.Key, *Row, bCheckAssets, Out);
		}
	}
	return Out;
}

#if WITH_EDITOR
EDataValidationResult FJRPGArtData::IsDataValid(FDataValidationContext& Context) const
{
	// A linha não sabe de que tabela é: se é a do DT_Arts que o jogo usa, as regras que olham as outras
	// linhas também valem; senão, só as da linha sozinha
	TArray<FJRPGArtIssue> Issues;
	const UDataTable* Table = UCoreSubsystem::ResolveDataTable(TEXT("DT_Arts"));
	const FJRPGArtData* Mine = Table && !Key.IsNone() ? Table->FindRow<FJRPGArtData>(Key, TEXT("ArtsValidator"), false) : nullptr;
	if (Mine == this)
	{
		UJRPGArtsValidator::ValidateRow(Table, Key, *this, true, Issues);
	}
	else
	{
		UJRPGArtsValidator::ValidateRowLocal(Key, *this, Issues);
	}
	EDataValidationResult Result = EDataValidationResult::Valid;
	for (const FJRPGArtIssue& Issue : Issues)
	{
		const FText Text = FText::FromString(Issue.Message);
		if (Issue.Severity == EJRPGArtIssueSeverity::Error)
		{
			Context.AddError(Text);
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			Context.AddWarning(Text);
		}
	}
	return Result;
}
#endif
