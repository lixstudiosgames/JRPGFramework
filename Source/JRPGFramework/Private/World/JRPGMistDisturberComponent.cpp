#include "World/JRPGMistDisturberComponent.h"
#include "World/JRPGMistSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UJRPGMistDisturberComponent::UJRPGMistDisturberComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UJRPGMistDisturberComponent::GetCurrentStrength(FVector& OutVelocity) const
{
	const AActor* Owner = GetOwner();
	OutVelocity = Owner ? Owner->GetVelocity() : FVector::ZeroVector;

	// Só o plano XY importa: a névoa é amostrada em XY e pular não deve abrir mais
	const float Speed = FVector2D(OutVelocity.X, OutVelocity.Y).Size();
	if (Speed < MinSpeed)
	{
		return IdleStrength;
	}
	const float Alpha = FMath::Clamp((Speed - MinSpeed) / FMath::Max(FullSpeed - MinSpeed, 1.0f), 0.0f, 1.0f);
	return FMath::Max(IdleStrength, Strength * Alpha);
}

void UJRPGMistDisturberComponent::BeginPlay()
{
	Super::BeginPlay();

	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (UJRPGMistSubsystem* Mist = GI ? GI->GetSubsystem<UJRPGMistSubsystem>() : nullptr)
	{
		Mist->RegisterDisturber(this);
	}
}

void UJRPGMistDisturberComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (UJRPGMistSubsystem* Mist = GI ? GI->GetSubsystem<UJRPGMistSubsystem>() : nullptr)
	{
		Mist->UnregisterDisturber(this);
	}

	Super::EndPlay(EndPlayReason);
}
