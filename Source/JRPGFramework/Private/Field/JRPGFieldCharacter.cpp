#include "Field/JRPGFieldCharacter.h"
#include "Field/JRPGFieldMapSettings.h"
#include "Field/JRPGInteractionComponent.h"
#include "Camera/CameraSubsystem.h"
#include "Camera/JRPGCameraZone.h"
#include "World/JRPGMistDisturberComponent.h"
#include "World/WorldStateSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	/** Zona encontrada até este tempo de vida do personagem aplica na hora: nasceu dentro dela
	 * (load de save, portal) e um blend mostraria a câmera vindo da base. */
	constexpr float SpawnSnapWindow = 2.0f;
}

AJRPGFieldCharacter::AJRPGFieldCharacter()
{
	// Tick só durante um blend de câmera (liga e desliga sozinho)
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	GetCapsuleComponent()->InitCapsuleSize(25.0f, 96.0f);

	// Quem gira o personagem é o movimento (vira para onde anda), nunca o controller: a câmera
	// é fixa no mundo e o controller só manda a direção
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->bUseControllerDesiredRotation = false;
	Move->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = 1000.0f;
	Move->BrakingDecelerationWalking = 2000.0f;
	Move->GroundFriction = 8.0f;

	// Câmera de cima. Rotação absoluta: o braço não gira junto com o personagem, e o pitch/yaw
	// são do mundo. Sem teste de colisão: num top-down o braço encolhendo atrás de telhados e
	// árvores faz a câmera pular. Os valores vêm da BaseCamera (ApplyCameraToComponents)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;

	FieldCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FieldCamera"));
	FieldCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FieldCamera->bUsePawnControlRotation = false;

	MistDisturber = CreateDefaultSubobject<UJRPGMistDisturberComponent>(TEXT("MistDisturber"));
	Interaction = CreateDefaultSubobject<UJRPGInteractionComponent>(TEXT("Interaction"));

	CurrentCamera = BaseCamera;
	ApplyCameraToComponents(BaseCamera);
}

void AJRPGFieldCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// O viewport do Blueprint e do level mostra a BaseCamera
	CurrentCamera = BaseCamera;
	ApplyCameraToComponents(BaseCamera);
}

void AJRPGFieldCharacter::BeginPlay()
{
	Super::BeginPlay();

	// O padrão deste mapa, se ele tiver um AJRPGFieldMapSettings
	if (const AJRPGFieldMapSettings* Map = AJRPGFieldMapSettings::Find(GetWorld()))
	{
		BaseCamera = Map->Camera;
		BaseControlMode = Map->ControlMode;
		BaseControlYaw = Map->ControlYaw;
		if (Map->bOverrideCanSprint)
		{
			bCanSprint = Map->bCanSprint;
		}
		if (Map->bOverrideWalkSpeed)
		{
			WalkSpeed = Map->WalkSpeed;
		}
		if (Map->bOverrideSprintSpeed)
		{
			SprintSpeed = Map->SprintSpeed;
		}
	}

	CurrentCamera = BaseCamera;
	ApplyCameraToComponents(CurrentCamera);
	ApplyMoveSpeed();

	if (bDeclareFieldOnBeginPlay)
	{
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
			{
				WorldState->SetIsInField(true);
			}
		}
	}
}

void AJRPGFieldCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (BlendDuration <= 0.0f)
	{
		SetActorTickEnabled(false);
		return;
	}

	BlendElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(BlendElapsed / BlendDuration, 0.0f, 1.0f);
	CurrentCamera = FJRPGFieldCameraSettings::Blend(BlendFrom, BlendTo, FMath::SmoothStep(0.0f, 1.0f, Alpha));
	ApplyCameraToComponents(CurrentCamera);

	if (Alpha >= 1.0f)
	{
		BlendDuration = 0.0f;
		SetActorTickEnabled(false);
	}
}

void AJRPGFieldCharacter::ApplyCameraToComponents(const FJRPGFieldCameraSettings& Settings)
{
	if (!CameraBoom || !FieldCamera)
	{
		return;
	}
	// Rotação absoluta: a relativa é a do mundo
	CameraBoom->SetRelativeRotation(FRotator(Settings.Pitch, Settings.Yaw, 0.0f));
	CameraBoom->TargetArmLength = Settings.ArmLength;
	CameraBoom->TargetOffset = Settings.FocusOffset;
	CameraBoom->bEnableCameraLag = Settings.LagSpeed > 0.0f;
	CameraBoom->CameraLagSpeed = Settings.LagSpeed;
	FieldCamera->SetFieldOfView(Settings.FOV);
}

void AJRPGFieldCharacter::SetBaseCamera(const FJRPGFieldCameraSettings& NewCamera, float BlendTime)
{
	BaseCamera = NewCamera;
	if (!FindTopZone())
	{
		AppliedZone = nullptr;
		RefreshView(BlendTime);
	}
}

void AJRPGFieldCharacter::SetBaseControl(EJRPGFieldControlMode NewMode, float NewControlYaw)
{
	BaseControlMode = NewMode;
	BaseControlYaw = NewControlYaw;
}

AJRPGCameraZone* AJRPGFieldCharacter::GetActiveCameraZone() const
{
	return FindTopZone();
}

float AJRPGFieldCharacter::GetControlReferenceYaw(float ScreenCameraYaw) const
{
	EJRPGFieldControlMode Mode = BaseControlMode;
	float FixedYaw = BaseControlYaw;
	if (const AJRPGCameraZone* Zone = FindTopZone())
	{
		if (Zone->bOverrideControl)
		{
			Mode = Zone->ControlMode;
			FixedYaw = Zone->ControlYaw;
		}
	}
	return Mode == EJRPGFieldControlMode::FixedYaw ? FixedYaw : ScreenCameraYaw;
}

AJRPGCameraZone* AJRPGFieldCharacter::FindTopZone() const
{
	// A de maior Priority; empatando, a última em que entrou (o fim da lista)
	AJRPGCameraZone* Top = nullptr;
	for (const TWeakObjectPtr<AJRPGCameraZone>& Weak : CameraZones)
	{
		AJRPGCameraZone* Zone = Weak.Get();
		if (Zone && (!Top || Zone->Priority >= Top->Priority))
		{
			Top = Zone;
		}
	}
	return Top;
}

void AJRPGFieldCharacter::EnterCameraZone(AJRPGCameraZone* Zone)
{
	if (!Zone)
	{
		return;
	}
	CameraZones.RemoveAll([Zone](const TWeakObjectPtr<AJRPGCameraZone>& Z) { return !Z.IsValid() || Z.Get() == Zone; });
	CameraZones.Add(Zone);

	const bool bJustSpawned = GetGameTimeSinceCreation() < SpawnSnapWindow;
	RefreshView(bJustSpawned ? 0.0f : Zone->BlendInTime);
}

void AJRPGFieldCharacter::ExitCameraZone(AJRPGCameraZone* Zone)
{
	const int32 Removed = CameraZones.RemoveAll([Zone](const TWeakObjectPtr<AJRPGCameraZone>& Z) { return !Z.IsValid() || Z.Get() == Zone; });
	if (Removed > 0 && Zone)
	{
		// Volta para o que continua valendo (outra zona ou a base) no tempo de saída desta
		RefreshView(Zone->FadeOutTime);
	}
}

void AJRPGFieldCharacter::RefreshView(float BlendTime)
{
	AJRPGCameraZone* Top = FindTopZone();
	const bool bChanged = Top != AppliedZone.Get() || !AppliedZone.IsValid();
	AppliedZone = Top;

	const UGameInstance* GI = GetGameInstance();
	UCameraSubsystem* CameraSubsystem = GI ? GI->GetSubsystem<UCameraSubsystem>() : nullptr;

	// Zona com câmera do level: a view vai para ela; o braço fica onde está
	if (Top && Top->Mode == EJRPGCameraZoneMode::LevelCamera && IsValid(Top->LevelCamera))
	{
		if ((bChanged || !bViewingLevelCamera) && CameraSubsystem)
		{
			CameraSubsystem->FocusOnLevelCamera(Top->LevelCamera, BlendTime, Top->bAutoRestoreOnUIClose, Top->FadeOutTime);
			bViewingLevelCamera = true;
		}
		return;
	}

	// Saiu de uma zona que não era a que valia: nada muda na tela
	if (!bChanged && !bViewingLevelCamera)
	{
		return;
	}

	// Câmera do personagem: a da zona (Player Camera) ou a base
	const FJRPGFieldCameraSettings& Target =
		(Top && Top->Mode == EJRPGCameraZoneMode::PlayerCamera) ? Top->PlayerCamera : BaseCamera;

	// Voltando de uma câmera do level: o braço já vai direto para o destino (ele não estava na
	// tela) e quem faz o blend é a troca de view
	bool bSnapArm = BlendTime <= 0.0f;
	if (bViewingLevelCamera)
	{
		bViewingLevelCamera = false;
		bSnapArm = true;
		if (CameraSubsystem)
		{
			CameraSubsystem->RestoreToPlayerCamera(BlendTime);
		}
		// Garantia: a view volta para este personagem mesmo se o subsistema achou que não
		// havia foco ativo (o Restore dele não faz nada nesse caso)
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC && PC->GetViewTarget() != this)
		{
			PC->SetViewTargetWithBlend(this, BlendTime, VTBlend_Cubic);
		}
	}

	if (bSnapArm)
	{
		BlendDuration = 0.0f;
		CurrentCamera = Target;
		ApplyCameraToComponents(CurrentCamera);
		SetActorTickEnabled(false);
		return;
	}

	// Blend do braço a partir de onde ele está agora (também no meio de outro blend)
	BlendFrom = CurrentCamera;
	BlendTo = Target;
	BlendDuration = BlendTime;
	BlendElapsed = 0.0f;
	SetActorTickEnabled(true);
}

void AJRPGFieldCharacter::SetSprinting(bool bNewSprinting)
{
	const bool bNow = bNewSprinting && bCanSprint;
	if (bNow == bSprinting)
	{
		return;
	}
	bSprinting = bNow;
	ApplyMoveSpeed();
	OnSprintChanged.Broadcast(bSprinting);
}

float AJRPGFieldCharacter::GetGroundSpeed() const
{
	const FVector Velocity = GetVelocity();
	return FVector2D(Velocity.X, Velocity.Y).Size();
}

void AJRPGFieldCharacter::ApplyMoveSpeed()
{
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

#if WITH_EDITOR
void AJRPGFieldCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// WalkSpeed é a fonte: o MaxWalkSpeed do CharacterMovement acompanha no Details
	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(AJRPGFieldCharacter, WalkSpeed))
	{
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	}
}
#endif
