#include "Camera/JRPGCameraZone.h"
#include "Camera/CameraSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

AJRPGCameraZone::AJRPGCameraZone()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetBoxExtent(FVector(200.0f, 200.0f, 120.0f));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	RootComponent = TriggerBox;
}

void AJRPGCameraZone::BeginPlay()
{
	Super::BeginPlay();

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AJRPGCameraZone::HandleBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &AJRPGCameraZone::HandleEndOverlap);

	if (!LevelCamera)
	{
		UE_LOG(LogTemp, Warning, TEXT("JRPGCameraZone '%s': LevelCamera não configurada — a zona não vai fazer nada."),
			*GetName());
	}

	// Load de save: o pawn é teleportado DEPOIS do BeginPlay (PostLoadMap + retry)
	// e o overlap do teleporte pode se perder. Sincroniza por polling nos
	// primeiros segundos do mapa — se o player "nasceu" dentro da zona, a câmera
	// dela liga sozinha (e sair da zona restaura normal, sem entrar/sair de novo)
	SyncChecksRemaining = 20; // ~5s a cada 0.25s
	GetWorldTimerManager().SetTimer(SyncTimerHandle, this, &AJRPGCameraZone::SyncPlayerOverlap, 0.25f, true);
}

void AJRPGCameraZone::HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!IsPlayerPawn(OtherActor))
	{
		return;
	}
	ActivateZoneCamera();
}

void AJRPGCameraZone::HandleEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!IsPlayerPawn(OtherActor))
	{
		return;
	}
	DeactivateZoneCamera();
}

void AJRPGCameraZone::ActivateZoneCamera()
{
	if (bZoneActive || !IsValid(LevelCamera))
	{
		return;
	}

	if (UCameraSubsystem* Camera = GetCameraSubsystem())
	{
		Camera->FocusOnLevelCamera(LevelCamera, BlendInTime, bAutoRestoreOnUIClose, FadeOutTime);
		bZoneActive = true;
	}
}

void AJRPGCameraZone::DeactivateZoneCamera()
{
	if (!bZoneActive)
	{
		return; // a zona nunca ligou — não rouba a câmera de mais ninguém
	}
	bZoneActive = false;

	// No-op se a câmera já voltou por outro caminho (bFocusActive false)
	if (UCameraSubsystem* Camera = GetCameraSubsystem())
	{
		Camera->RestoreToPlayerCamera(FadeOutTime);
	}
}

void AJRPGCameraZone::SyncPlayerOverlap()
{
	// Janela de sync esgotada — os overlaps normais assumem daqui pra frente
	if (--SyncChecksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(SyncTimerHandle);
	}

	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AActor* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return; // pawn ainda não existe/possuído — tenta no próximo tick do timer
	}

	const bool bPlayerInside = TriggerBox->IsOverlappingActor(Pawn);
	if (bPlayerInside && !bZoneActive)
	{
		ActivateZoneCamera();
	}
	else if (!bPlayerInside && bZoneActive)
	{
		DeactivateZoneCamera();
	}
}

bool AJRPGCameraZone::IsPlayerPawn(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC && Actor == PC->GetPawn();
}

UCameraSubsystem* AJRPGCameraZone::GetCameraSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UCameraSubsystem>() : nullptr;
}
