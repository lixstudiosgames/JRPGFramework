#include "Battle/JRPGBattlePawn.h"
#include "Arts/JRPGArtSequence.h"
#include "Battle/BattleFormulas.h"
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
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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

	// A velocidade do clip de andar do herói no disco (record[0], entrada 1, +0x0C), em unidades do PS1 por vsync; 0 = não é herói
	float HeroWalkSpeedUnits(FName Hero)
	{
		if (Hero == FName(TEXT("Vahn")))  { return 42.0f; }
		if (Hero == FName(TEXT("Noa")))   { return 44.0f; }
		if (Hero == FName(TEXT("Gala")))  { return 28.0f; }
		if (Hero == FName(TEXT("Terra"))) { return 48.0f; }
		return 0.0f;
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

// Definido aqui (não inline no header): atribuir ao TWeakObjectPtr exige o tipo completo da arena
void AJRPGBattlePawn::SetArena(AJRPGBattleArena* InArena)
{
	Arena = InArena;
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
	ReactionQueue.Reset();
	ReactionEndFrame = INDEX_NONE;
	IdleHandoffFrame = INDEX_NONE;
	StepClipEndFrame = INDEX_NONE;
	bDeathFadeQueued = false;
	bDeathFading = false;
	HitResults.Reset();
	HeldSounds.Reset();
	bSoundContextActive = false;
	SoundContextEndFrame = INDEX_NONE;
	// A pose do idle é medida de novo (o modelo pode ser outro)
	bHasNeutralCentroid = false;
	IdleSinceFrame = INDEX_NONE;
	ApproachStallFrames = 0;
	ActiveMotion = FClipMotion();
	MotionStartFrame = INDEX_NONE;
	MotionEndFrame = INDEX_NONE;
	MotionAppliedFrame = INDEX_NONE;
	bHasHeroReactionRow = false;
	bEnemyHasMotionData = false;
	SetActorHiddenInGame(false);
	if (InCombatant.Type == EJRPGCombatantType::Enemy)
	{
		if (const UDataTable* Actions = UCoreSubsystem::ResolveDataTable(TEXT("DT_EnemyActions")))
		{
			Actions->ForeachRow<FJRPGEnemyActionData>(TEXT("BattlePawn"), [&](const FName&, const FJRPGEnemyActionData& Row)
			{
				if (Row.Enemy == InCombatant.Key)
				{
					EnemyActions.Add(Row);
					// Um DT_EnemyActions importado antes das colunas RootSpeed / EndStep traz tudo 0: aí vale a heurística antiga
					bEnemyHasMotionData |= Row.RootSpeed != 0 || Row.EndStep != 0;
				}
			});
		}
		FJRPGEnemyData EnemyRow;
		if (UJRPGEncounterLibrary::GetEnemyData(InCombatant.Key, EnemyRow))
		{
			EnemyAnims = EnemyRow.Anims;
		}
	}
	else if (const UDataTable* Reactions = UCoreSubsystem::ResolveDataTable(TEXT("DT_HeroReactions")))
	{
		// O movimento das reações do herói (o knockdown que desliza, o flinch que termina para trás)
		if (const FJRPGHeroReactionData* Row = Reactions->FindRow<FJRPGHeroReactionData>(InCombatant.Key, TEXT("BattlePawn"), false))
		{
			HeroReactionRow = *Row;
			// Importado antes das colunas de movimento: tudo 0, e vale a heurística antiga
			bHasHeroReactionRow = Row->FlinchHighRootSpeed != 0 || Row->FlinchHighEndStep != 0 || Row->FlinchLowRootSpeed != 0
				|| Row->FlinchLowEndStep != 0 || Row->BlockRootSpeed != 0 || Row->KnockdownRootSpeed != 0 || Row->GetUpEndStep != 0;
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
	// O clip de uma vez só entra sem mistura (corte seco); o loop mistura por AnimBlendTime
	PlayClipBlended(Clip, bLoop, PlayRate, bLoop ? AnimBlendTime : 0.0f);
}

void AJRPGBattlePawn::PlayClipBlended(UAnimSequence* Clip, bool bLoop, float PlayRate, float BlendTime)
{
	if (!Clip || bUsesAnimClass)
	{
		return;
	}
	// O idle passa a contar daqui (a pose dele é medida depois de a mistura acabar); outro clip para a contagem
	if (Clip == IdleClip)
	{
		if (IdleSinceFrame == INDEX_NONE)
		{
			IdleSinceFrame = LastLogicFrame;
			IdleFramesSeen = 0;
		}
	}
	else
	{
		IdleSinceFrame = INDEX_NONE;
	}
	// Tudo toca na MESMA instância do crossfade (UJRPGFieldEnemyAnimInstance), em loop (idle, andar) ou de uma vez só (golpe,
	// reação). Antes o clip de uma vez só ia por Mesh->PlayAnimation, que troca a malha para o modo single node e destrói a
	// instância; voltar dali para o idle dependia de reconstruí-la no meio da luta, e a Noa ficava parada num quadro depois de
	// uma Art (bug 18). Sem trocar de modo não há o que reconstruir
	UJRPGFieldEnemyAnimInstance* Anim = Cast<UJRPGFieldEnemyAnimInstance>(Mesh->GetAnimInstance());
	if (!Anim)
	{
		// A malha saiu do modo Anim Blueprint (não deveria): volta a ele com a instância do crossfade
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Anim = Cast<UJRPGFieldEnemyAnimInstance>(Mesh->GetAnimInstance());
		if (!Anim)
		{
			Mesh->SetAnimInstanceClass(UJRPGFieldEnemyAnimInstance::StaticClass());
			Anim = Cast<UJRPGFieldEnemyAnimInstance>(Mesh->GetAnimInstance());
		}
	}
	if (Anim)
	{
		Anim->PlayClip(Clip, PlayRate, BlendTime, bLoop);
		return;
	}
	// Sem instância (a malha ainda sem asset): o último recurso é o single node
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

void AJRPGBattlePawn::StartReaction(const TArray<FReactionStep>& Steps, int32 LogicFrame, int32 GameSpeed)
{
	// Uma reação que corta outra no meio (um golpe no flinch): o original anda a parte tocada do passo do fim do clip cortado
	// (0x80047950..0x80047A28)
	if (!bDown && !bActionActive && ReactionEndFrame != INDEX_NONE)
	{
		UpdateRootMotion(LogicFrame);
		SlideBy(EndStepShift(ClipMotionFraction(LogicFrame)), LogicFrame);
	}
	ReactionQueue = Steps;
	ReactionGameSpeed = FMath::Max(GameSpeed, 1);
	ReactionEndFrame = INDEX_NONE;
	ReactionLastEnd = EReactionEnd::Idle;
	// Uma reação nova toma o lugar do idle que a ação anterior deixou pendente
	IdleHandoffFrame = INDEX_NONE;
	AdvanceReaction(LogicFrame);
}

void AJRPGBattlePawn::AdvanceReaction(int32 LogicFrame)
{
	while (!ReactionQueue.IsEmpty())
	{
		const FReactionStep Step = ReactionQueue[0];
		ReactionQueue.RemoveAt(0);
		const bool bLast = ReactionQueue.IsEmpty();

		float Rate = 1.0f;
		UAnimSequence* Clip = FindRoleClip(Step.Role, Rate);
		if (!Clip)
		{
			// Sem o clip deste papel a cadeia segue direto para o próximo (quem não tem o GetUp volta ao idle depois do Knockdown)
			if (bLast)
			{
				ReactionLastEnd = Step.End;
				ReactionEndFrame = INDEX_NONE;
				OnReactionEnded(LogicFrame);
			}
			continue;
		}
		ReactionLastEnd = Step.End;
		// A reação toca na velocidade em que o golpe foi tomado (uma Art deixa o jogo a 4 ou a 2)
		Rate *= static_cast<float>(ReactionGameSpeed) / static_cast<float>(UBattleFormulas::NormalGameSpeed);
		if (bLast && Step.End == EReactionEnd::Loop)
		{
			// O herói caído repete o clip dele para sempre (a entrada 8 do original re-comita a si mesma a cada fim natural, com a trava
			// que para a velocidade de raiz)
			PlayClip(Clip, true, Rate);
			ReactionEndFrame = INDEX_NONE;
			BeginClipMotion(FClipMotion(), LogicFrame, INDEX_NONE, ReactionGameSpeed);
			return;
		}
		PlayClip(Clip, false, Rate);
		const float Speed = FMath::Max(Rate * FMath::Max(Clip->RateScale, KINDA_SMALL_NUMBER), KINDA_SMALL_NUMBER);
		const int32 Frames = FMath::Max(FMath::CeilToInt(Clip->GetPlayLength() / Speed * static_cast<float>(UBattleFormulas::StepsPerSecond)), 1);
		ReactionEndFrame = LogicFrame + Frames;
		// O clip da reação move o ator enquanto toca (o knockdown desliza a queda inteira) e anda o passo do fim dele quando acaba
		BeginClipMotion(RoleMotion(Step.Role), LogicFrame, ReactionEndFrame, ReactionGameSpeed);
		return;
	}
}

void AJRPGBattlePawn::OnReactionEnded(int32 LogicFrame)
{
	ReactionEndFrame = INDEX_NONE;
	// A velocidade de raiz até o fim natural do clip que acabou (um quadro atrasado não perde o resto)
	UpdateRootMotion(LogicFrame);
	if (!ReactionQueue.IsEmpty())
	{
		if (bActionActive)
		{
			ReactionQueue.Reset();   // ele já está na ação dele: a reação não toca por cima
			return;
		}
		// O fim natural do clip anda o passo do fim dele (o knockdown antes do levantar); quem caiu morto não anda (o original pula o
		// passo com o HP em 0)
		if (!bDown && !bDeathFadeQueued && !bDeathFading)
		{
			SlideBy(EndStepShift(1.0f), LogicFrame);
		}
		AdvanceReaction(LogicFrame);
		return;
	}
	if (bActionActive)
	{
		return;
	}
	switch (ReactionLastEnd)
	{
	case EReactionEnd::Idle:
		ReturnToIdle();
		break;
	case EReactionEnd::Hold:
		// O monstro derrotado: o corpo segura o último quadro do knockdown e, daí, desbota e afunda
		if (bDeathFadeQueued)
		{
			BeginDeathFade(LogicFrame);
		}
		break;
	case EReactionEnd::Loop:
		break;
	}
}

void AJRPGBattlePawn::ReturnToIdle()
{
	if (bDown || bActionActive)
	{
		return;
	}
	// O passo do fim do clip vira posição, inteiro: o do disco (o Slash Kick termina 534 unidades à frente), sem trava de alcance.
	// Quem acaba perto demais do alvo é afastado pelo recuo dentro do alcance e pela separação, como no original
	SettleIntoIdle();
}

FVector AJRPGBattlePawn::PoseCentroid() const
{
	// Os modelos do PS1 são peças rígidas, cada uma num bone que se move inteiro (a raiz fica parada): o centro das peças é onde o
	// corpo está em relação ao ator
	const TArray<FTransform>& Pose = Mesh->GetComponentSpaceTransforms();
	if (Pose.Num() < 2)
	{
		return FVector::ZeroVector;
	}
	FVector Sum = FVector::ZeroVector;
	for (int32 Index = 1; Index < Pose.Num(); ++Index)
	{
		Sum += Pose[Index].GetLocation();
	}
	return Sum / static_cast<double>(Pose.Num() - 1);
}

FVector AJRPGBattlePawn::BodyOffset() const
{
	// O body pair do original (FUN_8004998C a cada quadro desenhado): a soma das translações das peças da pose dividida pelo número
	// de peças, girada pela direção do ator e somada à posição dele. As peças de equipamento são outros componentes (Leader Pose),
	// então a média da Mesh é só a do corpo, como a do original (o número de peças do stream)
	if (!bUseBodyPair || !Mesh || Mesh->GetComponentSpaceTransforms().Num() < 2)
	{
		return FVector::ZeroVector;
	}
	FVector Offset = Mesh->GetComponentTransform().TransformPosition(PoseCentroid()) - GetActorLocation();
	Offset.Z = 0.0;
	return Offset;
}

void AJRPGBattlePawn::SettleIntoIdle()
{
	if (!IdleClip || IdleSinceFrame != INDEX_NONE)
	{
		PlayIdle();
		return;
	}
	// O passo do fim do clip que acabou (o do disco; sem dado, a heurística antiga pelo centro da pose) vira posição DURANTE a mistura
	// para o idle: o clip que sai está deslocado e o ator ainda não andou; o idle não está deslocado e o ator já andou tudo; no meio
	// os dois se compensam. O corpo fica onde o clip o deixou e a pose troca suave, sem corte seco nem piscada
	UpdateRootMotion(LastLogicFrame);
	const FVector Shift = EndStepShift(1.0f, true);
	ActiveMotion = FClipMotion();
	MotionStartFrame = INDEX_NONE;
	MotionEndFrame = INDEX_NONE;
	SlideBy(Shift, LastLogicFrame);
	PlayIdle();
}

AJRPGBattlePawn::FClipMotion AJRPGBattlePawn::RoleMotion(FName ClipRole) const
{
	FClipMotion Motion;
	if (!bUseClipMotion)
	{
		return Motion;
	}
	if (Combatant.Type == EJRPGCombatantType::Enemy)
	{
		const int32 Tag = TagForRole(ClipRole);
		const FJRPGEnemyActionData* Row = bEnemyHasMotionData && Tag >= 0 ? FindEnemyActionByTag(Tag) : nullptr;
		if (Row)
		{
			Motion.RootSpeed = Row->RootSpeed;
			Motion.EndStep = Row->EndStep;
			Motion.bValid = true;
		}
		return Motion;
	}
	if (!bHasHeroReactionRow)
	{
		return Motion;
	}
	const FJRPGHeroReactionData& Row = HeroReactionRow;
	Motion.bValid = true;
	if (ClipRole == RoleFlinch1)        { Motion.RootSpeed = Row.FlinchHighRootSpeed; Motion.EndStep = Row.FlinchHighEndStep; }
	else if (ClipRole == RoleFlinch2)   { Motion.RootSpeed = Row.FlinchLowRootSpeed;  Motion.EndStep = Row.FlinchLowEndStep; }
	else if (ClipRole == RoleBlock)     { Motion.RootSpeed = Row.BlockRootSpeed;      Motion.EndStep = Row.BlockEndStep; }
	else if (ClipRole == RoleKnockdown) { Motion.RootSpeed = Row.KnockdownRootSpeed;  Motion.EndStep = Row.KnockdownEndStep; }
	else if (ClipRole == RoleGetUp)     { Motion.RootSpeed = Row.GetUpRootSpeed;      Motion.EndStep = Row.GetUpEndStep; }
	else if (ClipRole == RoleDying)     { Motion.RootSpeed = Row.DyingRootSpeed;      Motion.EndStep = Row.DyingEndStep; }
	else if (ClipRole != RoleDown)      { Motion.bValid = false; }   // o caído (8) é 0 / 0 em todo arquivo
	return Motion;
}

AJRPGBattlePawn::FClipMotion AJRPGBattlePawn::StepMotion(const FJRPGActionStep& Step) const
{
	FClipMotion Motion;
	if (!bUseClipMotion)
	{
		return Motion;
	}
	if (Combatant.Type == EJRPGCombatantType::Enemy)
	{
		if (const FJRPGEnemyActionData* Row = bEnemyHasMotionData ? FindEnemyAction(Step.EntryIndex) : nullptr)
		{
			Motion.RootSpeed = Row->RootSpeed;
			Motion.EndStep = Row->EndStep;
			Motion.bValid = true;
		}
		return Motion;
	}
	if (Step.Kind == EJRPGActionStepKind::Art && !Step.ArtKey.IsNone())
	{
		const UDataTable* ArtHits = UCoreSubsystem::ResolveDataTable(TEXT("DT_ArtHits"));
		const FJRPGArtHitData* Row = ArtHits ? ArtHits->FindRow<FJRPGArtHitData>(Step.ArtKey, TEXT("BattlePawn"), false) : nullptr;
		// Uma tabela importada antes das colunas de movimento traz 0 / 0: aí vale a heurística antiga
		if (Row && (Row->RootSpeed != 0 || Row->EndStep != 0))
		{
			Motion.RootSpeed = Row->RootSpeed;
			Motion.EndStep = Row->EndStep;
			Motion.bValid = true;
		}
		return Motion;
	}
	// Golpe simples: no disco todo registro é 4 / 0 (anda só fora do alcance, que o reach snap já cobre, e termina onde começou); os
	// starters seguem para o passo seguinte, que faz o snap
	Motion.bValid = true;
	return Motion;
}

void AJRPGBattlePawn::BeginClipMotion(const FClipMotion& Motion, int32 StartFrame, int32 EndFrame, int32 GameSpeed)
{
	ActiveMotion = Motion;
	MotionStartFrame = StartFrame;
	MotionEndFrame = EndFrame;
	MotionAppliedFrame = StartFrame;
	MotionGameSpeed = FMath::Max(GameSpeed, 1);
}

void AJRPGBattlePawn::UpdateRootMotion(int32 LogicFrame)
{
	if (!bUseClipMotion || !ActiveMotion.bValid || ActiveMotion.RootSpeed == 0 || MotionStartFrame == INDEX_NONE)
	{
		return;
	}
	// A velocidade de raiz anda enquanto o clip toca, do último quadro aplicado até agora (ou até o fim natural do clip)
	const int32 FirstFrame = FMath::Max(MotionAppliedFrame, MotionStartFrame);
	const int32 LastFrame = MotionEndFrame != INDEX_NONE ? FMath::Min(LogicFrame, MotionEndFrame) : LogicFrame;
	if (LastFrame <= FirstFrame)
	{
		return;
	}
	MotionAppliedFrame = LastFrame;
	FVector Facing = GetActorForwardVector();
	Facing.Z = 0.0;
	Facing = Facing.GetSafeNormal();
	if (Facing.IsNearlyZero())
	{
		return;
	}
	// facing * speed * step * escala >> 15 por passada = speed unidades por vsync na escala 8 (0x80047D20..0x80047E18)
	double Move = static_cast<double>(ActiveMotion.RootSpeed) * UnitsToCm * static_cast<double>(MotionGameSpeed)
		/ static_cast<double>(UBattleFormulas::NormalGameSpeed) * static_cast<double>(LastFrame - FirstFrame);
	if (ActiveMotion.RootSpeed > 0)
	{
		// Positiva: só enquanto a lei de alcance diz "fora", e só até a borda (uma investida não entra no alvo)
		const AJRPGBattlePawn* Foe = TargetPawn.Get();
		if (!bActionActive || !Foe || Foe == this)
		{
			return;
		}
		const double Gap = FVector::Dist2D(GetActorLocation(), Foe->GetBodyLocation());
		const double Edge = static_cast<double>(ReachFor(Foe) + StopMargin);
		if (Gap <= Edge)
		{
			return;
		}
		Move = FMath::Min(Move, Gap - Edge);
	}
	// Negativa: sempre, sem teste de alcance (o golpe empurra o corpo para trás a queda inteira)
	const FVector Here = GetActorLocation();
	const FVector Goal = ClampToArena(Here, Here + Facing * Move);
	TranslateBy(Goal - Here);
}

FVector AJRPGBattlePawn::EndStepShift(float Fraction, bool bHeuristic) const
{
	if (bUseClipMotion && ActiveMotion.bValid)
	{
		if (ActiveMotion.EndStep == 0)
		{
			return FVector::ZeroVector;
		}
		FVector Facing = GetActorForwardVector();
		Facing.Z = 0.0;
		// +0x34 += sin(facing) * step >> 12: o passo vai ao longo da direção do ator (negativo = para trás)
		return Facing.GetSafeNormal() * (static_cast<double>(ActiveMotion.EndStep) * UnitsToCm * FMath::Clamp(Fraction, 0.0f, 1.0f));
	}
	if (bHeuristic && bApplyEndStep && bHasNeutralCentroid)
	{
		// Sem dado do clip: o deslocamento do corpo (o último quadro do clip que acabou) em relação à pose do idle
		FVector Shift = Mesh->GetComponentTransform().TransformVector(PoseCentroid() - NeutralCentroid);
		Shift.Z = 0.0;
		return Shift;
	}
	return FVector::ZeroVector;
}

float AJRPGBattlePawn::ClipMotionFraction(int32 LogicFrame) const
{
	if (MotionStartFrame == INDEX_NONE || MotionEndFrame == INDEX_NONE || MotionEndFrame <= MotionStartFrame)
	{
		return 1.0f;
	}
	return FMath::Clamp(static_cast<float>(LogicFrame - MotionStartFrame) / static_cast<float>(MotionEndFrame - MotionStartFrame), 0.0f, 1.0f);
}

void AJRPGBattlePawn::SlideBy(const FVector& Shift, int32 LogicFrame)
{
	if (Shift.Size2D() < EndStepMinDistance)
	{
		return;
	}
	const FVector From = GetSettledLocation();
	const FVector To = ClampToArena(From, From + FVector(Shift.X, Shift.Y, 0.0));
	if (To.Equals(From, 1.0))
	{
		return;
	}
	// Durante a mistura para o clip seguinte: o clip que sai leva o corpo, o ator anda o mesmo tanto, e os dois se compensam
	const int32 BlendFrames = FMath::Max(FMath::CeilToInt(AnimBlendTime * static_cast<float>(UBattleFormulas::StepsPerSecond)), 1);
	StartSlide(To, LogicFrame, BlendFrames);
}

void AJRPGBattlePawn::TranslateBy(const FVector& Delta)
{
	const FVector Flat(Delta.X, Delta.Y, 0.0);
	if (Flat.IsNearlyZero())
	{
		return;
	}
	SetActorLocation(GetActorLocation() + Flat);
	ApproachFrom += Flat;
	ApproachTo += Flat;
	SlideFrom += Flat;
	SlideTo += Flat;
}

void AJRPGBattlePawn::TranslateApproachGoal(const FVector& Delta)
{
	ApproachTo += FVector(Delta.X, Delta.Y, 0.0);
}

void AJRPGBattlePawn::DelayAction(int32 Frames)
{
	if (bActionActive && Frames > 0)
	{
		Action.StartFrame += Frames;
	}
}

void AJRPGBattlePawn::StepAside(const FVector& Offset, int32 LogicFrame, int32 Frames)
{
	// Quem está na ação dele (o atacante) não é mexido por fora
	if (bActionActive || !IsStanding())
	{
		return;
	}
	const FVector From = GetSettledLocation();
	const FVector To = ClampToArena(From, From + FVector(Offset.X, Offset.Y, 0.0));
	if (!To.Equals(From, 1.0))
	{
		StartSlide(To, LogicFrame, Frames);
	}
}

void AJRPGBattlePawn::ShoveBy(const FVector& Offset)
{
	if (bActionActive || !IsStanding())
	{
		return;
	}
	const FVector From = GetActorLocation();
	const FVector To = ClampToArena(From, From + FVector(Offset.X, Offset.Y, 0.0));
	TranslateBy(To - From);
}

void AJRPGBattlePawn::ArrivalShove()
{
	AJRPGBattlePawn* Foe = TargetPawn.Get();
	if (!bApplyArrivalShove || !Foe || Foe == this)
	{
		return;
	}
	// Só na cadeia de caminhada do estado 0x16: os seis monstros com a entrada de tag 0x20 (Caruban x3, Evil Shadow, Shade,
	// Nightmare). Os heróis e os outros monstros ficam com o reach snap e o recuo dentro do alcance
	if (Combatant.Type != EJRPGCombatantType::Enemy || !FindEnemyActionByTag(0x20))
	{
		return;
	}
	// A lei de alcance do original: a posição do atacante contra o CORPO do alvo
	const FVector Here = GetActorLocation();
	const FVector There = Foe->GetBodyLocation();
	const FVector Facing = FVector(There.X - Here.X, There.Y - Here.Y, 0.0);
	const double Gap = Facing.Size2D();
	// A borda do alcance: o limiar da lei (o ReachFor já tira a margem de parada, que o original anda para dentro)
	const double Edge = ReachFor(Foe) + StopMargin;
	if (Gap >= Edge || Gap <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	Foe->ShoveBy(Facing / Gap * (Edge - Gap));
}

bool AJRPGBattlePawn::IsStanding() const
{
	return !bDown && !bDeathFadeQueued && !bDeathFading && !IsHidden();
}

int32 AJRPGBattlePawn::GetSizeClass() const
{
	if (Combatant.Type != EJRPGCombatantType::Enemy)
	{
		return 0;
	}
	return Combatant.SizeClass > 0 ? Combatant.SizeClass : DefaultSizeClass;
}

float AJRPGBattlePawn::SeparationRadiusCm() const
{
	// FUN_80050BB8 lê o raio que o stager do assento escreveu: 0x280 num herói, 0x400 na Terra, classe << 5 num monstro
	float Units = 640.0f;
	if (Combatant.Type == EJRPGCombatantType::Enemy)
	{
		Units = static_cast<float>(GetSizeClass() << 5);
	}
	else if (Combatant.Key == FName(TEXT("Terra")))
	{
		Units = 1024.0f;
	}
	return Units * UnitsToCm;
}

float AJRPGBattlePawn::WalkSpeedCm() const
{
	float Units = Combatant.Type == EJRPGCombatantType::Enemy ? 0.0f : HeroWalkSpeedUnits(Combatant.Key);
	if (Units <= 0.0f)
	{
		// O monstro: a velocidade de raiz do clip de andar dele (tag 1, +0x0C; o Gimard anda 28), senão o valor médio
		const FJRPGEnemyActionData* Walk = Combatant.Type == EJRPGCombatantType::Enemy ? FindEnemyActionByTag(1) : nullptr;
		Units = Walk && Walk->RootSpeed > 0 ? static_cast<float>(Walk->RootSpeed) : EnemyWalkSpeedUnits;
	}
	return FMath::Max(Units * UnitsToCm, 1.0f);
}

void AJRPGBattlePawn::BeginDeathFade(int32 LogicFrame)
{
	bDeathFadeQueued = false;
	bDeathFading = true;
	DeathFadeStartFrame = LogicFrame;
	DeathFadeBaseZ = GetActorLocation().Z;
}

void AJRPGBattlePawn::UpdateDeathFade(int32 LogicFrame)
{
	if (!bDeathFading)
	{
		return;
	}
	// O fade do original leva as lanes de cor de 0x100 a 0 (8 por passo, uns 32 vsyncs) com o corpo desenhado aditivo, e ele afunda
	// (classe de tamanho * passo) >> 2 unidades por passo até sumir por baixo do chão
	const int32 Total = FMath::Max(DeathFadeFrames, 1);
	const int32 Elapsed = FMath::Clamp(LogicFrame - DeathFadeStartFrame, 0, Total);
	const float Alpha = static_cast<float>(Elapsed) / static_cast<float>(Total);
	if (bDeathSink)
	{
		const int32 SizeClass = Combatant.SizeClass > 0 ? Combatant.SizeClass : DefaultSizeClass;
		FVector Here = GetActorLocation();
		Here.Z = DeathFadeBaseZ - static_cast<float>(SizeClass) * 0.25f * UnitsToCm * static_cast<float>(Elapsed);
		SetActorLocation(Here);
	}
	if (!DeathFadeParameter.IsNone())
	{
		Mesh->SetScalarParameterValueOnMaterials(DeathFadeParameter, 1.0f - Alpha);
		for (USkeletalMeshComponent* Piece : PieceComponents)
		{
			if (Piece)
			{
				Piece->SetScalarParameterValueOnMaterials(DeathFadeParameter, 1.0f - Alpha);
			}
		}
	}
	if (Elapsed >= Total)
	{
		// O corpo saiu de cena (o terceiro teste de espera do estado 0x20 do original: o nó deixou de ser desenhado)
		bDeathFading = false;
		SetActorHiddenInGame(true);
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
	// A lei de alcance do original mede o atacante (From) contra o CORPO do alvo (o body pair), não os pés
	const FVector TargetLocation = Target->GetSettledBodyLocation();
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
	// A banda de golpes do original mira o CORPO do alvo (0x801E36F0): um knockdown que leva o corpo de lado vira o atacante junto.
	// A partir de onde ele vai parar (o passo do fim do clip anterior pode estar deslizando)
	const FVector Current = GetSettledLocation();
	const FVector FoeBody = Foe->GetSettledBodyLocation();
	const FVector Flat = FVector(FoeBody.X - Current.X, FoeBody.Y - Current.Y, 0.0).GetSafeNormal();
	if (Flat.IsNearlyZero())
	{
		return;
	}
	SetActorRotation(Flat.Rotation());
	// O reach snap do commit (0x8004BF7C..0x8004C118): fora do alcance anda para a frente, dentro anda para TRÁS, até a borda. Cada
	// golpe da sequência começa na borda do alcance, o que quer que o recuo e o golpe anterior tenham feito
	const FVector Stop = ComputeSnapPoint(Current, Foe);
	if (FVector::Dist2D(Stop, Current) > 1.0)
	{
		ApproachTo = Stop;
		if (FollowFrames <= 0)
		{
			bSliding = false;
			SetActorLocation(Stop);
		}
		else
		{
			StartSlide(Stop, LogicFrame, FollowFrames);
		}
	}
}

FVector AJRPGBattlePawn::ComputeSnapPoint(const FVector& From, const AJRPGBattlePawn* Target) const
{
	if (!Target || Target == this)
	{
		return From;
	}
	const FVector TargetBody = Target->GetSettledBodyLocation();
	const FVector Flat = FVector(TargetBody.X - From.X, TargetBody.Y - From.Y, 0.0).GetSafeNormal();
	if (Flat.IsNearlyZero())
	{
		return From;
	}
	// O limiar da lei de alcance (o ReachFor tira a margem de parada da caminhada; o snap vai até o limiar mesmo). Os dois laços do
	// original param um passo de cada lado da borda: quem já está nessa faixa (a margem de parada) fica, e o recuo cuida do resto
	const double Edge = static_cast<double>(ReachFor(Target) + StopMargin);
	const double Distance = FVector::Dist2D(From, TargetBody);
	if (Distance >= Edge - static_cast<double>(StopMargin) - 1.0 && Distance <= Edge + 1.0)
	{
		return From;
	}
	FVector Snap = TargetBody - Flat * Edge;
	Snap.Z = From.Z;
	return ClampToArena(From, Snap);
}

void AJRPGBattlePawn::UpdateStrikeSpacing()
{
	const AJRPGBattlePawn* Foe = TargetPawn.Get();
	if (!bActionActive || !bApproachSettled || !Foe || Foe == this || Foe->IsHidden())
	{
		return;
	}
	const FVector Here = GetActorLocation();
	const FVector FoeBody = Foe->GetBodyLocation();
	const FVector ToFoe(FoeBody.X - Here.X, FoeBody.Y - Here.Y, 0.0);
	const double Distance = ToFoe.Size2D();
	if (Distance <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector Direction = ToFoe / Distance;
	// A banda de golpes re-mira o corpo do alvo a cada passada, segurando ou não um golpe (0x801E36F0, 0x801E3AD0)
	if (bFaceTargetEveryFrame)
	{
		SetActorRotation(FRotator(0.0, Direction.Rotation().Yaw, 0.0));
	}
	// O recuo dentro do alcance (0x80047C3C): enquanto a lei diz "dentro", volta pela direção a velocidade 4, na escala de animação
	// do ator (a câmera lenta reduz junto). Nunca passa da borda. Durante um deslize (o snap, um recuo) o deslize manda
	if (InRangeBackoffUnits <= 0.0f || bSliding)
	{
		return;
	}
	const double Edge = static_cast<double>(ReachFor(Foe) + StopMargin);
	if (Distance >= Edge)
	{
		return;
	}
	const int32 Speed = Action.Steps.IsValidIndex(PlayedStep) ? Action.Steps[PlayedStep].GameSpeed : UBattleFormulas::NormalGameSpeed;
	const double PerFrame = static_cast<double>(InRangeBackoffUnits) * UnitsToCm * static_cast<double>(Speed)
		/ static_cast<double>(UBattleFormulas::NormalGameSpeed);
	const double Move = FMath::Min(PerFrame, Edge - Distance);
	if (Move <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector To = ClampToArena(Here, Here - Direction * Move);
	TranslateBy(To - Here);
}

void AJRPGBattlePawn::ReactToHit(const AJRPGBattlePawn* Attacker, int32 LogicFrame, bool bKnockdown, bool bBlocked)
{
	// Quem está na ação dele não é mexido por fora; sem atacante, não há para onde virar
	if (!Attacker || bActionActive)
	{
		return;
	}
	// O commit da reação vira o defensor do CORPO dele para a posição do atacante (0x801EECBC): os dois ficam frente a frente
	const FVector Here = GetBodyLocation();
	const FVector Toward = FVector(Attacker->GetActorLocation().X - Here.X, Attacker->GetActorLocation().Y - Here.Y, 0.0).GetSafeNormal();
	if (Toward.IsNearlyZero())
	{
		return;
	}
	// Vira para quem bateu (o golpe e o block): senão quem apanha de costas fica de costas
	SetActorRotation(FRotator(0.0f, Toward.Rotation().Yaw, 0.0f));

	// O recuo vem do clip da reação (a velocidade de raiz, o passo do fim: BeginClipMotion na reação que ShowEvent começou). O recuo
	// fixo abaixo é só para o clip sem dado de movimento
	if (bUseClipMotion && ActiveMotion.bValid)
	{
		return;
	}
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

void AJRPGBattlePawn::BeginAction(const FJRPGActionTimeline& Timeline, AJRPGBattlePawn* Target)
{
	Action = Timeline;
	bActionActive = true;
	PlayedStep = -1;
	StrikesPlayed = 0;
	TargetPawn = Target;
	LastLogicFrame = Timeline.StartFrame;
	// Uma reação, um idle pendente ou os sons da ação anterior não passam para esta
	ReactionQueue.Reset();
	ReactionEndFrame = INDEX_NONE;
	IdleHandoffFrame = INDEX_NONE;
	StepClipEndFrame = INDEX_NONE;
	HitResults.Reset();
	HeldSounds.Reset();
	// O contexto de som da ação anterior (a cauda dela) acaba aqui: o desta nasce
	bSoundContextActive = true;
	SoundContextEndFrame = INDEX_NONE;
	// Um recuo ou um deslize de antes acaba aqui: a ação parte de onde ele está (e o movimento do clip de antes também)
	bSliding = false;
	ActiveMotion = FClipMotion();
	MotionStartFrame = INDEX_NONE;
	MotionEndFrame = INDEX_NONE;

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
	const float WalkDistance = FVector::Dist2D(ApproachTo, ApproachFrom);
	const bool bWalks = Timeline.LeadFrames > 0 && WalkDistance > MinWalkDistance;
	ApproachStartFrame = Timeline.StartFrame;
	ApproachFrames = 0;
	ApproachStallFrames = 0;
	if (bWalks)
	{
		// Velocidade constante, como o original (anda até estar ao alcance): longe leva mais que os LeadFrames das regras, e a
		// diferença desloca os passos da ação (a cena segura o resolvedor o mesmo tanto); perto chega antes e espera no idle
		ApproachFrames = FMath::Max(FMath::CeilToInt(WalkDistance / WalkSpeedCm()), 1);
		ApproachStallFrames = FMath::Max(ApproachFrames - Timeline.LeadFrames, 0);
		Action.StartFrame += ApproachStallFrames;
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
		// Onde o clip de fato acaba, em quadros lógicos (o comprimento do asset no play rate dele): a ação compara com o fim dela
		// quando passa ao idle (FinishAction) e o log diz quando os dois não batem
		const float Speed = FMath::Max(Rate * FMath::Max(Clip->RateScale, KINDA_SMALL_NUMBER), KINDA_SMALL_NUMBER);
		const int32 ClipFrames = FMath::Max(FMath::CeilToInt(Clip->GetPlayLength() / Speed * static_cast<float>(UBattleFormulas::StepsPerSecond)), 1);
		StepClipName = Clip->GetFName();
		StepClipEndFrame = Action.StartFrame + Step.StartFrame + ClipFrames;
		// O clip do passo move o ator enquanto toca (o Slash Kick recua 7 por vsync) e anda o passo do fim dele (534 à frente)
		BeginClipMotion(StepMotion(Step), Action.StartFrame + Step.StartFrame, StepClipEndFrame, Step.GameSpeed);
	}
	else
	{
		StepClipName = NAME_None;
		StepClipEndFrame = INDEX_NONE;
		BeginClipMotion(FClipMotion(), Action.StartFrame + Step.StartFrame, INDEX_NONE, Step.GameSpeed);
	}
}

void AJRPGBattlePawn::UpdatePlayback(int32 LogicFrame)
{
	LastLogicFrame = LogicFrame;
	// A pose do idle, medida uma vez quando ele já toca sozinho (a mistura de entrada acabou): a referência do passo do fim do clip
	if (!bHasNeutralCentroid && IdleSinceFrame != INDEX_NONE && !bActionActive
		&& ++IdleFramesSeen > FMath::CeilToInt(AnimBlendTime * static_cast<float>(UBattleFormulas::StepsPerSecond)) + 4)
	{
		NeutralCentroid = PoseCentroid();
		bHasNeutralCentroid = !NeutralCentroid.IsZero();
	}
	// O recuo de quem apanhou e o acompanhar do atacante andam em quadros lógicos, ação ou não
	UpdateSlide(LogicFrame);
	// A velocidade de raiz do clip em curso (a reação que desliza, a investida que anda)
	UpdateRootMotion(LogicFrame);
	// A reação em curso anda em quadros lógicos (a pausa e os soluços do quadro valem para ela como para o resolvedor); um quadro
	// atrasado recupera a cadeia passo a passo
	while (ReactionEndFrame != INDEX_NONE && LogicFrame >= ReactionEndFrame)
	{
		OnReactionEnded(ReactionEndFrame);
	}
	UpdateDeathFade(LogicFrame);
	UpdateHeldSounds(LogicFrame);
	// A cauda da ação que acabou passou (o idle entrou, o crossfade acabou e o último golpe ficou para trás): o som não é mais dela
	if (bSoundContextActive && !bActionActive && SoundContextEndFrame != INDEX_NONE && LogicFrame > SoundContextEndFrame)
	{
		bSoundContextActive = false;
		HitResults.Reset();
	}
	// O idle que esperava o fim real do último clip da ação
	if (IdleHandoffFrame != INDEX_NONE && LogicFrame >= IdleHandoffFrame)
	{
		IdleHandoffFrame = INDEX_NONE;
		ReturnToIdle();
	}
	if (!bActionActive)
	{
		return;
	}
	const int32 Elapsed = LogicFrame - Action.StartFrame;

	// A aproximação: da posição de saída até o alcance do alvo, em velocidade constante (ApproachFrames); fechada a aproximação,
	// só os deslizes (acompanhar o alvo que recuou) mexem nele
	const int32 WalkElapsed = LogicFrame - ApproachStartFrame;
	if (!bApproachSettled && WalkElapsed < ApproachFrames)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(WalkElapsed) / static_cast<float>(ApproachFrames), 0.0f, 1.0f);
		SetActorLocation(FMath::Lerp(ApproachFrom, ApproachTo, Alpha));
	}
	else if (!bApproachSettled)
	{
		bApproachSettled = true;
		if (!bSliding && !GetActorLocation().Equals(ApproachTo, 1.0))
		{
			SetActorLocation(ApproachTo);
		}
		// Chegou: o alvo é empurrado para a borda do alcance (não ficam colados)
		ArrivalShove();
		// Chegou antes do primeiro golpe (o alvo estava perto): espera parado, sem o clip de andar no lugar
		if (PlayedStep < 0 && Action.Steps.Num() > 0 && Elapsed < Action.Steps[0].StartFrame)
		{
			PlayIdle();
			SetPhase(EJRPGActionPhase::Windup);
		}
	}

	// Entra no passo que começou (um por vez: um quadro lento recupera o atraso passo a passo)
	while (PlayedStep + 1 < Action.Steps.Num() && Elapsed >= Action.Steps[PlayedStep + 1].StartFrame)
	{
		// O clip anterior foi cortado pelo próximo (o corte do meio do clip): o original anda a parte tocada do passo do fim dele
		// (0x80047950..0x80047A28); o reach snap logo abaixo decide onde o golpe seguinte começa
		if (PlayedStep >= 0)
		{
			SlideBy(EndStepShift(ClipMotionFraction(LogicFrame)), LogicFrame);
		}
		++PlayedStep;
		const FJRPGActionStep& Step = Action.Steps[PlayedStep];
		// A cada clip que entra, vira e vai até a borda do alcance (o reach snap)
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

	// Depois da caminhada: mira o corpo do alvo e não deixa o atacante dentro dele
	UpdateStrikeSpacing();

	if (Elapsed >= Action.TotalFrames)
	{
		FinishAction(LogicFrame);
	}
	else if (PlayedStep >= 0 && PlayedStep == Action.Steps.Num() - 1 && Elapsed >= Action.Steps[PlayedStep].EndFrame)
	{
		SetPhase(EJRPGActionPhase::Recovery);
	}
}

void AJRPGBattlePawn::FinishAction(int32 LogicFrame)
{
	// A ação acabou: fica onde está (sem voltar ao lugar) e passa ao idle
	bActionActive = false;
	SetPhase(EJRPGActionPhase::Done);
	// Os sons que ainda esperavam o resultado de um golpe nunca o receberam (o golpe não foi resolvido): descartados
	HeldSounds.Reset();

	bool bIdleNow = true;
	if (!bDown && StepClipEndFrame != INDEX_NONE)
	{
		// O original nunca corta o último clip: a ação só acaba depois dele (o estado 0x20 espera o clip voltar ao idle). Se o fim da
		// ação (os dados) e o fim real do clip (o asset) não batem, o log diz por quanto: era assim que o idle forçado cortava o
		// clip, ou deixava o último quadro parado
		const int32 Remaining = StepClipEndFrame - LogicFrame;
		if (FMath::Abs(Remaining) > 2)
		{
			UE_LOG(LogTemp, Log, TEXT("BattlePawn %s: a ação %d acabou no quadro %d, mas o clip %s %s por %d quadro(s) (fim real no quadro %d)%s"),
				*Combatant.Key.ToString(), Action.ActionIndex, LogicFrame, *StepClipName.ToString(),
				Remaining > 0 ? TEXT("ainda tocaria") : TEXT("já tinha acabado e segurava o último quadro"), FMath::Abs(Remaining), StepClipEndFrame,
				Remaining > 0 && Remaining <= MaxIdleHandoffWait ? TEXT("; o idle espera o clip acabar") : (Remaining > 0 ? TEXT("; o idle corta o clip") : TEXT("")));
		}
		if (Remaining > 2 && Remaining <= MaxIdleHandoffWait)
		{
			bIdleNow = false;
			IdleHandoffFrame = StepClipEndFrame;
		}
	}
	if (bIdleNow && !bDown)
	{
		IdleHandoffFrame = INDEX_NONE;
		SettleIntoIdle();
	}

	// O contexto de som sobrevive ao fim da ação enquanto o último clip ainda dispara notifies: até o último golpe do passo mais a
	// janela depois dele, até o idle entrar (a espera do idle) e mais o crossfade em que o clip que sai segue tocando
	int32 TailEnd = LogicFrame;
	for (const FJRPGActionHit& Hit : Action.Hits)
	{
		if (Hit.StepIndex == PlayedStep)
		{
			TailEnd = FMath::Max(TailEnd, Action.StartFrame + Hit.Frame + FMath::Max(HitSoundLagFrames, 1));
		}
	}
	const int32 IdleStartFrame = IdleHandoffFrame != INDEX_NONE ? IdleHandoffFrame : LogicFrame;
	const int32 CrossfadeFrames = FMath::CeilToInt(AnimBlendTime * static_cast<float>(UBattleFormulas::StepsPerSecond));
	SoundContextEndFrame = FMath::Max(TailEnd, IdleStartFrame + CrossfadeFrames);
	SetPhase(EJRPGActionPhase::Idle);
}

void AJRPGBattlePawn::SyncActionTimeline(const FJRPGActionTimeline& Current)
{
	if (!bActionActive || Current.ActionIndex != Action.ActionIndex
		|| (Current.TotalFrames == Action.TotalFrames && Current.Steps.Num() == Action.Steps.Num()))
	{
		return;
	}
	// A cópia do ActionStart ficou velha: o resolvedor cortou o resto (nenhum passo novo começa depois do corte). O StartFrame fica
	// o da cópia do pawn: está no relógio de apresentação, o da timeline das regras não
	const int32 PresentationStart = Action.StartFrame;
	Action = Current;
	Action.StartFrame = PresentationStart;
	PlayedStep = FMath::Min(PlayedStep, Action.Steps.Num() - 1);
}

void AJRPGBattlePawn::EndAction(int32 LogicFrame)
{
	LastLogicFrame = FMath::Max(LastLogicFrame, LogicFrame);
	if (bActionActive)
	{
		FinishAction(LogicFrame);
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
				StartReaction({ { RoleKnockdown, EReactionEnd::Idle }, { RoleGetUp, EReactionEnd::Idle } }, Event.Frame, Event.GameSpeed);
			}
			else
			{
				StartReaction({ { (Event.Flags & FJRPGBattleEventFlag::LowHit) ? RoleFlinch2 : RoleFlinch1, EReactionEnd::Idle } }, Event.Frame, Event.GameSpeed);
			}
			break;
		case EJRPGBattleEventType::Block:
			StartReaction({ { RoleBlock, EReactionEnd::Idle } }, Event.Frame, Event.GameSpeed);
			break;
		case EJRPGBattleEventType::Death:
			bDown = true;
			bActionActive = false;
			HeldSounds.Reset();
			bSoundContextActive = false;
			HitResults.Reset();
			if (Combatant.Type == EJRPGCombatantType::Enemy)
			{
				// O monstro cai, segura o último quadro do knockdown e então desbota e afunda (Event.Value é o fade, em quadros)
				bDeathFadeQueued = true;
				DeathFadeFrames = FMath::Max(Event.Value, 1);
				StartReaction({ { RoleKnockdown, EReactionEnd::Hold } }, Event.Frame, Event.GameSpeed);
			}
			else
			{
				// O herói cai (Knockdown), morre (Dying, a entrada 7) e fica no clip do caído (Down, a entrada 8) REPETINDO: no
				// original o caído continua animando, não segura um quadro (bug 17)
				StartReaction({ { RoleKnockdown, EReactionEnd::Idle }, { RoleDying, EReactionEnd::Hold }, { RoleDown, EReactionEnd::Loop } },
					Event.Frame, Event.GameSpeed);
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

// =================================================================================================
// Os sons de clip: o do impacto só toca se o golpe conectou
// =================================================================================================

void AJRPGBattlePawn::PlayNotifySound(USkeletalMeshComponent* MeshComp, USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier, bool bFollow, FName AttachName)
{
	// O mesmo que o UAnimNotify_PlaySound::Notify da engine faz (o som preso à malha ou ao bone, ou solto no ponto dela), para o
	// som que o pawn segurou tocar igual ao que tocaria sem a regra
	if (!Sound || !MeshComp)
	{
		return;
	}
	if (!Sound->IsOneShot())
	{
		UE_LOG(LogTemp, Warning, TEXT("BattlePawn: o som %s de um clip não é um one-shot — não toca (vazaria uma instância por notify)"), *GetNameSafe(Sound));
		return;
	}
	// Como no UE 5.8: só prende ao bone / à malha com bFollow ligado; sem ele o AttachName é ignorado e o som toca solto no ponto
	if (bFollow)
	{
		UGameplayStatics::SpawnSoundAttached(Sound, MeshComp, AttachName, FVector(ForceInit), EAttachLocation::SnapToTarget, false, VolumeMultiplier, PitchMultiplier);
	}
	else
	{
		UGameplayStatics::PlaySoundAtLocation(MeshComp->GetWorld(), Sound, MeshComp->GetComponentLocation(), VolumeMultiplier, PitchMultiplier);
	}
}

void AJRPGBattlePawn::NoteHitResult(const FJRPGBattleEvent& Event)
{
	if (Event.Type != EJRPGBattleEventType::Damage && Event.Type != EJRPGBattleEventType::Block && Event.Type != EJRPGBattleEventType::Miss)
	{
		return;
	}
	// O evento é de uma ação que este pawn não tem (a cena montou no meio, ou veio outra depois): nada espera por ele. A ação que
	// já fechou ainda vale enquanto a cauda do clip dela toca (bSoundContextActive)
	if (!bSoundContextActive || Event.ActionIndex != Action.ActionIndex)
	{
		return;
	}
	HitResults.Add(Event.HitIndex, Event.Type);
	for (int32 i = HeldSounds.Num() - 1; i >= 0; --i)
	{
		if (HeldSounds[i].HitIndex == Event.HitIndex)
		{
			const FJRPGBattleSoundSpec Spec = HeldSounds[i].Spec;
			HeldSounds.RemoveAt(i);
			ResolveSound(Spec, Event.Type);
		}
	}
}

void AJRPGBattlePawn::ResolveSound(const FJRPGBattleSoundSpec& Spec, EJRPGBattleEventType Result) const
{
	if (Result == EJRPGBattleEventType::Miss)
	{
		// O golpe errou (a altura do alvo): o impacto não toca; o som de quem erra, se houver (o do clip, senão o do pawn)
		USoundBase* Whiff = Spec.MissSound ? Spec.MissSound.Get() : MissSound.Get();
		PlayNotifySound(Mesh, Whiff, Spec.VolumeMultiplier, Spec.PitchMultiplier, Spec.bFollow, Spec.AttachName);
		return;
	}
	PlayNotifySound(Mesh, Spec.HitSound, Spec.VolumeMultiplier, Spec.PitchMultiplier, Spec.bFollow, Spec.AttachName);
}

int32 AJRPGBattlePawn::FindHitForSound(const FJRPGBattleSoundSpec& Spec) const
{
	// Só a ação em curso (ou a cauda dela) tem golpes, e só os do passo que toca agora (o som é do clip que toca)
	if (!bSoundContextActive || PlayedStep < 0)
	{
		return INDEX_NONE;
	}
	const int32 Elapsed = LastLogicFrame - Action.StartFrame;
	int32 Best = INDEX_NONE;
	int32 BestDistance = MAX_int32;
	for (int32 i = 0; i < Action.Hits.Num(); ++i)
	{
		if (Action.Hits[i].StepIndex != PlayedStep)
		{
			continue;
		}
		// Positivo = o som vem depois do quadro do golpe. O PlaySound da engine só é "o impacto" dentro da janela assimétrica dele
		// (um assobio pouco antes do golpe, o impacto uns quadros depois); o UJRPGAnimNotify_HitSound é sempre o do golpe mais próximo
		const int32 Delta = Elapsed - Action.Hits[i].Frame;
		if (!Spec.bExplicitHitSound && (Delta < -HitSoundLeadFrames || Delta > HitSoundLagFrames))
		{
			continue;
		}
		const int32 Distance = FMath::Abs(Delta);
		if (Distance < BestDistance)
		{
			Best = i;
			BestDistance = Distance;
		}
	}
	return Best;
}

void AJRPGBattlePawn::HandleClipSound(const FJRPGBattleSoundSpec& Spec)
{
	if (!Spec.HitSound && !Spec.MissSound)
	{
		return;
	}
	const bool bWindowOn = HitSoundLeadFrames > 0 || HitSoundLagFrames > 0;
	const int32 HitIndex = (bWindowOn || Spec.bExplicitHitSound) ? FindHitForSound(Spec) : INDEX_NONE;
	if (HitIndex == INDEX_NONE)
	{
		// Não é o som de nenhum golpe (a voz, o passo, uma reação, o idle): toca como o notify manda
		PlayNotifySound(Mesh, Spec.HitSound, Spec.VolumeMultiplier, Spec.PitchMultiplier, Spec.bFollow, Spec.AttachName);
		return;
	}
	if (const EJRPGBattleEventType* Result = HitResults.Find(HitIndex))
	{
		// O resultado do golpe já saiu (o notify chegou depois da lógica): decide na hora
		ResolveSound(Spec, *Result);
		return;
	}
	if (!bActionActive)
	{
		// A ação já fechou e este golpe nunca foi resolvido (o alvo já tinha caído): nada mais vai confirmar o impacto
		return;
	}
	// O notify chegou antes da lógica resolver o golpe: espera o resultado dele (NoteHitResult), até a janela depois do quadro dele
	FJRPGHeldBattleSound& Held = HeldSounds.AddDefaulted_GetRef();
	Held.Spec = Spec;
	Held.HitIndex = HitIndex;
	Held.ExpireFrame = Action.StartFrame + Action.Hits[HitIndex].Frame + FMath::Max(HitSoundLagFrames, 1);
}

void AJRPGBattlePawn::UpdateHeldSounds(int32 LogicFrame)
{
	// Um golpe que a lógica nunca resolveu (o alvo já tinha caído, a ação foi cortada) não confirma o impacto: o som é descartado
	for (int32 i = HeldSounds.Num() - 1; i >= 0; --i)
	{
		if (LogicFrame > HeldSounds[i].ExpireFrame)
		{
			HeldSounds.RemoveAt(i);
		}
	}
}
