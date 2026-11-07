#include "Battle/JRPGBattleUISubsystem.h"
#include "Arts/ArtsSubsystem.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/EnemyData.h"
#include "Battle/JRPGBattleSceneSubsystem.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Core/CoreSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Party/CharacterData.h"
#include "TimerManager.h"
#include "UI/WebUISubsystem.h"
#include "UI/WebUIScripting.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Battle/JRPGBattlePawn.h"
#include "Battle/JRPGBattleArena.h"

bool UJRPGBattleUISubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UJRPGBattleUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// A cena é quem diz quando a luta está de pé (OnSceneAssembled): tem que existir antes
	Collection.InitializeDependency<UJRPGBattleSceneSubsystem>();
	Super::Initialize(Collection);
}

UBattleSubsystem* UJRPGBattleUISubsystem::GetBattle() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UBattleSubsystem>() : nullptr;
}

UWebUISubsystem* UJRPGBattleUISubsystem::GetWebUI() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWebUISubsystem>() : nullptr;
}

void UJRPGBattleUISubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (UBattleSubsystem* Battle = GetBattle())
	{
		Battle->OnBattleStarted.AddDynamic(this, &UJRPGBattleUISubsystem::HandleBattleStarted);
		Battle->OnBattleEnded.AddDynamic(this, &UJRPGBattleUISubsystem::HandleBattleEnded);
		Battle->OnSlotChanged.AddDynamic(this, &UJRPGBattleUISubsystem::HandleSlotChanged);
		Battle->OnCommandRefused.AddDynamic(this, &UJRPGBattleUISubsystem::HandleCommandRefused);
	}
	if (UJRPGBattleSceneSubsystem* Scene = InWorld.GetSubsystem<UJRPGBattleSceneSubsystem>())
	{
		Scene->OnSceneAssembled.AddDynamic(this, &UJRPGBattleUISubsystem::HandleSceneAssembled);
		Scene->OnFieldShown.AddDynamic(this, &UJRPGBattleUISubsystem::HandleFieldShown);
		Scene->OnIntroStageChanged.AddDynamic(this, &UJRPGBattleUISubsystem::HandleIntroStage);
	}
	if (UWebUISubsystem* WebUI = GetWebUI())
	{
		WebUI->OnBattleUIInput.AddUObject(this, &UJRPGBattleUISubsystem::HandleUIInput);
	}
}

void UJRPGBattleUISubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoTimer);
		if (UBattleSubsystem* Battle = GetBattle())
		{
			Battle->OnBattleStarted.RemoveDynamic(this, &UJRPGBattleUISubsystem::HandleBattleStarted);
			Battle->OnBattleEnded.RemoveDynamic(this, &UJRPGBattleUISubsystem::HandleBattleEnded);
			Battle->OnSlotChanged.RemoveDynamic(this, &UJRPGBattleUISubsystem::HandleSlotChanged);
			Battle->OnCommandRefused.RemoveDynamic(this, &UJRPGBattleUISubsystem::HandleCommandRefused);
		}
		if (UJRPGBattleSceneSubsystem* Scene = World->GetSubsystem<UJRPGBattleSceneSubsystem>())
		{
			Scene->OnSceneAssembled.RemoveDynamic(this, &UJRPGBattleUISubsystem::HandleSceneAssembled);
			Scene->OnFieldShown.RemoveDynamic(this, &UJRPGBattleUISubsystem::HandleFieldShown);
			Scene->OnIntroStageChanged.RemoveDynamic(this, &UJRPGBattleUISubsystem::HandleIntroStage);
			Scene->SetDebugPromptSuppressed(false);
		}
		if (UWebUISubsystem* WebUI = GetWebUI())
		{
			WebUI->OnBattleUIInput.RemoveAll(this);
			HideIntroBanner();
			// O mapa saiu com o menu na tela: devolve o input ao jogo
			if (bMenuUp)
			{
				WebUI->CloseBattleUI();
			}
		}
	}
	bMenuUp = false;
	bFightOn = false;
	bSceneUp = false;
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// A luta e a cena
// ---------------------------------------------------------------------------------------------

void UJRPGBattleUISubsystem::HandleBattleStarted(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants)
{
	// Uma luta nova: táticas e auto zerados; a UI espera a cena de pé
	HideMenu();
	Menu.ResetFight();
	bFightOn = true;
	LastRefusal.Reset();
	// Com a transição em 0 a cena monta dentro do próprio OnBattleStarted, às vezes antes deste handler: confere
	const UJRPGBattleSceneSubsystem* Scene = GetWorld() ? GetWorld()->GetSubsystem<UJRPGBattleSceneSubsystem>() : nullptr;
	bSceneUp = Scene && Scene->IsFieldHidden() && Scene->GetArena() != nullptr;
	TryShowMenu();
}

void UJRPGBattleUISubsystem::HandleBattleEnded(EJRPGBattleResult Result)
{
	bFightOn = false;
	bSceneUp = false;
	HideIntroBanner();
	HideMenu();
	Menu.ResetFight();
}

void UJRPGBattleUISubsystem::HandleFieldShown()
{
	bSceneUp = false;
	HideIntroBanner();
}

void UJRPGBattleUISubsystem::HandleSceneAssembled(AJRPGBattleArena* Arena, const TArray<AJRPGBattlePawn*>& Pawns)
{
	// Com a abertura rodando, o menu espera o fim dela (HandleIntroStage None): nenhum prompt debaixo da varredura nem dos nomes
	const UJRPGBattleSceneSubsystem* Scene = GetWorld() ? GetWorld()->GetSubsystem<UJRPGBattleSceneSubsystem>() : nullptr;
	if (Scene && Scene->GetIntroStage() != EJRPGBattleIntroStage::None)
	{
		return;
	}
	bSceneUp = true;
	TryShowMenu();
}

void UJRPGBattleUISubsystem::HandleIntroStage(EJRPGBattleIntroStage Stage)
{
	switch (Stage)
	{
	case EJRPGBattleIntroStage::Names:
		ShowIntroBanner();
		break;
	case EJRPGBattleIntroStage::None:
		HideIntroBanner();
		if (bFightOn)
		{
			bSceneUp = true;
			TryShowMenu();
		}
		break;
	default:
		break;
	}
}

void UJRPGBattleUISubsystem::ShowIntroBanner()
{
	UWorld* World = GetWorld();
	UBattleSubsystem* Battle = GetBattle();
	UWebUISubsystem* WebUI = GetWebUI();
	const UJRPGBattleSceneSubsystem* Scene = World ? World->GetSubsystem<UJRPGBattleSceneSubsystem>() : nullptr;
	const AJRPGBattleArena* Arena = Scene ? Scene->GetArena() : nullptr;
	if (!bUIEnabled || !World || !Battle || !Battle->IsInBattle() || !WebUI)
	{
		return;
	}

	// Onde um ponto do mundo cai na tela, em fração da largura (o JS põe no canvas dele). O original centra pelo X de mundo do grupo,
	// que com a câmera do caso 2 (de trás da party, giro 0) é o mesmo que a posição na tela
	APlayerController* PC = World->GetFirstPlayerController();
	FVector2D ViewportSize(1.0, 1.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	auto ScreenX = [PC, &ViewportSize](const FVector& Where) -> double
	{
		FVector2D Screen;
		if (PC && ViewportSize.X > 1.0 && PC->ProjectWorldLocationToScreen(Where, Screen, false))
		{
			return FMath::Clamp(Screen.X / ViewportSize.X, 0.0, 1.0);
		}
		return 0.5;
	};

	// Um rótulo por grupo de inimigos iguais em slots seguidos (o original corre os quatro ids e junta os iguais em sequência): o
	// nome do primeiro, e "* N" quando há mais
	struct FGroup
	{
		FName Key;
		FString Name;
		int32 Count = 0;
		FVector Sum = FVector::ZeroVector;
		int32 Placed = 0;
	};
	TArray<FGroup> Groups;
	FString Leader;
	int32 PartyCount = 0;
	const UDataTable* Characters = UCoreSubsystem::ResolveDataTable(TEXT("DT_Characters"));
	for (const FJRPGCombatant& C : Battle->GetCombatants())
	{
		if (C.Side != EJRPGBattleSide::Enemy)
		{
			// O líder é o primeiro assento da party (DAT_8007BD10[0]); a linha fala do time quando há um segundo
			if (PartyCount++ == 0)
			{
				Leader = C.Key.ToString();
				if (const FCharacterData* Row = Characters ? Characters->FindRow<FCharacterData>(C.Key, TEXT("BattleUI"), false) : nullptr)
				{
					if (!Row->DisplayName.IsEmpty())
					{
						Leader = Row->DisplayName.ToString();
					}
				}
			}
			continue;
		}
		if (!C.bAlive)
		{
			continue;
		}
		if (Groups.Num() == 0 || Groups.Last().Key != C.Key)
		{
			FGroup& Group = Groups.AddDefaulted_GetRef();
			Group.Key = C.Key;
			FJRPGEnemyData Row;
			Group.Name = UJRPGEncounterLibrary::GetEnemyData(C.Key, Row) && !Row.DisplayName.IsEmpty() ? Row.DisplayName.ToString() : C.Key.ToString();
		}
		FGroup& Group = Groups.Last();
		++Group.Count;
		if (const AJRPGBattlePawn* Pawn = Arena ? Arena->GetPawnForSlot(C.SlotIndex) : nullptr)
		{
			Group.Sum += Pawn->GetBodyLocation();
			++Group.Placed;
		}
	}

	FString GroupsJS;
	for (const FGroup& Group : Groups)
	{
		const FString Label = Group.Count > 1 ? FString::Printf(TEXT("%s * %d"), *Group.Name, Group.Count) : Group.Name;
		const double X = Group.Placed > 0 ? ScreenX(Group.Sum / Group.Placed) : 0.5;
		GroupsJS += FString::Printf(TEXT("%s{name:%s,x:%s}"), GroupsJS.IsEmpty() ? TEXT("") : TEXT(","), *JRPGWebUI::ToJSStringLiteral(Label),
			*FString::SanitizeFloat(X));
	}

	// A linha de formação (as strings do PROT 0898 em 0x801F4CD8 / 0x801F4CF8 / 0x801F4D10)
	FString Line;
	switch (Battle->GetState().FormationAdvantage)
	{
	case 1:
		Line = TEXT("Ambushed!");
		break;
	case 2:
		Line = PartyCount > 1 ? FString::Printf(TEXT("%s's team surprised the enemy."), *Leader)
			: FString::Printf(TEXT("%s surprised the enemy."), *Leader);
		break;
	default:
		break;
	}

	WebUI->CallBattleHUD(FString::Printf(TEXT("intro({groups:[%s],line:%s})"), *GroupsJS, *JRPGWebUI::ToJSStringLiteral(Line)));
	bIntroBannerUp = true;
}

void UJRPGBattleUISubsystem::HideIntroBanner()
{
	if (!bIntroBannerUp)
	{
		return;
	}
	bIntroBannerUp = false;
	if (UWebUISubsystem* WebUI = GetWebUI())
	{
		WebUI->CallBattleHUD(TEXT("introHide()"));
	}
}

void UJRPGBattleUISubsystem::HandleSlotChanged(int32 Slot, EJRPGBattleStage Stage)
{
	// O próprio envio do Begin muda o slot a cada comando aceito: nada abre nem fecha no meio dele
	if (!bFightOn || bCommitting)
	{
		return;
	}

	if (Stage == EJRPGBattleStage::Commands && Slot >= 0)
	{
		if (!bMenuUp)
		{
			// A fase de comandos de uma rodada nova
			TryShowMenu();
			return;
		}
		// O menu está de pé e o núcleo mudou por fora (o comando de console, por exemplo): se quem falta mudou, recomeça
		const UBattleSubsystem* Battle = GetBattle();
		if (Battle && Battle->IsInBattle())
		{
			const FJRPGBattleMenuContext Fresh = BuildContext(*Battle);
			if (Fresh.HeroSlotsNeedingCommand() != Menu.GetPlan().HeroSlots)
			{
				UE_LOG(LogTemp, Log, TEXT("BattleUI: os comandos mudaram por fora do menu — ele recomeça na tela inicial"));
				Menu.BeginRound(Fresh);
				if (Menu.GetPlan().IsEmpty())
				{
					HideMenu();
				}
				else
				{
					Render();
				}
			}
		}
		return;
	}

	// A rodada toca (Execution) ou a luta acabou: a UI sai de cena
	HideMenu();
}

void UJRPGBattleUISubsystem::HandleCommandRefused(int32 Slot, const FString& Why)
{
	// Guardado para o CommitPlan mostrar o motivo na tela
	LastRefusal = Why;
}

// ---------------------------------------------------------------------------------------------
// Mostrar e esconder
// ---------------------------------------------------------------------------------------------

void UJRPGBattleUISubsystem::TryShowMenu()
{
	if (!bUIEnabled || !bFightOn || !bSceneUp || bMenuUp || bCommitting)
	{
		return;
	}
	UBattleSubsystem* Battle = GetBattle();
	if (!Battle || !Battle->IsInBattle() || Battle->GetState().Stage != EJRPGBattleStage::Commands || Battle->GetActiveSlot() < 0)
	{
		return;
	}

	Menu.BeginRound(BuildContext(*Battle));
	if (Menu.GetPlan().IsEmpty())
	{
		// Ninguém para comandar (os heróis são da IA): a rodada anda sem a tela
		return;
	}

	UWebUISubsystem* WebUI = GetWebUI();
	if (!WebUI || !WebUI->OpenBattleUI())
	{
		// A WebUI está ocupada (o menu de pausa aberto, por exemplo): o prompt de debug e o console seguem valendo
		UE_LOG(LogTemp, Warning, TEXT("BattleUI: a WebUI não abriu o menu da luta — use os comandos JRPG.Battle.* (Docs/Guides/BATTLE.md)"));
		return;
	}
	bMenuUp = true;
	if (UJRPGBattleSceneSubsystem* Scene = GetWorld()->GetSubsystem<UJRPGBattleSceneSubsystem>())
	{
		Scene->SetDebugPromptSuppressed(true);
	}
	Render();
	if (Menu.IsAutoPending())
	{
		StartAutoTimer();
	}
}

void UJRPGBattleUISubsystem::HideMenu()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoTimer);
	}
	if (!bMenuUp)
	{
		return;
	}
	bMenuUp = false;
	Menu.ClearAutoPending();
	if (UWebUISubsystem* WebUI = GetWebUI())
	{
		WebUI->CloseBattleUI();
	}
	if (UWorld* World = GetWorld())
	{
		if (UJRPGBattleSceneSubsystem* Scene = World->GetSubsystem<UJRPGBattleSceneSubsystem>())
		{
			Scene->SetDebugPromptSuppressed(false);
		}
	}
}

void UJRPGBattleUISubsystem::Render(const TCHAR* Sfx)
{
	if (!bMenuUp)
	{
		return;
	}
	if (UWebUISubsystem* WebUI = GetWebUI())
	{
		WebUI->UpdateBattleUI(Menu.BuildViewJS(Sfx));
	}
}

// ---------------------------------------------------------------------------------------------
// A foto da rodada
// ---------------------------------------------------------------------------------------------

FJRPGBattleMenuContext UJRPGBattleUISubsystem::BuildContext(const UBattleSubsystem& Battle) const
{
	FJRPGBattleMenuContext Ctx;
	const FJRPGBattleState& State = Battle.GetState();
	Ctx.Round = State.Round;
	Ctx.PlayerIndex = LocalPlayerIndex;
	// A luta roteirizada não se foge (a regra do núcleo: FJRPGBattleSetup::bScriptedFight)
	Ctx.bCanEscape = !State.bScriptedFight;

	const UDataTable* Characters = UCoreSubsystem::ResolveDataTable(TEXT("DT_Characters"));
	TArray<FName> EnemyKeys;
	TArray<FString> EnemyNames;
	TArray<const FJRPGCombatant*> EnemyCombatants;

	for (const FJRPGCombatant& C : Battle.GetCombatants())
	{
		if (C.Side == EJRPGBattleSide::Enemy)
		{
			FJRPGEnemyData Row;
			const bool bFound = UJRPGEncounterLibrary::GetEnemyData(C.Key, Row);
			EnemyKeys.Add(C.Key);
			EnemyNames.Add(bFound && !Row.DisplayName.IsEmpty() ? Row.DisplayName.ToString() : C.Key.ToString());
			EnemyCombatants.Add(&C);
			continue;
		}

		FJRPGBattleMenuHero Hero;
		Hero.Slot = C.SlotIndex;
		Hero.Key = C.Key;
		Hero.Name = C.Key.ToString();
		Hero.Level = C.Level;
		Hero.HP = C.HP;
		Hero.MaxHP = C.MaxHP;
		Hero.MP = C.MP;
		Hero.MaxMP = C.MaxMP;
		Hero.AP = C.AP;
		Hero.MaxAP = FMath::Max(C.MaxAP, 1);
		Hero.Gauge = C.CommandGauge;
		Hero.ButtonCost = C.ButtonCost;
		Hero.bLeftHanded = C.bLeftHanded;
		Hero.bAlive = C.bAlive;
		// Só os heróis deste jogador, ainda sem comando nesta rodada, entram no buffer (o dono do slot é conferido pelo núcleo)
		Hero.bNeedsCommand = C.bAlive && C.OwnerPlayer == LocalPlayerIndex && State.Commands.IsValidIndex(C.SlotIndex)
			&& State.Commands[C.SlotIndex].Kind == EJRPGBattleCommandKind::None;

		if (const FCharacterData* Row = Characters ? Characters->FindRow<FCharacterData>(C.Key, TEXT("BattleUI"), false) : nullptr)
		{
			if (!Row->DisplayName.IsEmpty())
			{
				Hero.Name = Row->DisplayName.ToString();
			}
			Hero.RaSeruName = Row->RaSeru.IsNone() ? FString() : Row->RaSeru.ToString();
		}

		// As Artes Regulares que ele conhece: o que a tática Aggressive pode escolher (as Hyper / Super gastam AP)
		for (const FName& ArtKey : C.KnownArts)
		{
			const FJRPGArtData* Art = UArtsSubsystem::FindHeroArt(C.Key, ArtKey);
			if (Art && Art->Kind == EJRPGArtKind::Regular && !Art->Command.IsEmpty())
			{
				Hero.KnownArtCommands.Add(Art->Command);
			}
			// A lista de consulta (o painel do Triângulo): toda Art que se digita (as Parts não)
			if (Art && Art->Kind != EJRPGArtKind::Part && !Art->Command.IsEmpty())
			{
				FJRPGBattleMenuArt& Entry = Hero.ArtList.AddDefaulted_GetRef();
				Entry.Name = Art->DisplayName.IsEmpty() ? ArtKey.ToString() : Art->DisplayName.ToString();
				Entry.Kind = Art->Kind;
				Entry.bCost = Art->Kind != EJRPGArtKind::Regular;
				Entry.AP = Entry.bCost ? Art->APCost : Art->APGain;
				Entry.Buttons = Art->Command;
			}
		}
		// A ordem do painel: a Miracle sempre no topo, depois as Hyper, as Super e as Regulares; em cada grupo, mais AP primeiro
		auto GroupOf = [](EJRPGArtKind Kind)
		{
			switch (Kind)
			{
			case EJRPGArtKind::Miracle: return 0;
			case EJRPGArtKind::Hyper:   return 1;
			case EJRPGArtKind::Super:   return 2;
			default:                    return 3;
			}
		};
		Hero.ArtList.StableSort([&GroupOf](const FJRPGBattleMenuArt& A, const FJRPGBattleMenuArt& B)
		{
			const int32 GA = GroupOf(A.Kind);
			const int32 GB = GroupOf(B.Kind);
			if (GA != GB)
			{
				return GA < GB;
			}
			if (A.AP != B.AP)
			{
				return A.AP > B.AP;
			}
			return A.Name < B.Name;
		});
		Ctx.Heroes.Add(Hero);
	}

	// Os inimigos com o sufixo A / B ("Frog A"); o nome vale a luta toda, então conta os vivos e os caídos
	const TArray<FString> Labels = JRPGBattleMenu::MakeEnemyLabels(EnemyKeys, EnemyNames);
	for (int32 i = 0; i < EnemyCombatants.Num(); ++i)
	{
		FJRPGBattleMenuEnemy Enemy;
		Enemy.Slot = EnemyCombatants[i]->SlotIndex;
		Enemy.Key = EnemyCombatants[i]->Key;
		Enemy.Name = Labels[i];
		Enemy.bAlive = EnemyCombatants[i]->bAlive;
		Ctx.Enemies.Add(Enemy);
	}
	return Ctx;
}

// ---------------------------------------------------------------------------------------------
// Os apertos
// ---------------------------------------------------------------------------------------------

void UJRPGBattleUISubsystem::HandleUIInput(const FString& Action, int32 A, int32 B)
{
	// Nada vindo da UI vale sem o menu de pé; os índices são conferidos dentro do menu (regra 22)
	if (!bMenuUp || bCommitting)
	{
		return;
	}

	FJRPGBattleMenuResult Result;
	if (Action == TEXT("pick"))
	{
		Result = Menu.Pick(A, B);
	}
	else if (Action == TEXT("up"))
	{
		Result = Menu.Press(EJRPGBattleMenuKey::Up);
	}
	else if (Action == TEXT("down"))
	{
		Result = Menu.Press(EJRPGBattleMenuKey::Down);
	}
	else if (Action == TEXT("left"))
	{
		Result = Menu.Press(EJRPGBattleMenuKey::Left);
	}
	else if (Action == TEXT("right"))
	{
		Result = Menu.Press(EJRPGBattleMenuKey::Right);
	}
	else if (Action == TEXT("confirm"))
	{
		Result = Menu.Press(EJRPGBattleMenuKey::Confirm);
	}
	else if (Action == TEXT("cancel"))
	{
		Result = Menu.Press(EJRPGBattleMenuKey::Cancel);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleUI: ação '%s' desconhecida — ignorada"), *Action);
		return;
	}
	ApplyResult(Result);
}

void UJRPGBattleUISubsystem::ApplyResult(const FJRPGBattleMenuResult& Result)
{
	switch (Result.Request)
	{
	case EJRPGBattleMenuRequest::Commit:
		CommitPlan();
		return;
	case EJRPGBattleMenuRequest::Escape:
		DoEscape();
		return;
	case EJRPGBattleMenuRequest::AutoRound:
		RunAutoRound();
		return;
	case EJRPGBattleMenuRequest::AutoOff:
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AutoTimer);
		}
		break;
	default:
		break;
	}
	if (Result.bChanged || (Result.Sfx && Result.Sfx[0] != TEXT('\0')))
	{
		Render(Result.Sfx);
	}
}

void UJRPGBattleUISubsystem::CommitPlan()
{
	UBattleSubsystem* Battle = GetBattle();
	if (!Battle || !Battle->IsInBattle())
	{
		HideMenu();
		return;
	}

	LastRefusal.Reset();
	bCommitting = true;
	int32 RefusedIndex = INDEX_NONE;
	// Em ordem, um a um, COM O JOGADOR LOCAL (o comando já leva o PlayerIndex dele): o núcleo confere o dono (regra 22)
	const int32 Accepted = Menu.GetPlan().Commit([Battle](const FJRPGBattleCommand& Chosen)
	{
		return Battle->SubmitCommand(Chosen);
	}, &RefusedIndex);
	bCommitting = false;

	const bool bWholePlan = Accepted == Menu.GetPlan().Num() && Menu.GetPlan().Num() > 0;
	const EJRPGBattleStage Stage = Battle->GetState().Stage;
	if (Battle->IsInBattle() && Stage == EJRPGBattleStage::Commands && Battle->GetActiveSlot() >= 0)
	{
		// Alguém ficou sem comando (o núcleo recusou um, ou a rodada ainda espera outro): recomeça só com quem falta,
		// com o motivo na tela, e o auto desligado (não vai repetir a recusa sozinho)
		const FString Why = !LastRefusal.IsEmpty() ? LastRefusal : FString(TEXT("The command was not accepted."));
		UE_LOG(LogTemp, Warning, TEXT("BattleUI: o núcleo aceitou %d de %d comandos — %s"), Accepted, Menu.GetPlan().Num(), *Why);
		Menu.TurnAutoOff();
		Menu.BeginRound(BuildContext(*Battle));
		Menu.Refusal = Why;
		if (Menu.GetPlan().IsEmpty())
		{
			HideMenu();
		}
		else
		{
			Render(TEXT("Cancel"));
		}
		return;
	}
	if (!bWholePlan)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleUI: o plano foi mandado incompleto (%d de %d), mas a rodada já seguiu"), Accepted, Menu.GetPlan().Num());
	}
	// A rodada toca (ou a luta acabou): a UI sai e volta na próxima fase de comandos
	HideMenu();
}

void UJRPGBattleUISubsystem::RunAutoRound()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoTimer);
	}
	Menu.ClearAutoPending();
	if (!Menu.PlanAuto())
	{
		// Sem alvo ou sem comando possível: o auto não segura a luta, volta ao manual
		UE_LOG(LogTemp, Warning, TEXT("BattleUI: o auto não conseguiu montar a rodada — volta ao manual"));
		Menu.TurnAutoOff();
		Menu.Refusal = TEXT("Auto-battle could not plan the round.");
		Render(TEXT("Cancel"));
		return;
	}
	CommitPlan();
}

void UJRPGBattleUISubsystem::DoEscape()
{
	UBattleSubsystem* Battle = GetBattle();
	if (!Battle || !Battle->IsInBattle())
	{
		return;
	}
	// Quem decide é o núcleo, não a tela (regra 39): a luta roteirizada não foge nem se o payload disser que sim
	if (Battle->GetState().bScriptedFight)
	{
		Menu.BeginRound(BuildContext(*Battle));
		Menu.Refusal = TEXT("There is no escaping this fight.");
		Render(TEXT("Cancel"));
		return;
	}
	// TODO(Fase 7): o sorteio da fuga do original (FUN_801E791C: a party (SPD*3)>>1 + o HP que falta >>4 contra os inimigos
	// SPD + o HP que falta >>5, dois sorteios). Até lá a fuga sempre dá certo e termina a luta como Escaped
	// (BATTLE_ROADMAP.md › Phase 7 › Escape).
	UE_LOG(LogTemp, Log, TEXT("BattleUI: fuga escolhida — a luta termina como Escaped (sem o sorteio da fuga ainda)"));
	Battle->EndBattle(EJRPGBattleResult::Escaped);
}

// ---------------------------------------------------------------------------------------------
// O auto
// ---------------------------------------------------------------------------------------------

void UJRPGBattleUISubsystem::StartAutoTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(AutoTimer);
	if (AutoStartDelay <= 0.0f)
	{
		RunAutoRound();
		return;
	}
	World->GetTimerManager().SetTimer(AutoTimer, FTimerDelegate::CreateUObject(this, &UJRPGBattleUISubsystem::OnAutoTimer), AutoStartDelay, false);
}

void UJRPGBattleUISubsystem::OnAutoTimer()
{
	// O jogador pode ter cancelado (o auto desligou) ou a luta ter mudado nesse meio-tempo
	if (bMenuUp && Menu.IsAutoPending())
	{
		RunAutoRound();
	}
}
