#include "Field/JRPGFieldPlayerController.h"
#include "Field/JRPGFieldCharacter.h"
#include "Field/JRPGInteractionComponent.h"
#include "UI/WebUISubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Math/RotationMatrix.h"

namespace
{
	/** Mapeia Key -> Action com modificadores opcionais (criados dentro do contexto). */
	void MapWithModifiers(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key,
		bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bSwizzle)
		{
			// YXZ: a tecla vira o eixo Y (frente/trás) da ação Axis2D
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}
}

AJRPGFieldPlayerController::AJRPGFieldPlayerController()
{
	// Field anda no teclado/controle: sem cursor nem clicar para andar
	bShowMouseCursor = false;
}

void AJRPGFieldPlayerController::EnsureDefaultInput()
{
	if (!MoveAction)
	{
		MoveAction = NewObject<UInputAction>(this, TEXT("IA_FieldMove_Default"));
		MoveAction->ValueType = EInputActionValueType::Axis2D;
	}
	if (!SprintAction)
	{
		SprintAction = NewObject<UInputAction>(this, TEXT("IA_FieldSprint_Default"));
		SprintAction->ValueType = EInputActionValueType::Boolean;
		SprintAction->bConsumeInput = false;   // divide o botão do controle com o Interact
	}
	if (!InteractAction)
	{
		InteractAction = NewObject<UInputAction>(this, TEXT("IA_FieldInteract_Default"));
		InteractAction->ValueType = EInputActionValueType::Boolean;
		InteractAction->bConsumeInput = false;
	}
	if (!MenuAction)
	{
		MenuAction = NewObject<UInputAction>(this, TEXT("IA_FieldMenu_Default"));
		MenuAction->ValueType = EInputActionValueType::Boolean;
	}
	if (MappingContext)
	{
		return;
	}

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Field_Default"));

	// Andar: W/S no eixo Y (frente), A/D no X (direita). Setas iguais. Analógico já é 2D
	MapWithModifiers(Context, MoveAction, EKeys::W, true, false);
	MapWithModifiers(Context, MoveAction, EKeys::S, true, true);
	MapWithModifiers(Context, MoveAction, EKeys::A, false, true);
	MapWithModifiers(Context, MoveAction, EKeys::D, false, false);
	MapWithModifiers(Context, MoveAction, EKeys::Up, true, false);
	MapWithModifiers(Context, MoveAction, EKeys::Down, true, true);
	MapWithModifiers(Context, MoveAction, EKeys::Left, false, true);
	MapWithModifiers(Context, MoveAction, EKeys::Right, false, false);
	{
		FEnhancedActionKeyMapping& Stick = Context->MapKey(MoveAction, EKeys::Gamepad_Left2D);
		Stick.Modifiers.Add(NewObject<UInputModifierDeadZone>(Context));
	}

	// Correr: Shift e o botão de baixo do controle (A no Xbox, X no PlayStation). No controle é
	// o mesmo de interagir: segurar corre, um toque novo interage
	Context->MapKey(SprintAction, EKeys::LeftShift);
	Context->MapKey(SprintAction, EKeys::Gamepad_FaceButton_Bottom);

	// Interagir: E e o mesmo botão de baixo do controle
	Context->MapKey(InteractAction, EKeys::E);
	Context->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Bottom);

	// Menu: Tab e o botão de cima do controle (Y no Xbox, Triângulo no PlayStation)
	Context->MapKey(MenuAction, EKeys::Tab);
	Context->MapKey(MenuAction, EKeys::Gamepad_FaceButton_Top);

	MappingContext = Context;
}

void AJRPGFieldPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	EnsureDefaultInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogTemp, Warning, TEXT("JRPGFieldPlayerController: o InputComponent não é Enhanced Input — "
			"confira DefaultInputComponentClass no DefaultInput.ini. O personagem não vai andar."));
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AJRPGFieldPlayerController::HandleMove);
	Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &AJRPGFieldPlayerController::HandleMoveCompleted);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &AJRPGFieldPlayerController::HandleSprintStarted);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &AJRPGFieldPlayerController::HandleSprintCompleted);
	// Started = só no aperto: segurar (correndo) não interage
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &AJRPGFieldPlayerController::HandleInteract);
	Input->BindAction(MenuAction, ETriggerEvent::Started, this, &AJRPGFieldPlayerController::HandleMenu);
}

void AJRPGFieldPlayerController::BeginPlay()
{
	Super::BeginPlay();

	EnsureDefaultInput();
	SetFieldInputEnabled(bFieldInputEnabled);
}

void AJRPGFieldPlayerController::SetFieldInputEnabled(bool bEnabled)
{
	bFieldInputEnabled = bEnabled;

	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Subsystem && MappingContext)
	{
		if (bEnabled)
		{
			if (!Subsystem->HasMappingContext(MappingContext))
			{
				Subsystem->AddMappingContext(MappingContext, MappingPriority);
			}
		}
		else
		{
			Subsystem->RemoveMappingContext(MappingContext);
		}
	}

	// Desligando no meio da corrida: o Completed do botão não chega mais
	if (!bEnabled)
	{
		bMoveHeld = false;
		if (AJRPGFieldCharacter* FieldCharacter = Cast<AJRPGFieldCharacter>(GetPawn()))
		{
			FieldCharacter->SetSprinting(false);
		}
	}
}

void AJRPGFieldPlayerController::HandleMove(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	const FVector2D Axis = Value.Get<FVector2D>();
	if (!ControlledPawn || Axis.IsNearlyZero())
	{
		return;
	}

	float ReferenceYaw = GetControlReferenceYaw();
	if (bHoldDirectionOnCameraChange)
	{
		if (!bMoveHeld)
		{
			bMoveHeld = true;
			HeldReferenceYaw = ReferenceYaw;
		}
		ReferenceYaw = HeldReferenceYaw;
	}
	const FRotator YawRotation(0.0f, ReferenceYaw, 0.0f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	ControlledPawn->AddMovementInput(Forward, Axis.Y);
	ControlledPawn->AddMovementInput(Right, Axis.X);
}

void AJRPGFieldPlayerController::HandleMoveCompleted(const FInputActionValue& Value)
{
	bMoveHeld = false;
}

float AJRPGFieldPlayerController::GetControlReferenceYaw() const
{
	// Só o yaw da câmera na tela: "frente" é para longe da câmera no chão
	const float ScreenYaw = PlayerCameraManager
		? PlayerCameraManager->GetCameraRotation().Yaw : GetControlRotation().Yaw;
	if (const AJRPGFieldCharacter* FieldCharacter = Cast<AJRPGFieldCharacter>(GetPawn()))
	{
		return FieldCharacter->GetControlReferenceYaw(ScreenYaw);
	}
	return ScreenYaw;
}

void AJRPGFieldPlayerController::HandleSprintStarted(const FInputActionValue& Value)
{
	if (AJRPGFieldCharacter* FieldCharacter = Cast<AJRPGFieldCharacter>(GetPawn()))
	{
		FieldCharacter->SetSprinting(true);
	}
}

void AJRPGFieldPlayerController::HandleInteract(const FInputActionValue& Value)
{
	const APawn* ControlledPawn = GetPawn();
	if (UJRPGInteractionComponent* Interaction = ControlledPawn
		? ControlledPawn->FindComponentByClass<UJRPGInteractionComponent>() : nullptr)
	{
		Interaction->TryInteract();
	}
}

void AJRPGFieldPlayerController::HandleMenu(const FInputActionValue& Value)
{
	OpenGameMenu();
}

void AJRPGFieldPlayerController::OpenGameMenu()
{
	const UGameInstance* GI = GetGameInstance();
	UWebUISubsystem* WebUI = GI ? GI->GetSubsystem<UWebUISubsystem>() : nullptr;
	if (!WebUI || WebUI->GetUIState() != EJRPGUIState::HudOnly)
	{
		return;   // outra tela aberta: o botão não faz nada
	}

	// Parado ao abrir: a corrida não fica presa ligada e o direcional volta a valer do zero
	bMoveHeld = false;
	if (AJRPGFieldCharacter* FieldCharacter = Cast<AJRPGFieldCharacter>(GetPawn()))
	{
		FieldCharacter->SetSprinting(false);
	}
	WebUI->OpenMenu();
}

void AJRPGFieldPlayerController::HandleSprintCompleted(const FInputActionValue& Value)
{
	if (AJRPGFieldCharacter* FieldCharacter = Cast<AJRPGFieldCharacter>(GetPawn()))
	{
		FieldCharacter->SetSprinting(false);
	}
}

void AJRPGFieldPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// A rotação de controle segue a direção do personagem: é ela que o save grava
	// (CoreSubsystem::GetPlayerSaveTransform) e o load aplica no ator
	if (const APawn* ControlledPawn = GetPawn())
	{
		SetControlRotation(FRotator(0.0f, ControlledPawn->GetActorRotation().Yaw, 0.0f));
	}
}
