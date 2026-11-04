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

	int32 TagForRole(FName Role)
	{
		if (Role == RoleIdle)      { return 0; }
		if (Role == RoleWalk)      { return 1; }
		if (Role == RoleFlinch1)   { return 2; }
		if (Role == RoleFlinch2)   { return 3; }
		if (Role == RoleKnockdown) { return 4; }
		if (Role == RoleBlock)     { return 0x0B; }
		return -1;
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
		if (UJRPGFieldEnemyAnimInstance* Anim = Cast<UJRPGFieldEnemyAnimInstance>(Mesh->GetAnimInstance()))
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

void AJRPGBattlePawn::PlayReaction(FName ClipRole, bool bHold)
{
	float Rate = 1.0f;
	UAnimSequence* Clip = FindRoleClip(ClipRole, Rate);
	if (!Clip && ClipRole == RoleDying)
	{
		Clip = FindRoleClip(RoleKnockdown, Rate);   // o herói sem o clip de morrer cai pelo de derrubar
	}
	if (!Clip)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(ReactionTimer);
	PlayClip(Clip, false, Rate);
	if (!bHold)
	{
		const float Speed = FMath::Max(Rate * FMath::Max(Clip->RateScale, KINDA_SMALL_NUMBER), KINDA_SMALL_NUMBER);
		GetWorldTimerManager().SetTimer(ReactionTimer, this, &AJRPGBattlePawn::ReturnToIdle, FMath::Max(Clip->GetPlayLength() / Speed, 0.1f), false);
	}
}

void AJRPGBattlePawn::ReturnToIdle()
{
	if (!bDown && !bActionActive)
	{
		PlayIdle();
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

	ApproachFrom = GetActorLocation();
	ApproachTo = ApproachFrom;
	if (Target)
	{
		const FVector TargetLocation = Target->GetActorLocation();
		const FVector Flat = FVector(TargetLocation.X - ApproachFrom.X, TargetLocation.Y - ApproachFrom.Y, 0.0).GetSafeNormal();
		if (!Flat.IsNearlyZero())
		{
			SetActorRotation(Flat.Rotation());
			// O alvo é a parede: anda até o alcance dele e para; mais perto que isso, não anda
			if (FVector::Dist2D(TargetLocation, ApproachFrom) > ReachDistance)
			{
				FVector Stop = TargetLocation - Flat * ReachDistance;
				Stop.Z = ApproachFrom.Z;
				ApproachTo = ClampToArena(ApproachFrom, Stop);
			}
		}
	}

	if (Timeline.LeadFrames > 0 && !ApproachTo.Equals(ApproachFrom, 1.0))
	{
		SetPhase(EJRPGActionPhase::Advance);
		float Rate = 1.0f;
		if (UAnimSequence* Walk = FindRoleClip(RoleWalk, Rate))
		{
			PlayClip(Walk, true, Rate);
		}
	}
	else
	{
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
	if (!bActionActive)
	{
		return;
	}
	const int32 Elapsed = LogicFrame - Action.StartFrame;

	// A aproximação: da posição de saída até o alcance do alvo, nos LeadFrames
	if (Action.LeadFrames > 0 && Elapsed < Action.LeadFrames)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Elapsed) / static_cast<float>(Action.LeadFrames), 0.0f, 1.0f);
		SetActorLocation(FMath::Lerp(ApproachFrom, ApproachTo, Alpha));
	}
	else if (!GetActorLocation().Equals(ApproachTo, 1.0))
	{
		SetActorLocation(ApproachTo);
	}

	// Entra no passo que começou (um por vez: um quadro lento recupera o atraso passo a passo)
	while (PlayedStep + 1 < Action.Steps.Num() && Elapsed >= Action.Steps[PlayedStep + 1].StartFrame)
	{
		++PlayedStep;
		const FJRPGActionStep& Step = Action.Steps[PlayedStep];
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
			PlayReaction((Event.Flags & FJRPGBattleEventFlag::Knockdown) ? RoleKnockdown
				: (Event.Flags & FJRPGBattleEventFlag::LowHit) ? RoleFlinch2 : RoleFlinch1, false);
			break;
		case EJRPGBattleEventType::Block:
			PlayReaction(RoleBlock, false);
			break;
		case EJRPGBattleEventType::Death:
			bDown = true;
			bActionActive = false;
			PlayReaction(Combatant.Type == EJRPGCombatantType::Enemy ? RoleKnockdown : RoleDying, true);
			break;
		default:
			break;
		}
	}

	if (bShowEventText && !Text.IsEmpty() && GetWorld())
	{
		const float Height = Capsule ? Capsule->GetScaledCapsuleHalfHeight() + 40.0f : 140.0f;
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, Height), Text, nullptr, EventColor(Event.Type), EventTextSeconds, true, 1.4f);
	}
	OnBattleEventShown(Event, Text);
}
