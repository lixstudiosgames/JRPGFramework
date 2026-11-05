#include "Battle/JRPGBattlePawn.h"
#include "Arts/JRPGArtSequence.h"
#include "Battle/JRPGBattleArena.h"
#include "Core/CoreSubsystem.h"
#include "Field/JRPGFieldEnemyAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	// Os papéis dos clips de reação e de pose (os nomes dos clips do herói; no monstro são as tags 0 / 1 / 2 / 3 / 4 / 0x0B)
	const FName RoleIdle(TEXT("Idle"));
	const FName RoleWalk(TEXT("Walk"));
	const FName RoleFlinch1(TEXT("Flinch1"));
	const FName RoleFlinch2(TEXT("Flinch2"));
	const FName RoleKnockdown(TEXT("Knockdown"));
	const FName RoleBlock(TEXT("Block"));
	const FName RoleDying(TEXT("Dying"));
	const FName RoleGetUp(TEXT("GetUp"));
	const FName RoleDown(TEXT("Down"));

	int32 TagForRole(FName ClipRole)
	{
		if (ClipRole == RoleIdle)      { return 0; }
		if (ClipRole == RoleWalk)      { return 1; }
		if (ClipRole == RoleFlinch1)   { return 2; }
		if (ClipRole == RoleFlinch2)   { return 3; }
		if (ClipRole == RoleKnockdown) { return 4; }
		if (ClipRole == RoleGetUp)     { return 5; }
		if (ClipRole == RoleBlock)     { return 0x0B; }
		return -1;
	}

	/**
	 * O limiar de alcance da lei FUN_8004E2F0, em unidades do PS1, para uma classe de tamanho: {256, 384, 1024}[classe]
	 * se menor que 3, senão classe << 4 (RE: docs/subsystems/battle-action-helpers.md, "battle range / reach metric").
	 */
	float ReachThresholdUnits(int32 SizeClass)
	{
		static constexpr float Small[3] = { 256.0f, 384.0f, 1024.0f };
		return SizeClass >= 0 && SizeClass < 3 ? Small[SizeClass] : static_cast<float>(SizeClass << 4);
	}

	/** O desvio de alcance de cada herói (DAT_80078878, ids de roster 1..4), somado à distância: positivo encurta o alcance. */
	float HeroReachOffsetUnits(FName Hero)
	{
		if (Hero == FName(TEXT("Vahn")))  { return 43.0f; }
		if (Hero == FName(TEXT("Gala")))  { return -53.0f; }
		if (Hero == FName(TEXT("Terra"))) { return -100.0f; }
		return 0.0f;   // a Noa
	}

	FColor EventColor(EJRPGBattleEventType Type)
	{
		switch (Type)
		{
		case EJRPGBattleEventType::Damage:        return FColor(255, 235, 200);
		case EJRPGBattleEventType::Miss:          return FColor(190, 190, 190);
		case EJRPGBattleEventType::Block:         return FColor(120, 220, 255);
		case EJRPGBattleEventType::Death:         return FColor(255, 140, 60);
		case EJRPGBattleEventType::APChanged:     return FColor(140, 255, 140);
		case EJRPGBattleEventType::ArtRecognized: return FColor(255, 230, 90);
		case EJRPGBattleEventType::ArtLearned:    return FColor(255, 170, 255);
		case EJRPGBattleEventType::ComboTotal:    return FColor(255, 215, 0);
		default:                                  return FColor::White;
		}
	}
}

AJRPGBattlePawn::AJRPGBattlePawn()
{
	PrimaryActorTick.bCanEverTick = false;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;

	// Só consulta: a câmera e a mira usam a cápsula; ninguém anda nem colide na luta
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(45.0f, 90.0f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Capsule->SetCanEverAffectNavigation(false);
	RootComponent = Capsule;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetRelativeRotation(FRotator(0.0f, MeshYawOffset, 0.0f));
}

void AJRPGBattlePawn::InitCombatant(const FJRPGCombatant& InCombatant, const FJRPGBattleVisual& Visual, const FTransform& FeetTransform)
{
	Combatant = InCombatant;

	USkeletalMesh* LoadedMesh = Visual.Mesh.LoadSynchronous();
	Mesh->SetSkeletalMeshAsset(LoadedMesh);
	Mesh->SetRelativeScale3D(FVector(Visual.Scale));
	Mesh->SetRelativeRotation(FRotator(0.0f, MeshYawOffset, 0.0f));

	// Cápsula do tamanho da malha, com os pés da malha no fundo dela
	float Half = Capsule->GetUnscaledCapsuleHalfHeight();
	if (LoadedMesh)
	{
		const FBoxSphereBounds Bounds = LoadedMesh->GetBounds();
		const float MinZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Visual.Scale;
		const float Height = Bounds.BoxExtent.Z * 2.0f * Visual.Scale;
		const float Radius = FMath::Clamp(FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * Visual.Scale * 0.6f, 20.0f, 150.0f);
		Half = FMath::Max(Height * 0.5f, Radius);
		Capsule->SetCapsuleSize(Radius, Half);
		Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -Half - MinZ));
	}
	SetActorLocationAndRotation(FeetTransform.GetLocation() + FVector(0.0f, 0.0f, Half), FeetTransform.GetRotation());

	BuildPieces(Visual);

	// Na luta: a apresentação do monstro lê as ações dele (os clips de golpe e de reação) e o conjunto de clips
	EnemyActions.Reset();
	EnemyAnims = FJRPGMonsterAnimSet();
	bDown = false;
	bActionActive = false;
	Phase = EJRPGActionPhase::Idle;
	if (InCombatant.Type == EJRPGCombatantType::Enemy)
	{
		if (const UDataTable* Actions = UCoreSubsystem::ResolveDataTable(TEXT("DT_EnemyActions")))
		{
			Actions->ForeachRow<FJRPGEnemyActionData>(TEXT("BattlePawn"), [&](const FName&, const FJRPGEnemyActionData& Row)
			{
				if (Row.Enemy == InCombatant.Key)
				{
					EnemyActions.Add(Row);
				}
			});
		}
		FJRPGEnemyData EnemyRow;
		if (UJRPGEncounterLibrary::GetEnemyData(InCombatant.Key, EnemyRow))
		{
			EnemyAnims = EnemyRow.Anims;
		}
	}

	IdleClip = Visual.IdleAnim.LoadSynchronous();
	bUsesAnimClass = Visual.AnimClass != nullptr;
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(bUsesAnimClass ? Visual.AnimClass.Get() : UJRPGFieldEnemyAnimInstance::StaticClass());
	PlayIdle();

	OnCombatantInitialized();
}

void AJRPGBattlePawn::BuildPieces(const FJRPGBattleVisual& Visual)
{
	for (USkeletalMeshComponent* Old : PieceComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	for (UStaticMeshComponent* Old : AttachmentComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	PieceComponents.Reset();
	AttachmentComponents.Reset();

	// Por partes, a Mesh só conduz a pose. Invisível ela pararia de atualizar os bones (por padrão
	// só atualiza quando aparece na tela), e as partes que seguem a pose dela sumiriam junto.
	const bool bByPieces = Visual.Pieces.Num() > 0;
	Mesh->SetVisibility(!bByPieces);
	Mesh->VisibilityBasedAnimTickOption = bByPieces
		? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		: EVisibilityBasedAnimTickOption::AlwaysTickPose;

	for (const FJRPGBattlePieceMesh& Part : Visual.Pieces)
	{
		USkeletalMesh* PartMesh = Part.Mesh.LoadSynchronous();
		if (!PartMesh)
		{
			continue;
		}
		const FString Label = TEXT("Piece_") + StaticEnum<EJRPGBattlePiece>()->GetNameStringByValue(static_cast<int64>(Part.Piece));
		USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(this, MakeUniqueObjectName(this, USkeletalMeshComponent::StaticClass(), FName(*Label)));
		Comp->SetupAttachment(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetSkeletalMeshAsset(PartMesh);
		Comp->RegisterComponent();
		Comp->SetLeaderPoseComponent(Mesh);
		PieceComponents.Add(Comp);
	}

	for (const FJRPGBattleAttachment& Attached : Visual.Attachments)
	{
		UStaticMesh* AttachedMesh = Attached.Mesh.LoadSynchronous();
		if (!AttachedMesh)
		{
			continue;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("Accessory")));
		Comp->SetupAttachment(Mesh, Attached.Bone);
		Comp->SetRelativeTransform(Attached.Offset);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetStaticMesh(AttachedMesh);
		Comp->RegisterComponent();
		AttachmentComponents.Add(Comp);
	}
}

void AJRPGBattlePawn::PlayClip(UAnimSequence* Clip, bool bLoop, float PlayRate)
{
	if (!Clip || bUsesAnimClass)
	{
		return;
	}
	// O crossfade nativo só toca em loop; clip de uma vez só vai direto, sem mistura
	if (bLoop)
	{
		UJRPGFieldEnemyAnimInstance* Anim = Cast<UJRPGFieldEnemyAnimInstance>(Mesh->GetAnimInstance());
		if (!Anim)
		{
			// Um clip de uma vez (PlayAnimation) passou a Mesh para o modo single node, e o idle em loop que vinha depois
			// (o fim de uma Art da Noa) ficava num quadro parado. Volta para a instância do crossfade, a mesma do começo da
			// luta, que repete o clip pela própria engine
			Mesh->SetAnimInstanceClass(UJRPGFieldEnemyAnimInstance::StaticClass());
			Anim = Cast<UJRPGFieldEnemyAnimInstance>(Mesh->GetAnimInstance());
		}
		if (Anim)
		{
			Anim->PlayClip(Clip, PlayRate, AnimBlendTime);
			return;
		}
	}
	Mesh->PlayAnimation(Clip, bLoop);
	Mesh->SetPlayRate(PlayRate);
}

void AJRPGBattlePawn::PlayIdle()
{
	PlayClip(IdleClip, true);
}

// =================================================================================================
// A luta: a apresentação em compasso com a linha do tempo lógica
// =================================================================================================

void AJRPGBattlePawn::SetPhase(EJRPGActionPhase NewPhase)
{
	if (Phase != NewPhase)
	{
		Phase = NewPhase;
		OnActionPhaseChanged(NewPhase);
	}
}

const FJRPGEnemyActionData* AJRPGBattlePawn::FindEnemyAction(int32 ActionIndex) const
{
	return EnemyActions.FindByPredicate([ActionIndex](const FJRPGEnemyActionData& Row) { return Row.ActionIndex == ActionIndex; });
}

const FJRPGEnemyActionData* AJRPGBattlePawn::FindEnemyActionByTag(int32 Tag) const
{
	return EnemyActions.FindByPredicate([Tag](const FJRPGEnemyActionData& Row) { return Row.Tag == Tag; });
}

UAnimSequence* AJRPGBattlePawn::FindRoleClip(FName ClipRole, float& OutPlayRate) const
{
	OutPlayRate = 1.0f;
	if (Combatant.Type == EJRPGCombatantType::Enemy)
	{
		// O clip da linha do DT_EnemyActions com a tag do papel, tocado no rate dele (a escala 8: 15 x rate quadros/s)
		const int32 Tag = TagForRole(ClipRole);
		if (const FJRPGEnemyActionData* Row = Tag >= 0 ? FindEnemyActionByTag(Tag) : nullptr)
		{
			if (UAnimSequence* Clip = Row->Anim.LoadSynchronous())
			{
				OutPlayRate = static_cast<float>(FMath::Max(Row->Rate, 1));
				return Clip;
			}
		}
		// Sem a tabela de ações: o conjunto de clips do DT_Enemies (rate 2, o de quase todo clip do original)
		const TSoftObjectPtr<UAnimSequence>* Soft = nullptr;
		if (ClipRole == RoleIdle)           { Soft = &EnemyAnims.Idle; }
		else if (ClipRole == RoleWalk)      { Soft = &EnemyAnims.Walk; }
		else if (ClipRole == RoleFlinch1)   { Soft = &EnemyAnims.Flinch1; }
		else if (ClipRole == RoleFlinch2)   { Soft = &EnemyAnims.Flinch2; }
		else if (ClipRole == RoleKnockdown) { Soft = &EnemyAnims.Knockdown; }
		else if (ClipRole == RoleGetUp)     { Soft = &EnemyAnims.GetUp; }
		else if (ClipRole == RoleBlock)     { Soft = &EnemyAnims.Block; }
		if (Soft && !Soft->IsNull())
		{
			OutPlayRate = 2.0f;
			return Soft->LoadSynchronous();
		}
		return nullptr;
	}

	// O herói: AS_<Nome>_Battle_<Papel>, com o RateScale já na velocidade do jogo (escala 8 = 2 x o clip)
	UAnimSequence* Clip = UJRPGArtSequenceLibrary::LoadHeroClip(Combatant.Key, ClipRole.ToString());
	if (Clip && Clip->RateScale > KINDA_SMALL_NUMBER)
	{
		OutPlayRate = 2.0f / Clip->RateScale;
	}
	return Clip;
}

void AJRPGBattlePawn::PlayReaction(FName ClipRole, bool bHold, FName FollowRole, bool bHoldFollow)
{
	float Rate = 1.0f;
	UAnimSequence* Clip = FindRoleClip(ClipRole, Rate);
	if (!Clip && ClipRole == RoleDying)
	{
		Clip = FindRoleClip(RoleKnockdown, Rate);   // o herói sem o clip de morrer cai pelo de derrubar
	}
	if (!Clip)
	{
		// Sem o clip deste papel a cadeia segue direto para o próximo (quem não tem o Knockdown ainda levanta)
		if (!FollowRole.IsNone())
		{
			PlayReaction(FollowRole, bHoldFollow);
		}
		else if (!bHold)
		{
			// Fim da cadeia sem clip (ex.: o GetUp que falta depois do Knockdown): volta ao idle, senão fica no último quadro
			ReturnToIdle();
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(ReactionTimer);
	PlayClip(Clip, false, Rate);
	PendingFollowRole = FollowRole;
	bPendingFollowHold = bHoldFollow;
	// Sem nada depois e para segurar: fica no último quadro. Senão o timer acorda no fim do clip (o seguinte da cadeia, ou o idle)
	if (!bHold || !FollowRole.IsNone())
	{
		const float Speed = FMath::Max(Rate * FMath::Max(Clip->RateScale, KINDA_SMALL_NUMBER), KINDA_SMALL_NUMBER);
		GetWorldTimerManager().SetTimer(ReactionTimer, this, &AJRPGBattlePawn::OnReactionEnded, FMath::Max(Clip->GetPlayLength() / Speed, 0.1f), false);
	}
}

void AJRPGBattlePawn::OnReactionEnded()
{
	if (bActionActive)
	{
		PendingFollowRole = NAME_None;   // ele já está na ação dele: a reação não toca por cima
		return;
	}
	if (!PendingFollowRole.IsNone())
	{
		const FName Next = PendingFollowRole;
		const bool bHoldNext = bPendingFollowHold;
		PendingFollowRole = NAME_None;
		PlayReaction(Next, bHoldNext);
		return;
	}
	ReturnToIdle();
}

void AJRPGBattlePawn::ReturnToIdle()
{
	if (!bDown && !bActionActive)
	{
		PlayIdle();
	}
}

void AJRPGBattlePawn::StartSlide(const FVector& To, int32 LogicFrame, int32 Frames)
{
	SlideFrom = GetActorLocation();
	SlideTo = To;
	SlideStartFrame = LogicFrame;
	SlideFrames = FMath::Max(Frames, 1);
	bSliding = true;
}

void AJRPGBattlePawn::UpdateSlide(int32 LogicFrame)
{
	if (!bSliding)
	{
		return;
	}
	const float Alpha = FMath::Clamp(static_cast<float>(LogicFrame - SlideStartFrame) / static_cast<float>(SlideFrames), 0.0f, 1.0f);
	SetActorLocation(FMath::Lerp(SlideFrom, SlideTo, Alpha));
	if (Alpha >= 1.0f)
	{
		bSliding = false;
	}
}

float AJRPGBattlePawn::ReachFor(const AJRPGBattlePawn* Target) const
{
	if (ReachDistance > 0.0f)
	{
		return ReachDistance;
	}
	// A lei de alcance do original (FUN_8004E2F0): o monstro bate de acordo com o próprio tamanho; o herói, com o do alvo
	// e o desvio dele. Classe 0 = o monstro não a traz: a padrão
	int32 SizeClass = 0;
	float OffsetUnits = 0.0f;
	if (Combatant.Type == EJRPGCombatantType::Enemy)
	{
		SizeClass = Combatant.SizeClass;
	}
	else
	{
		SizeClass = Target ? Target->GetCombatant().SizeClass : 0;
		OffsetUnits = HeroReachOffsetUnits(Combatant.Key);
	}
	if (SizeClass <= 0)
	{
		SizeClass = DefaultSizeClass;
	}
	const float Units = FMath::Max(ReachThresholdUnits(SizeClass) - OffsetUnits, 0.0f);
	return FMath::Max(Units * UnitsToCm - StopMargin, 20.0f);
}

FVector AJRPGBattlePawn::ComputeStopPoint(const FVector& From, const AJRPGBattlePawn* Target) const
{
	if (!Target)
	{
		return From;
	}
	const FVector TargetLocation = Target->GetSettledLocation();
	const FVector Flat = FVector(TargetLocation.X - From.X, TargetLocation.Y - From.Y, 0.0).GetSafeNormal();
	const float Reach = ReachFor(Target);
	// Já ao alcance (ou em cima do alvo): fica onde está. Senão o alvo é a parede: para a Reach dele
	if (Flat.IsNearlyZero() || FVector::Dist2D(TargetLocation, From) <= Reach)
	{
		return From;
	}
	FVector Stop = TargetLocation - Flat * Reach;
	Stop.Z = From.Z;
	return ClampToArena(From, Stop);
}

void AJRPGBattlePawn::FollowTarget(int32 LogicFrame)
{
	// O original re-vira o atacante a cada passada e, a cada clip que ele commita, fecha a distância até o alvo (o "reach snap",
	// battle-action.md): o alvo recuou com o golpe anterior, e a combo continua ao alcance
	const AJRPGBattlePawn* Foe = TargetPawn.Get();
	if (!Foe)
	{
		return;
	}
	const FVector Current = GetActorLocation();
	const FVector Flat = FVector(Foe->GetSettledLocation().X - Current.X, Foe->GetSettledLocation().Y - Current.Y, 0.0).GetSafeNormal();
	if (Flat.IsNearlyZero())
	{
		return;
	}
	SetActorRotation(Flat.Rotation());
	const FVector Stop = ComputeStopPoint(Current, Foe);
	if (FVector::Dist2D(Stop, Current) > 5.0)
	{
		ApproachTo = Stop;
		StartSlide(Stop, LogicFrame, FollowFrames);
	}
}

void AJRPGBattlePawn::ReactToHit(const AJRPGBattlePawn* Attacker, int32 LogicFrame, bool bKnockdown, bool bBlocked)
{
	// Quem está na ação dele não é mexido por fora; sem atacante, não há para onde virar
	if (!Attacker || bActionActive)
	{
		return;
	}
	const FVector Here = GetActorLocation();
	const FVector Toward = FVector(Attacker->GetActorLocation().X - Here.X, Attacker->GetActorLocation().Y - Here.Y, 0.0).GetSafeNormal();
	if (Toward.IsNearlyZero())
	{
		return;
	}
	// Vira para quem bateu (o golpe e o block): senão quem apanha de costas fica de costas
	SetActorRotation(FRotator(0.0f, Toward.Rotation().Yaw, 0.0f));

	const float Distance = bBlocked ? BlockKnockback : (bKnockdown ? KnockdownKnockback : FlinchKnockback);
	if (Distance <= 0.0f)
	{
		return;
	}
	// O recuo vai para longe de quem bateu, a partir de onde o deslize em curso acaba; a arena limita (inteiro, ou metade)
	const FVector From = GetSettledLocation();
	FVector To = From - Toward * Distance;
	To.Z = From.Z;
	FVector Final = ClampToArena(From, To);
	if (Final.Equals(From, 1.0))
	{
		FVector Half = From - Toward * (Distance * 0.5f);
		Half.Z = From.Z;
		Final = ClampToArena(From, Half);
	}
	if (!Final.Equals(From, 1.0))
	{
		StartSlide(Final, LogicFrame, KnockbackFrames);
	}
}

FVector AJRPGBattlePawn::ClampToArena(const FVector& From, const FVector& To) const
{
	// A arena limita todo mundo (o alvo é a parede, a arena a outra): um passo que sairia da área não acontece. Só se
	// a posição de partida está dentro, para uma área mal desenhada não congelar o pawn
	const AJRPGBattleArena* Bounds = Arena.Get();
	if (Bounds && Bounds->bUseArea && Bounds->ContainsPoint(From) && !Bounds->ContainsPoint(To))
	{
		return From;
	}
	return To;
}

void AJRPGBattlePawn::BeginAction(const FJRPGActionTimeline& Timeline, const AJRPGBattlePawn* Target)
{
	Action = Timeline;
	bActionActive = true;
	PlayedStep = -1;
	StrikesPlayed = 0;
	TargetPawn = Target;
	GetWorldTimerManager().ClearTimer(ReactionTimer);
	PendingFollowRole = NAME_None;
	// Um recuo ou um deslize de antes acaba aqui: a ação parte de onde ele está
	bSliding = false;

	ApproachFrom = GetActorLocation();
	ApproachTo = ApproachFrom;
	if (Target)
	{
		const FVector TargetLocation = Target->GetSettledLocation();
		const FVector Flat = FVector(TargetLocation.X - ApproachFrom.X, TargetLocation.Y - ApproachFrom.Y, 0.0).GetSafeNormal();
		if (!Flat.IsNearlyZero())
		{
			SetActorRotation(Flat.Rotation());
			// O alvo é a parede: anda até o alcance dele e para; mais perto que isso, não anda
			ApproachTo = ComputeStopPoint(ApproachFrom, Target);
		}
	}

	// Anda só se há o que andar: o alvo já ao alcance (ou quase) não toca o clip de andar, que ficaria andando no lugar
	const bool bWalks = Timeline.LeadFrames > 0 && FVector::Dist2D(ApproachTo, ApproachFrom) > MinWalkDistance;
	if (bWalks)
	{
		bApproachSettled = false;
		SetPhase(EJRPGActionPhase::Advance);
		float Rate = 1.0f;
		if (UAnimSequence* Walk = FindRoleClip(RoleWalk, Rate))
		{
			PlayClip(Walk, true, Rate);
		}
	}
	else
	{
		// Perto o bastante: vai direto ao lugar de bater e arma o golpe
		if (!ApproachTo.Equals(ApproachFrom, 1.0))
		{
			SetActorLocation(ApproachTo);
		}
		ApproachFrom = ApproachTo;
		bApproachSettled = true;
		SetPhase(EJRPGActionPhase::Windup);
	}
}

void AJRPGBattlePawn::PlayStep(const FJRPGActionStep& Step)
{
	UAnimSequence* Clip = nullptr;
	float Rate = 1.0f;
	if (Combatant.Type == EJRPGCombatantType::Enemy)
	{
		// O clip do golpe é o da linha da ação (a escala do passo sobre a velocidade normal 8)
		const FJRPGEnemyActionData* Row = FindEnemyAction(Step.EntryIndex);
		if (Row && !Row->Anim.IsNull())
		{
			Clip = Row->Anim.LoadSynchronous();
			Rate = static_cast<float>(FMath::Max(Row->Rate, 1)) * Step.GameSpeed / 8.0f;
		}
		else if (EnemyAnims.Attacks.Num() > 0)
		{
			Clip = EnemyAnims.Attacks[0].LoadSynchronous();   // o golpe padrão: o primeiro ataque do conjunto
			Rate = 2.0f;
		}
	}
	else
	{
		if (Step.Kind == EJRPGActionStepKind::Art && !Step.ArtKey.IsNone())
		{
			if (const FJRPGArtData* ArtRow = UJRPGArtSequenceLibrary::FindArt(Step.ArtKey))
			{
				Clip = ArtRow->Anim.LoadSynchronous();
			}
		}
		else
		{
			Clip = UJRPGArtSequenceLibrary::LoadHeroClip(Combatant.Key, Step.ClipName.ToString());
		}
		// O RateScale do clip já traz a velocidade; o play rate leva à escala do passo (UJRPGArtSequenceLibrary::StepPlayRate)
		if (Clip && Clip->RateScale > KINDA_SMALL_NUMBER)
		{
			Rate = Step.GameSpeed / (4.0f * Clip->RateScale);
		}
	}
	if (Clip)
	{
		PlayClip(Clip, false, Rate);
	}
}

void AJRPGBattlePawn::UpdatePlayback(int32 LogicFrame)
{
	// O recuo de quem apanhou e o acompanhar do atacante andam em quadros lógicos, ação ou não
	UpdateSlide(LogicFrame);
	if (!bActionActive)
	{
		return;
	}
	const int32 Elapsed = LogicFrame - Action.StartFrame;

	// A aproximação: da posição de saída até o alcance do alvo, nos LeadFrames; fechada a aproximação, só os deslizes
	// (acompanhar o alvo que recuou) mexem nele
	if (Action.LeadFrames > 0 && Elapsed < Action.LeadFrames)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Elapsed) / static_cast<float>(Action.LeadFrames), 0.0f, 1.0f);
		SetActorLocation(FMath::Lerp(ApproachFrom, ApproachTo, Alpha));
	}
	else if (!bApproachSettled)
	{
		bApproachSettled = true;
		if (!bSliding && !GetActorLocation().Equals(ApproachTo, 1.0))
		{
			SetActorLocation(ApproachTo);
		}
	}

	// Entra no passo que começou (um por vez: um quadro lento recupera o atraso passo a passo)
	while (PlayedStep + 1 < Action.Steps.Num() && Elapsed >= Action.Steps[PlayedStep + 1].StartFrame)
	{
		++PlayedStep;
		const FJRPGActionStep& Step = Action.Steps[PlayedStep];
		// A cada clip que entra, vira e fecha a distância até o alvo (o recuo do golpe anterior)
		FollowTarget(LogicFrame);
		PlayStep(Step);
		switch (Step.Kind)
		{
		case EJRPGActionStepKind::ArtStarter:
		case EJRPGActionStepKind::SpecialStarter:
			SetPhase(EJRPGActionPhase::Windup);
			break;
		default:
			SetPhase(StrikesPlayed == 0 ? EJRPGActionPhase::Strike : EJRPGActionPhase::Chain);
			++StrikesPlayed;
			break;
		}
	}

	if (Elapsed >= Action.TotalFrames)
	{
		FinishAction();
	}
	else if (PlayedStep >= 0 && PlayedStep == Action.Steps.Num() - 1 && Elapsed >= Action.Steps[PlayedStep].EndFrame)
	{
		SetPhase(EJRPGActionPhase::Recovery);
	}
}

void AJRPGBattlePawn::FinishAction()
{
	// A ação acabou: fica onde está (sem voltar ao lugar) e volta ao idle
	bActionActive = false;
	SetPhase(EJRPGActionPhase::Done);
	if (!bDown)
	{
		PlayIdle();
	}
	SetPhase(EJRPGActionPhase::Idle);
}

void AJRPGBattlePawn::SyncActionTimeline(const FJRPGActionTimeline& Current)
{
	if (!bActionActive || Current.ActionIndex != Action.ActionIndex
		|| (Current.TotalFrames == Action.TotalFrames && Current.Steps.Num() == Action.Steps.Num()))
	{
		return;
	}
	// A cópia do ActionStart ficou velha: o resolvedor cortou o resto (nenhum passo novo começa depois do corte)
	Action = Current;
	PlayedStep = FMath::Min(PlayedStep, Action.Steps.Num() - 1);
}

void AJRPGBattlePawn::EndAction()
{
	if (bActionActive)
	{
		FinishAction();
	}
}

void AJRPGBattlePawn::ShowEvent(const FJRPGBattleEvent& Event, const FString& Text, bool bAsTarget)
{
	if (bAsTarget && !bDown)
	{
		switch (Event.Type)
		{
		case EJRPGBattleEventType::Damage:
			if (Event.Flags & FJRPGBattleEventFlag::Knockdown)
			{
				// Cai e levanta (o golpe que mata troca isto pela queda no Death, que vem no mesmo quadro): Knockdown -> GetUp -> Idle
				PlayReaction(RoleKnockdown, false, RoleGetUp, false);
			}
			else
			{
				PlayReaction((Event.Flags & FJRPGBattleEventFlag::LowHit) ? RoleFlinch2 : RoleFlinch1, false);
			}
			break;
		case EJRPGBattleEventType::Block:
			PlayReaction(RoleBlock, false);
			break;
		case EJRPGBattleEventType::Death:
			bDown = true;
			bActionActive = false;
			if (Combatant.Type == EJRPGCombatantType::Enemy)
			{
				PlayReaction(RoleKnockdown, true);   // o monstro caído segura o último quadro do knockdown
			}
			else
			{
				PlayReaction(RoleDying, false, RoleDown, true);   // o herói cai (Dying) e fica no clip do caído (Down)
			}
			break;
		default:
			break;
		}
	}

	if (bShowEventText && !Text.IsEmpty() && GetWorld())
	{
		// O resumo da combo fica acima do número do último golpe
		const float Height = (Capsule ? Capsule->GetScaledCapsuleHalfHeight() + 40.0f : 140.0f) + (Event.Type == EJRPGBattleEventType::ComboTotal ? 45.0f : 0.0f);
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, Height), Text, nullptr, EventColor(Event.Type), EventTextSeconds, true, 1.4f);
	}
	OnBattleEventShown(Event, Text);
}
