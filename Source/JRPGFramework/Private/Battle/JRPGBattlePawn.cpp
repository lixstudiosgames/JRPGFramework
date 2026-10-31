#include "Battle/JRPGBattlePawn.h"
#include "Field/JRPGFieldEnemyAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

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
