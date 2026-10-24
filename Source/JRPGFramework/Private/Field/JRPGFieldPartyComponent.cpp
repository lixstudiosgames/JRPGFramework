#include "Field/JRPGFieldPartyComponent.h"
#include "Field/JRPGFieldCharacter.h"
#include "Field/JRPGFieldMapSettings.h"
#include "Field/JRPGFieldPlayerController.h"
#include "Field/JRPGPartyFollowerController.h"
#include "Party/PartySubsystem.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace
{
	/** Distância entre dois pontos gravados da trilha, em uu. */
	constexpr float TrailSampleSpacing = 15.0f;
	/** Quanto à frente do cursor o seguidor mira na trilha: faz a curva sem cortar a quina. */
	constexpr float TrailLookahead = 60.0f;
	/** Quanto o cursor pode voltar na trilha (o seguidor empurrado para trás). */
	constexpr float TrailBacktrack = 60.0f;
	/** Pular adiante na trilha só estando em cima dela, a até esta distância (uu, no chão). */
	constexpr float TrailShortcutRadius = 40.0f;
	/** Diferença de altura a partir da qual um trecho da trilha é de outro andar. */
	constexpr float TrailMaxHeightGap = 150.0f;
	/** Trilha guardada além do último seguidor, em uu. */
	constexpr float TrailExtraLength = 600.0f;
	/** O líder andou isto num tick só: foi teleportado (portal, load, cutscene). */
	constexpr float LeaderJumpDistance = 600.0f;
	/** Abaixo disto o líder está parado (uu/s). */
	constexpr float IdleSpeed = 10.0f;
	/** Perto assim do lugar dele, o seguidor para. */
	constexpr float ArriveDistance = 15.0f;
	/** A partir desta distância do lugar, o seguidor vai com o direcional inteiro. */
	constexpr float SlowdownDistance = 100.0f;
	/** Tentando andar abaixo desta velocidade, o seguidor está preso (uu/s). */
	constexpr float StuckSpeed = 20.0f;
	/** De quanto em quanto tempo cada seguidor procura um atalho (s). */
	constexpr float ShortcutInterval = 0.25f;
	/** Passo entre os pontos da trilha testados como destino do atalho, em uu. */
	constexpr float ShortcutSampleStep = 40.0f;
	/** No máximo tantas checagens de caminho (raycast/sweep) por procura. */
	constexpr int32 ShortcutMaxChecks = 6;
	/** Perto assim do ponto do atalho, ele volta a andar pela trilha. */
	constexpr float ShortcutArriveDistance = 40.0f;
	/** Leash, líder parado: o seguidor só vai para o lugar ao lado se está mais longe que isto dele. */
	constexpr float LeashSlotTolerance = 100.0f;
	/** Leash: outro membro mais perto que isto está em cima dele (atravessam pawns). */
	constexpr float LeashPersonalSpace = 60.0f;
	/** Leash, andando junto: o seguidor acompanha o líder esta distância atrás do lugar dele. */
	constexpr float LeashTrailGap = 150.0f;
	/** Leash: quanto a velocidade sobe (uu/s) por uu além do LeashTrailGap. */
	constexpr float LeashSpeedGain = 2.0f;
	/** Leash: o MoveTo terminou antes (chegou no fim do caminho): refaz depois disto (s). */
	constexpr float LeashIdleRepathDelay = 0.1f;

	/** Até onde procurar o navmesh em volta de um ponto. */
	const FVector NavProjectExtent(60.0f, 60.0f, 250.0f);
}

UJRPGFieldPartyComponent::UJRPGFieldPartyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	// Ao lado e um pouco atrás: um de cada lado, o terceiro logo atrás, depois as diagonais
	GatherAngles = { -110.0f, 110.0f, 180.0f, -145.0f, 145.0f };
}

void UJRPGFieldPartyComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AJRPGFieldMapSettings* Map = AJRPGFieldMapSettings::Find(GetWorld()))
	{
		bPartyAllowedInMap = Map->bShowParty;
	}
	if (UPartySubsystem* Party = GetParty())
	{
		Party->OnPartyChanged.AddUniqueDynamic(this, &UJRPGFieldPartyComponent::HandlePartyChanged);
	}

	// O pawn pode ter sido possuído antes do BeginPlay do controller
	if (const AJRPGFieldPlayerController* PC = GetFieldController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			HandlePawnPossessed(Pawn);
		}
	}
}

void UJRPGFieldPartyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UPartySubsystem* Party = GetParty())
	{
		Party->OnPartyChanged.RemoveDynamic(this, &UJRPGFieldPartyComponent::HandlePartyChanged);
	}
	Super::EndPlay(EndPlayReason);
}

// ============================================================
// QUEM ESTÁ NO MAPA
// ============================================================

void UJRPGFieldPartyComponent::HandlePawnPossessed(APawn* NewPawn)
{
	// A troca do R3 possui o próximo pawn por conta própria; antes do BeginPlay, ele pega o pawn
	if (bSwitching || !NewPawn || !HasBegunPlay())
	{
		return;
	}
	// Um tick depois: o pawn do GameMode (ou do Load) termina de nascer primeiro
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UJRPGFieldPartyComponent::SetupForPossessedPawn));
	}
}

void UJRPGFieldPartyComponent::SetupForPossessedPawn()
{
	AJRPGFieldCharacter* Leader = GetLeader();
	UPartySubsystem* Party = GetParty();
	if (!Leader || !Party || Leader->IsPartyFollower())
	{
		return;
	}

	const TArray<FName> Formation = Party->GetActiveParty();
	if (Leader->PartyKey.IsNone())
	{
		if (Formation.Num() == 0)
		{
			return; // sem roster (PIE direto no mapa, sem New Game): só o pawn, sem party
		}
		Leader->PartyKey = Formation[0];
		UE_LOG(LogTemp, Log, TEXT("JRPGFieldParty: o pawn '%s' não tem PartyKey — tratado como %s (o primeiro da formação)."),
			*Leader->GetName(), *Formation[0].ToString());
	}
	PartyPawns.Add(Leader->PartyKey, Leader);
	ResetTrail(Leader->GetActorLocation());

	// Quem o jogador escolheu no mapa anterior (depois de um load, o primeiro da formação). Ele
	// assume o lugar exato do pawn do GameMode — o PlayerStart do portal, a posição do save
	const FName Chosen = Party->GetFieldLeader();
	if (!Chosen.IsNone() && Chosen != Leader->PartyKey)
	{
		if (AJRPGFieldCharacter* ChosenPawn = FindOrSpawnMember(Chosen))
		{
			DoSwitchLeader(ChosenPawn, /*bSwapPlaces=*/ true, /*BlendTime=*/ 0.0f);
		}
	}

	RefreshParty();
	SnapFollowersToLeader();
}

void UJRPGFieldPartyComponent::HandlePartyChanged()
{
	// O mapa ainda não foi arrumado: começou sem roster (PIE direto, sem New Game) e alguém
	// acabou de ser recrutado
	const AJRPGFieldCharacter* Leader = GetLeader();
	if (Leader && !bSwitching && (Leader->PartyKey.IsNone() || !PartyPawns.Contains(Leader->PartyKey)))
	{
		SetupForPossessedPawn();
		return;
	}
	// Dispara também para HP, level, equipamento... RefreshParty só mexe se a formação mudou
	RefreshParty();
}

void UJRPGFieldPartyComponent::RefreshParty()
{
	AJRPGFieldCharacter* Leader = GetLeader();
	UPartySubsystem* Party = GetParty();
	if (bSwitching || !Leader || !Party || Leader->PartyKey.IsNone() || !PartyPawns.Contains(Leader->PartyKey))
	{
		return; // o mapa ainda não foi arrumado (SetupForPossessedPawn)
	}

	// O líder saiu da formação (menu, história): o controle vai para o primeiro que continua
	// nela, no lugar dele
	const TArray<FName> Formation = Party->GetActiveParty();
	if (Formation.Num() > 0 && !Formation.Contains(Leader->PartyKey))
	{
		for (const FName& Key : Formation)
		{
			AJRPGFieldCharacter* Next = FindOrSpawnMember(Key);
			if (Next && DoSwitchLeader(Next, /*bSwapPlaces=*/ true, /*BlendTime=*/ 0.0f))
			{
				Leader = Next;
				break;
			}
		}
	}

	const TArray<FName> Wanted = GetWantedFollowers(Leader->PartyKey);
	bool bChanged = false;

	// Tira quem saiu
	for (auto It = PartyPawns.CreateIterator(); It; ++It)
	{
		AJRPGFieldCharacter* Member = It.Value().Get();
		if (!Member)
		{
			It.RemoveCurrent();
			bChanged = true;
		}
		else if (Member != Leader && !Wanted.Contains(It.Key()))
		{
			DestroyMember(Member);
			It.RemoveCurrent();
			bChanged = true;
		}
	}

	// Põe quem chegou
	TArray<AJRPGFieldCharacter*> Arrived;
	for (const FName& Key : Wanted)
	{
		if (!PartyPawns.Contains(Key))
		{
			if (AJRPGFieldCharacter* Member = SpawnMember(Key))
			{
				Arrived.Add(Member);
			}
		}
	}

	if (!bChanged && Arrived.Num() == 0 && Followers.Num() == Wanted.Num())
	{
		return;
	}
	RebuildFollowers();

	// Quem acabou de chegar nasceu em cima do líder: vai para o lugar dele na fila
	for (int32 Index = 0; Index < Followers.Num(); ++Index)
	{
		AJRPGFieldCharacter* Member = Followers[Index].Pawn.Get();
		if (Member && Arrived.Contains(Member))
		{
			TeleportMember(Member, GetSnapSpot(Index, Leader, Member), Leader->GetActorRotation());
		}
	}
}

TArray<FName> UJRPGFieldPartyComponent::GetWantedFollowers(FName LeaderKey) const
{
	TArray<FName> Out;
	const UPartySubsystem* Party = GetParty();
	if (!Party || !bPartyAllowedInMap)
	{
		return Out;
	}
	for (const FName& Key : Party->GetActiveParty())
	{
		const TSubclassOf<AJRPGFieldCharacter>* Class = MemberClasses.Find(Key);
		if (Key != LeaderKey && Class && *Class)
		{
			Out.Add(Key);
		}
	}
	return Out;
}

AJRPGFieldCharacter* UJRPGFieldPartyComponent::FindOrSpawnMember(FName Character)
{
	if (const TWeakObjectPtr<AJRPGFieldCharacter>* Found = PartyPawns.Find(Character))
	{
		if (AJRPGFieldCharacter* Member = Found->Get())
		{
			return Member;
		}
	}
	return SpawnMember(Character);
}

AJRPGFieldCharacter* UJRPGFieldPartyComponent::SpawnMember(FName Character)
{
	UWorld* World = GetWorld();
	const AJRPGFieldCharacter* Leader = GetLeader();
	const TSubclassOf<AJRPGFieldCharacter>* Class = MemberClasses.Find(Character);
	if (!World || !Leader)
	{
		return nullptr;
	}
	if (!Class || !*Class)
	{
		UE_LOG(LogTemp, Warning, TEXT("JRPGFieldParty: %s está na formação mas não tem Blueprint em MemberClasses — não aparece no mapa."),
			*Character.ToString());
		return nullptr;
	}

	const AJRPGFieldCharacter* Defaults = Class->GetDefaultObject();
	const FTransform Transform(FRotator(0.0f, Leader->GetActorRotation().Yaw, 0.0f),
		ToMemberHeight(Leader->GetActorLocation(), Leader, Defaults));

	AJRPGFieldCharacter* Member = World->SpawnActorDeferred<AJRPGFieldCharacter>(*Class, Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Member)
	{
		return nullptr;
	}
	Member->PartyKey = Character;
	Member->bDeclareFieldOnBeginPlay = false;     // quem declara o Field é o líder
	Member->AutoPossessAI = EAutoPossessAI::Disabled; // o controller é o nosso, logo abaixo
	// Antes de terminar de nascer: entra no mapa sem overlap, sem disparar trigger nenhum
	Member->SetPartyFollower(true);
	Member->FinishSpawning(Transform);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AJRPGPartyFollowerController* Controller = World->SpawnActor<AJRPGPartyFollowerController>(Params))
	{
		Controller->Possess(Member);
	}
	Member->SetMoveSpeedScale(FollowerSpeedScale);

	PartyPawns.Add(Character, Member);
	return Member;
}

void UJRPGFieldPartyComponent::DestroyMember(AJRPGFieldCharacter* Member)
{
	if (!Member)
	{
		return;
	}
	if (AJRPGPartyFollowerController* Controller = Cast<AJRPGPartyFollowerController>(Member->GetController()))
	{
		Controller->UnPossess();
		Controller->Destroy();
	}
	Member->Destroy();
}

void UJRPGFieldPartyComponent::RebuildFollowers()
{
	const AJRPGFieldCharacter* Leader = GetLeader();
	TArray<FFollower> Previous = MoveTemp(Followers);
	Followers.Reset();
	bGatherSlotsValid = false;
	if (!Leader)
	{
		return;
	}

	for (const FName& Key : GetWantedFollowers(Leader->PartyKey))
	{
		const TWeakObjectPtr<AJRPGFieldCharacter>* Found = PartyPawns.Find(Key);
		AJRPGFieldCharacter* Member = Found ? Found->Get() : nullptr;
		if (!Member || Member == Leader)
		{
			continue;
		}
		const FFollower* Kept = Previous.FindByPredicate(
			[Member](const FFollower& F) { return F.Pawn.Get() == Member; });
		FFollower& Follower = Followers.Add_GetRef(Kept ? *Kept : FFollower());
		Follower.Pawn = Member;
		if (!Kept)
		{
			// Novo na fila: já começa no lugar dele
			const float Oldest = Trail.Num() > 0 ? Trail[0].Distance : 0.0f;
			Follower.Cursor = FMath::Max(Oldest, GetTrailHead() - FollowSpacing * Followers.Num());
		}
	}
}

// ============================================================
// TROCA DE PERSONAGEM
// ============================================================

bool UJRPGFieldPartyComponent::CycleLeader()
{
	const AJRPGFieldCharacter* Leader = GetLeader();
	const UPartySubsystem* Party = GetParty();
	if (!Leader || !Party || bSwitching)
	{
		return false;
	}

	// O próximo da formação que está no mapa e perto, dando a volta na lista
	const TArray<FName> Formation = Party->GetActiveParty();
	const int32 Start = FMath::Max(Formation.Find(Leader->PartyKey), 0);
	for (int32 Step = 1; Step <= Formation.Num(); ++Step)
	{
		AJRPGFieldCharacter* Next = GetPartyPawn(Formation[(Start + Step) % Formation.Num()]);
		if (Next && Next != Leader
			&& FVector::Dist(Next->GetActorLocation(), Leader->GetActorLocation()) <= MaxSwitchDistance)
		{
			return DoSwitchLeader(Next, /*bSwapPlaces=*/ false, SwitchBlendTime);
		}
	}
	return false;
}

bool UJRPGFieldPartyComponent::SwitchLeader(FName Character)
{
	AJRPGFieldCharacter* Next = GetPartyPawn(Character);
	return Next && Next != GetLeader() && DoSwitchLeader(Next, /*bSwapPlaces=*/ false, SwitchBlendTime);
}

bool UJRPGFieldPartyComponent::DoSwitchLeader(AJRPGFieldCharacter* NewLeader, bool bSwapPlaces, float BlendTime)
{
	AJRPGFieldPlayerController* PC = GetFieldController();
	AJRPGFieldCharacter* OldLeader = GetLeader();
	UWorld* World = GetWorld();
	if (!PC || !World || !OldLeader || !NewLeader || NewLeader == OldLeader || bSwitching)
	{
		return false;
	}
	TGuardValue<bool> SwitchingGuard(bSwitching, true);

	AController* FollowerController = NewLeader->GetController();
	OldLeader->SetSprinting(false);
	NewLeader->SetSprinting(false);

	// 1) O antigo vira seguidor ainda possuído: triggers e zonas de câmera recebem a saída dele
	//    como a do jogador
	OldLeader->SetPartyFollower(true);

	// 2) Trocando de lugar: o novo fica exatamente onde o antigo estava
	if (bSwapPlaces)
	{
		const FVector OldLocation = OldLeader->GetActorLocation();
		const FRotator OldRotation = OldLeader->GetActorRotation();
		const FVector NewLocation = NewLeader->GetActorLocation();
		const FRotator NewRotation = NewLeader->GetActorRotation();
		TeleportMember(NewLeader, ToMemberHeight(OldLocation, OldLeader, NewLeader), OldRotation);
		TeleportMember(OldLeader, ToMemberHeight(NewLocation, NewLeader, OldLeader), NewRotation);
	}

	// 3) O controle passa, e a câmera desliza do antigo para o novo
	PC->PossessKeepingView(NewLeader, BlendTime);
	if (!FollowerController)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FollowerController = World->SpawnActor<AJRPGPartyFollowerController>(Params);
	}
	if (FollowerController)
	{
		FollowerController->Possess(OldLeader);
	}

	// 4) Já é o pawn do jogador: as zonas de câmera e os triggers onde ele está entram agora.
	//    Depois da view — senão o blend acima passaria por cima de uma zona Level Camera
	NewLeader->SetPartyFollower(false);
	NewLeader->SetMoveSpeedScale(1.0f);
	OldLeader->SetMoveSpeedScale(FollowerSpeedScale);

	if (UPartySubsystem* Party = GetParty())
	{
		Party->SetFieldLeader(NewLeader->PartyKey);
	}

	// A fila recomeça do novo líder; com ele parado, os outros se juntam ao lado
	ResetTrail(NewLeader->GetActorLocation());
	RebuildFollowers();
	for (FFollower& Follower : Followers)
	{
		Follower.Cursor = 0.0f;
		Follower.bHasSlot = false;
		Follower.bShortcut = false;
		StopLeashMove(Follower);
	}
	LeaderIdleSeconds = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("JRPGFieldParty: o jogador agora controla %s (%s virou seguidor)."),
		*NewLeader->PartyKey.ToString(), *OldLeader->PartyKey.ToString());
	OnLeaderChanged.Broadcast(NewLeader, OldLeader);
	return true;
}

// ============================================================
// CONSULTAS / CUTSCENE
// ============================================================

AJRPGFieldPlayerController* UJRPGFieldPartyComponent::GetFieldController() const
{
	return Cast<AJRPGFieldPlayerController>(GetOwner());
}

UPartySubsystem* UJRPGFieldPartyComponent::GetParty() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
}

AJRPGFieldCharacter* UJRPGFieldPartyComponent::GetLeader() const
{
	const AJRPGFieldPlayerController* PC = GetFieldController();
	return PC ? Cast<AJRPGFieldCharacter>(PC->GetPawn()) : nullptr;
}

AJRPGFieldCharacter* UJRPGFieldPartyComponent::GetPartyPawn(FName Character) const
{
	const TWeakObjectPtr<AJRPGFieldCharacter>* Found = PartyPawns.Find(Character);
	return Found ? Found->Get() : nullptr;
}

TArray<AJRPGFieldCharacter*> UJRPGFieldPartyComponent::GetFollowers() const
{
	TArray<AJRPGFieldCharacter*> Out;
	for (const FFollower& Follower : Followers)
	{
		if (AJRPGFieldCharacter* Member = Follower.Pawn.Get())
		{
			Out.Add(Member);
		}
	}
	return Out;
}

void UJRPGFieldPartyComponent::SetFollowEnabled(bool bEnabled)
{
	bFollowEnabled = bEnabled;
	for (FFollower& Follower : Followers)
	{
		StopLeashMove(Follower); // desligando: nenhum MoveTo brigando com o Sequencer
	}
}

void UJRPGFieldPartyComponent::SnapFollowersToLeader()
{
	const AJRPGFieldCharacter* Leader = GetLeader();
	if (!Leader)
	{
		return;
	}
	ResetTrail(Leader->GetActorLocation());
	for (int32 Index = 0; Index < Followers.Num(); ++Index)
	{
		FFollower& Follower = Followers[Index];
		Follower.Cursor = 0.0f;
		Follower.bHasSlot = false;
		Follower.bShortcut = false;
		StopLeashMove(Follower);
		if (AJRPGFieldCharacter* Member = Follower.Pawn.Get())
		{
			TeleportMember(Member, GetSnapSpot(Index, Leader, Member), Leader->GetActorRotation());
		}
	}
	// Já estão no lugar de juntar: os lugares calculados no próximo tick são estes mesmos
	bGatherSlotsValid = false;
	LeaderIdleSeconds = GatherDelay;
}

// ============================================================
// TRILHA
// ============================================================

void UJRPGFieldPartyComponent::ResetTrail(const FVector& LeaderLocation)
{
	// Dois pontos: o primeiro fixo, o último é sempre o líder agora
	Trail.Reset();
	Trail.Add({ LeaderLocation, 0.0f });
	Trail.Add({ LeaderLocation, 0.0f });
}

void UJRPGFieldPartyComponent::RecordTrail(const AJRPGFieldCharacter* Leader)
{
	const FVector Location = Leader->GetActorLocation();
	if (Trail.Num() < 2)
	{
		ResetTrail(Location);
		return;
	}
	if (FVector::Dist(Location, Trail.Last().Location) > LeaderJumpDistance)
	{
		SnapFollowersToLeader(); // portal, load, cutscene: a fila recomeça ao lado dele
		return;
	}

	const FTrailPoint Fixed = Trail[Trail.Num() - 2];
	FTrailPoint& Head = Trail.Last();
	Head.Location = Location;
	Head.Distance = Fixed.Distance + FVector::Dist(Fixed.Location, Location);
	if (Head.Distance - Fixed.Distance >= TrailSampleSpacing)
	{
		const FTrailPoint NewHead = Head; // fixa este ponto; o novo último segue o líder
		Trail.Add(NewHead);
	}

	// Esquece o que nenhum seguidor vai mais pisar
	const float KeepFrom = GetTrailHead() - (FollowSpacing * (Followers.Num() + 1) + TrailExtraLength);
	int32 Expired = 0;
	while (Expired < Trail.Num() - 2 && Trail[Expired + 1].Distance < KeepFrom)
	{
		++Expired;
	}
	if (Expired > 0)
	{
		Trail.RemoveAt(0, Expired, EAllowShrinking::No);
	}
}

FVector UJRPGFieldPartyComponent::SampleTrail(float Distance) const
{
	if (Trail.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	const float Clamped = FMath::Clamp(Distance, Trail[0].Distance, GetTrailHead());
	// De trás para a frente: quem pergunta quase sempre quer um ponto perto do líder
	for (int32 Index = Trail.Num() - 1; Index > 0; --Index)
	{
		const FTrailPoint& A = Trail[Index - 1];
		if (A.Distance <= Clamped)
		{
			const FTrailPoint& B = Trail[Index];
			const float Length = B.Distance - A.Distance;
			const float Alpha = Length > KINDA_SMALL_NUMBER ? (Clamped - A.Distance) / Length : 1.0f;
			return FMath::Lerp(A.Location, B.Location, FMath::Clamp(Alpha, 0.0f, 1.0f));
		}
	}
	return Trail[0].Location;
}

float UJRPGFieldPartyComponent::ProjectOntoTrail(const FVector& Location, float Cursor, float Desired) const
{
	const float From = Cursor - TrailBacktrack;
	const float To = FMath::Max(Cursor, Desired);
	const float ShortcutFrom = Cursor + TrailLookahead * 2.0f;

	float Best = FMath::Clamp(Cursor, Trail.Num() > 0 ? Trail[0].Distance : 0.0f, GetTrailHead());
	float BestDistSq = TNumericLimits<float>::Max();
	for (int32 Index = 1; Index < Trail.Num(); ++Index)
	{
		const FTrailPoint& A = Trail[Index - 1];
		const FTrailPoint& B = Trail[Index];
		if (B.Distance < From || A.Distance > To)
		{
			continue;
		}
		const FVector Closest = FMath::ClosestPointOnSegment(Location, A.Location, B.Location);
		if (FMath::Abs(Closest.Z - Location.Z) > TrailMaxHeightGap)
		{
			continue; // outro andar
		}
		const float DistSq = FVector::DistSquared2D(Location, Closest);
		const float Along = FMath::Clamp(A.Distance + FVector::Dist(A.Location, Closest), From, To);
		if (Along > ShortcutFrom && DistSq > FMath::Square(TrailShortcutRadius))
		{
			continue;
		}
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Along;
		}
	}
	return Best;
}

// ============================================================
// SEGUIDORES
// ============================================================

void UJRPGFieldPartyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AJRPGFieldCharacter* Leader = GetLeader();
	if (!Leader || bSwitching || !PartyPawns.Contains(Leader->PartyKey))
	{
		return;
	}

	// A trilha anda mesmo sem seguidores: quem entrar na party depois nasce no lugar certo
	RecordTrail(Leader);
	if (Followers.Num() == 0)
	{
		return;
	}

	const float LeaderSpeed = Leader->GetGroundSpeed();
	LeaderIdleSeconds = LeaderSpeed < IdleSpeed ? LeaderIdleSeconds + DeltaTime : 0.0f;
	const bool bGathering = LeaderIdleSeconds >= GatherDelay;
	const bool bLeash = FollowMode == EJRPGPartyFollowMode::Leash && HasNavMesh();
	if (!bGathering)
	{
		bGatherSlotsValid = false;
	}
	else if (!bGatherSlotsValid)
	{
		// Uma vez por parada: recalcular todo tick faria os lugares tremerem. No Leash o MoveTo
		// contorna parede, então o lugar não precisa estar em linha reta do seguidor
		ComputeGatherSlots(Leader, /*bCheckFromMember=*/ !bLeash);
		bGatherSlotsValid = true;
	}

	for (int32 Index = 0; Index < Followers.Num(); ++Index)
	{
		UpdateFollower(Followers[Index], Index, Leader, LeaderSpeed, bGathering, bLeash, DeltaTime);
	}
}

void UJRPGFieldPartyComponent::UpdateFollower(FFollower& Follower, int32 Index, const AJRPGFieldCharacter* Leader,
	float LeaderSpeed, bool bGathering, bool bLeash, float DeltaTime)
{
	AJRPGFieldCharacter* Member = Follower.Pawn.Get();
	if (!Member)
	{
		return;
	}
	if (!bFollowEnabled)
	{
		Follower.StuckSeconds = 0.0f;
		return;
	}

	const float Oldest = Trail[0].Distance;
	const float Desired = FMath::Max(Oldest, GetTrailHead() - FollowSpacing * (Index + 1));
	const FVector Location = Member->GetActorLocation();

	// Ficou para trás demais (caiu, empacou fora da tela): volta para o lugar dele
	if (FVector::Dist2D(Location, Leader->GetActorLocation()) > TeleportDistance)
	{
		StopLeashMove(Follower);
		TeleportMember(Member, GetSnapSpot(Index, Leader, Member), Leader->GetActorRotation());
		Follower.Cursor = Desired;
		Follower.bHasSlot = false;
		Follower.bShortcut = false;
		return;
	}

	if (bLeash)
	{
		// O cursor acompanha o lugar dele na fila: se o modo cair para Trail, ele começa dali
		Follower.Cursor = Desired;
		Follower.bShortcut = false;
		UpdateLeashFollower(Follower, Index, Leader, LeaderSpeed, bGathering, DeltaTime);
		return;
	}
	if (Follower.bLeashMoving)
	{
		StopLeashMove(Follower); // voltou para o Trail no meio de um MoveTo
	}

	// Com o líder parado vai para o lugar ao lado dele: esse lugar manda, nada de atalho
	const bool bToSlot = bGathering && Follower.bHasSlot;
	if (bToSlot)
	{
		Follower.bShortcut = false;
	}

	// Pela trilha: acompanha até onde chegou e, de tempos em tempos, procura um atalho. Durante
	// o atalho não projeta — a projeção puxaria o cursor de volta para a volta que ele está pulando
	if (!bToSlot && !Follower.bShortcut)
	{
		Follower.Cursor = ProjectOntoTrail(Location, Follower.Cursor, Desired);
		Follower.ShortcutCooldown -= DeltaTime;
		if (bTrailShortcuts && Follower.ShortcutCooldown <= 0.0f)
		{
			Follower.ShortcutCooldown = ShortcutInterval;
			TryTrailShortcut(Follower, Leader, Member, Desired);
		}
	}

	FVector Goal;
	float Remaining;
	if (bToSlot)
	{
		Goal = Follower.Slot;
		Remaining = FVector::Dist2D(Location, Goal);
	}
	else if (Follower.bShortcut && FVector::Dist2D(Location, SampleTrail(Follower.ShortcutTo)) > ShortcutArriveDistance)
	{
		// Reto até o ponto do atalho; o que falta conta a reta mais o resto da trilha depois dele
		Goal = SampleTrail(Follower.ShortcutTo);
		Remaining = FVector::Dist2D(Location, Goal) + FMath::Max(0.0f, Desired - Follower.ShortcutTo);
	}
	else
	{
		if (Follower.bShortcut)
		{
			// Chegou no ponto do atalho: dali em diante, a trilha de novo
			Follower.bShortcut = false;
			Follower.Cursor = FMath::Max(Follower.Cursor, Follower.ShortcutTo);
		}
		Goal = SampleTrail(FMath::Min(Follower.Cursor + TrailLookahead, Desired));
		Remaining = Desired - Follower.Cursor;
	}

	const FVector Direction = (Goal - Location).GetSafeNormal2D();
	if (Remaining <= ArriveDistance || Direction.IsNearlyZero())
	{
		Member->SetSprinting(false);
		Follower.StuckSeconds = 0.0f;
		return;
	}

	Member->SetSprinting(!bToSlot && (Leader->IsSprinting() || Remaining > CatchUpDistance));

	// A velocidade do líder mais a correção pelo que falta: andando junto, o seguidor não fica
	// sempre um pouco atrás; chegando, desacelera
	const float MaxSpeed = FMath::Max(Member->GetCharacterMovement()->GetMaxSpeed(), 1.0f);
	const float FeedForward = bToSlot ? 0.0f : LeaderSpeed / MaxSpeed;
	Member->AddMovementInput(Direction, FMath::Clamp(FeedForward + Remaining / SlowdownDistance, 0.25f, 1.0f));

	// Preso: tentando andar e não sai do lugar (porta fechou, um NPC que bloqueia tudo)
	Follower.StuckSeconds = Member->GetGroundSpeed() < StuckSpeed ? Follower.StuckSeconds + DeltaTime : 0.0f;
	if (Follower.StuckSeconds >= StuckTime)
	{
		Follower.StuckSeconds = 0.0f;
		if (bToSlot)
		{
			Follower.bHasSlot = false; // o lugar ao lado não dá: fica onde está
			return;
		}
		TeleportMember(Member, ToMemberHeight(SampleTrail(Desired), Leader, Member), Member->GetActorRotation());
		Follower.Cursor = Desired;
		Follower.bShortcut = false;
	}
}

void UJRPGFieldPartyComponent::UpdateLeashFollower(FFollower& Follower, int32 Index, const AJRPGFieldCharacter* Leader,
	float LeaderSpeed, bool bGathering, float DeltaTime)
{
	AJRPGFieldCharacter* Member = Follower.Pawn.Get();
	AAIController* AI = Member ? Cast<AAIController>(Member->GetController()) : nullptr;
	if (!AI)
	{
		return;
	}

	const FVector Location = Member->GetActorLocation();
	const FVector LeaderLocation = Leader->GetActorLocation();
	const float ToLeader = FVector::Dist2D(Location, LeaderLocation);
	// Líder parado: o lugar ao lado, já validado. Andando: o mesmo ponto em volta dele, sem
	// validar — o MoveTo acha o caminho até lá
	const bool bToSlot = bGathering && Follower.bHasSlot;
	const FVector Goal = bToSlot ? Follower.Slot : GetGatherPoint(Index, Leader);
	const float ToGoal = FVector::Dist2D(Location, Goal);

	// Parado: só sai quando o líder se afasta, ou (líder parado) se o lugar ao lado está longe ou
	// tem alguém em cima dele. Sai com um atraso sorteado, para não saírem todos juntos
	if (!Follower.bLeashMoving)
	{
		const bool bWantsToMove = ToLeader > LeashStartDistance
			|| (bToSlot && (ToGoal > LeashSlotTolerance || IsCrowded(Member)));
		if (!bWantsToMove)
		{
			Follower.ReactionTimer = -1.0f;
			return;
		}
		if (Follower.ReactionTimer < 0.0f)
		{
			Follower.ReactionTimer = FMath::FRandRange(FMath::Min(LeashReactionMin, LeashReactionMax),
				FMath::Max(LeashReactionMin, LeashReactionMax));
		}
		Follower.ReactionTimer -= DeltaTime;
		if (Follower.ReactionTimer > 0.0f)
		{
			return;
		}
		Follower.bLeashMoving = true;
		Follower.ReactionTimer = -1.0f;
		Follower.RepathTimer = 0.0f;
		Follower.StuckSeconds = 0.0f;
	}

	// Chegou: com o líder parado, no lugar ao lado (sem lugar válido, perto o bastante do líder).
	// Com ele andando não para — acompanha (velocidade abaixo)
	if (bGathering && (bToSlot ? ToGoal <= LeashArriveRadius : ToLeader <= LeashStopDistance))
	{
		StopLeashMove(Follower);
		return;
	}

	// Corre com o líder correndo ou longe dele; volta a andar perto da metade (sem pisca-pisca)
	Member->SetSprinting(Leader->IsSprinting() || ToLeader > LeashRunDistance
		|| (Member->IsSprinting() && ToLeader > LeashRunDistance * 0.5f));

	// Andando junto: a velocidade do líder, mais (ou menos) pelo quanto está fora da distância de
	// acompanhar. Indo para o lugar ao lado: o máximo, e o path following freia na chegada
	const float BaseSpeed = FMath::Max(Member->IsSprinting() ? Member->SprintSpeed : Member->WalkSpeed, 1.0f);
	float WantedSpeed = BaseSpeed * FollowerSpeedScale;
	if (!bGathering)
	{
		WantedSpeed = FMath::Clamp(LeaderSpeed + (ToGoal - LeashTrailGap) * LeashSpeedGain, 0.0f, WantedSpeed);
	}
	Member->SetMoveSpeedScale(WantedSpeed / BaseSpeed);

	// O MoveTo: refeito quando o destino andou o bastante, ou logo depois de ele terminar (chegou
	// no fim de um caminho velho, com o líder ainda andando)
	Follower.RepathTimer -= DeltaTime;
	const bool bMoveIdle = AI->GetMoveStatus() == EPathFollowingStatus::Idle;
	const bool bGoalMoved = FVector::DistSquared2D(Goal, Follower.MoveGoal) > FMath::Square(LeashRepathDistance);
	if ((bMoveIdle && Follower.RepathTimer <= LeashRepathInterval - LeashIdleRepathDelay)
		|| (bGoalMoved && Follower.RepathTimer <= 0.0f))
	{
		Follower.RepathTimer = LeashRepathInterval;
		Follower.MoveGoal = Goal;
		const float Acceptance = LeashArriveRadius * 0.5f;
		if (AI->MoveToLocation(Goal, Acceptance, /*bStopOnOverlap=*/ false, /*bUsePathfinding=*/ true,
				/*bProjectDestinationToNavigation=*/ true, /*bCanStrafe=*/ false) == EPathFollowingRequestResult::Failed)
		{
			// O ponto em volta dele caiu fora do navmesh (parede, beirada): vai até o próprio líder
			AI->MoveToLocation(LeaderLocation, LeashStopDistance * 0.5f, /*bStopOnOverlap=*/ false,
				/*bUsePathfinding=*/ true, /*bProjectDestinationToNavigation=*/ true, /*bCanStrafe=*/ false);
		}
	}

	// Preso: querendo andar e não sai do lugar (sem caminho, porta fechou, NPC no meio)
	const bool bTrying = WantedSpeed > StuckSpeed * 2.0f && ToGoal > LeashArriveRadius;
	Follower.StuckSeconds = bTrying && Member->GetGroundSpeed() < StuckSpeed ? Follower.StuckSeconds + DeltaTime : 0.0f;
	if (Follower.StuckSeconds >= StuckTime)
	{
		const bool bNearLeader = ToLeader <= LeashStartDistance;
		if (bGathering && bNearLeader)
		{
			Follower.bHasSlot = false; // o lugar ao lado não dá: fica onde está
			StopLeashMove(Follower);
			return;
		}
		StopLeashMove(Follower);
		TeleportMember(Member, GetSnapSpot(Index, Leader, Member), Member->GetActorRotation());
	}
}

void UJRPGFieldPartyComponent::StopLeashMove(FFollower& Follower) const
{
	Follower.bLeashMoving = false;
	Follower.ReactionTimer = -1.0f;
	Follower.RepathTimer = 0.0f;
	Follower.StuckSeconds = 0.0f;
	if (AJRPGFieldCharacter* Member = Follower.Pawn.Get())
	{
		if (AAIController* AI = Cast<AAIController>(Member->GetController()))
		{
			AI->StopMovement();
		}
		Member->SetSprinting(false);
		Member->SetMoveSpeedScale(FollowerSpeedScale);
	}
}

bool UJRPGFieldPartyComponent::IsCrowded(const AJRPGFieldCharacter* Member) const
{
	const FVector Location = Member->GetActorLocation();
	for (const TPair<FName, TWeakObjectPtr<AJRPGFieldCharacter>>& Pair : PartyPawns)
	{
		const AJRPGFieldCharacter* Other = Pair.Value.Get();
		if (Other && Other != Member
			&& FVector::DistSquared2D(Location, Other->GetActorLocation()) < FMath::Square(LeashPersonalSpace))
		{
			return true;
		}
	}
	return false;
}

bool UJRPGFieldPartyComponent::HasNavMesh() const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	return Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
}

void UJRPGFieldPartyComponent::TryTrailShortcut(FFollower& Follower, const AJRPGFieldCharacter* Leader,
	const AJRPGFieldCharacter* Member, float Desired) const
{
	// Do lugar dele na trilha para trás: o primeiro ponto que vale a pena e que ele alcança em
	// linha reta é o mais adiantado possível
	const FVector Location = Member->GetActorLocation();
	int32 Checks = 0;
	for (float Along = Desired; Along > Follower.Cursor + ShortcutMinSaving && Checks < ShortcutMaxChecks;
		Along -= ShortcutSampleStep)
	{
		const FVector Point = ToMemberHeight(SampleTrail(Along), Leader, Member);
		const float Straight = FVector::Dist(Location, Point);
		if (Straight > ShortcutMaxDistance || (Along - Follower.Cursor) - Straight < ShortcutMinSaving)
		{
			continue; // longe demais, ou economiza pouco: não gasta checagem
		}
		++Checks;
		if (IsStraightPathClear(Member, Location, Point))
		{
			Follower.bShortcut = true;
			Follower.ShortcutTo = Along;
			Follower.StuckSeconds = 0.0f;
			return;
		}
	}
}

bool UJRPGFieldPartyComponent::IsStraightPathClear(const AJRPGFieldCharacter* Member, const FVector& From, const FVector& To) const
{
	UWorld* World = GetWorld();
	if (!World || !Member)
	{
		return false;
	}

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		// Borda do navmesh no meio = parede ou buraco. Fora do navmesh também não vale
		FNavLocation FromNav;
		FNavLocation ToNav;
		if (!Nav->ProjectPointToNavigation(From, FromNav, NavProjectExtent)
			|| !Nav->ProjectPointToNavigation(To, ToNav, NavProjectExtent))
		{
			return false;
		}
		FVector Hit;
		return !UNavigationSystemV1::NavigationRaycast(World, FromNav.Location, ToNav.Location, Hit);
	}

	// Sem navmesh: nada no caminho na altura do corpo, e chão ao longo dele (um buraco no meio
	// deixaria a esfera passar)
	const FCollisionQueryParams Params = MakePartyQueryParams();
	const UCapsuleComponent* Capsule = Member->GetCapsuleComponent();
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Capsule->GetScaledCapsuleRadius() * 0.8f);
	if (World->SweepTestByChannel(From, To, FQuat::Identity, ECC_Pawn, Sphere, Params))
	{
		return false;
	}
	const FVector Down(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() + 100.0f);
	for (const float Alpha : { 0.25f, 0.5f, 0.75f, 1.0f })
	{
		const FVector Point = FMath::Lerp(From, To, Alpha);
		if (!World->LineTraceTestByChannel(Point, Point - Down, ECC_Pawn, Params))
		{
			return false;
		}
	}
	return true;
}

FCollisionQueryParams UJRPGFieldPartyComponent::MakePartyQueryParams() const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGFieldParty), false);
	for (const TPair<FName, TWeakObjectPtr<AJRPGFieldCharacter>>& Pair : PartyPawns)
	{
		if (const AJRPGFieldCharacter* PartyPawn = Pair.Value.Get())
		{
			Params.AddIgnoredActor(PartyPawn);
		}
	}
	return Params;
}

FVector UJRPGFieldPartyComponent::GetGatherPoint(int32 Index, const AJRPGFieldCharacter* Leader) const
{
	const int32 NumAngles = GatherAngles.Num();
	const float Angle = NumAngles > 0 ? GatherAngles[Index % NumAngles] : 180.0f;
	// Passou do fim da lista: os mesmos ângulos numa roda mais afastada
	const float Ring = 1.0f + (NumAngles > 0 ? Index / NumAngles : Index);
	const FRotator Around(0.0f, Leader->GetActorRotation().Yaw + Angle, 0.0f);
	return Leader->GetActorLocation() + Around.Vector() * GatherDistance * Ring;
}

void UJRPGFieldPartyComponent::ComputeGatherSlots(const AJRPGFieldCharacter* Leader, bool bCheckFromMember)
{
	for (int32 Index = 0; Index < Followers.Num(); ++Index)
	{
		FFollower& Follower = Followers[Index];
		const AJRPGFieldCharacter* Member = Follower.Pawn.Get();
		Follower.bHasSlot = Member
			&& FindStandSpot(Leader, Member, GetGatherPoint(Index, Leader), bCheckFromMember, Follower.Slot);
	}
}

bool UJRPGFieldPartyComponent::FindStandSpot(const AJRPGFieldCharacter* Leader, const AJRPGFieldCharacter* Member,
	const FVector& Wanted, bool bCheckFromMember, FVector& OutSpot) const
{
	UWorld* World = GetWorld();
	if (!World || !Leader || !Member)
	{
		return false;
	}
	const FVector LeaderLocation = Leader->GetActorLocation();
	const FVector MemberLocation = Member->GetActorLocation();
	const UCapsuleComponent* Capsule = Member->GetCapsuleComponent();

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		// O mapa tem navmesh: o lugar precisa estar nele, sem borda do navmesh (parede, buraco)
		// entre ele e o líder, nem entre ele e o seguidor
		FNavLocation SpotNav;
		FNavLocation LeaderNav;
		if (!Nav->ProjectPointToNavigation(Wanted, SpotNav, NavProjectExtent)
			|| !Nav->ProjectPointToNavigation(LeaderLocation, LeaderNav, NavProjectExtent))
		{
			return false;
		}
		FVector Hit;
		if (UNavigationSystemV1::NavigationRaycast(World, LeaderNav.Location, SpotNav.Location, Hit))
		{
			return false;
		}
		if (bCheckFromMember)
		{
			FNavLocation MemberNav;
			if (!Nav->ProjectPointToNavigation(MemberLocation, MemberNav, NavProjectExtent)
				|| UNavigationSystemV1::NavigationRaycast(World, MemberNav.Location, SpotNav.Location, Hit))
			{
				return false;
			}
		}
		// O navmesh fica no chão: o centro da cápsula vai meia altura acima
		OutSpot = SpotNav.Location + FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight());
		return true;
	}

	// Sem navmesh: uma esfera do líder até o lugar (e do seguidor até lá) não pode bater em nada,
	// e precisa ter chão embaixo
	const FVector Spot = ToMemberHeight(FVector(Wanted.X, Wanted.Y, LeaderLocation.Z), Leader, Member);
	const FCollisionQueryParams Params = MakePartyQueryParams();
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Capsule->GetScaledCapsuleRadius() * 0.8f);
	if (World->SweepTestByChannel(LeaderLocation, Spot, FQuat::Identity, ECC_Pawn, Sphere, Params))
	{
		return false;
	}
	if (bCheckFromMember && World->SweepTestByChannel(MemberLocation, Spot, FQuat::Identity, ECC_Pawn, Sphere, Params))
	{
		return false;
	}
	const FVector Down(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() + 100.0f);
	if (!World->LineTraceTestByChannel(Spot, Spot - Down, ECC_Pawn, Params))
	{
		return false;
	}
	OutSpot = Spot;
	return true;
}

FVector UJRPGFieldPartyComponent::GetSnapSpot(int32 Index, const AJRPGFieldCharacter* Leader, const AJRPGFieldCharacter* Member) const
{
	// 1) O lugar dele na trilha: o líder passou por ali, então cabe
	const float Desired = GetTrailHead() - FollowSpacing * (Index + 1);
	if (Trail.Num() > 0 && Desired >= Trail[0].Distance)
	{
		return ToMemberHeight(SampleTrail(Desired), Leader, Member);
	}
	// 2) Ao lado do líder (trilha recém-zerada: começo do mapa, teleporte)
	FVector Spot;
	if (FindStandSpot(Leader, Member, GetGatherPoint(Index, Leader), /*bCheckFromMember=*/ false, Spot))
	{
		return Spot;
	}
	// 3) Em cima do próprio líder: o seguidor atravessa pawns, e na primeira andada a fila se forma
	return ToMemberHeight(Leader->GetActorLocation(), Leader, Member);
}

void UJRPGFieldPartyComponent::TeleportMember(AJRPGFieldCharacter* Member, const FVector& Location, const FRotator& Rotation) const
{
	Member->SetActorLocationAndRotation(Location, FRotator(0.0f, Rotation.Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = Member->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}
}

FVector UJRPGFieldPartyComponent::ToMemberHeight(const FVector& LeaderCenter, const AJRPGFieldCharacter* Leader,
	const AJRPGFieldCharacter* Member)
{
	if (!Leader || !Member)
	{
		return LeaderCenter;
	}
	// Mesmo chão, cápsulas de alturas diferentes (o Gala é mais alto que a Noa)
	const float LeaderHalf = Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float MemberHalf = Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return LeaderCenter + FVector(0.0f, 0.0f, MemberHalf - LeaderHalf);
}
