#include "Field/JRPGInteractionComponent.h"
#include "Field/JRPGInteractable.h"
#include "GameFramework/Actor.h"

UJRPGInteractionComponent::UJRPGInteractionComponent()
{
	// Acompanhar o alvo para o prompt não precisa ser todo frame
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UJRPGInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateTarget();
}

bool UJRPGInteractionComponent::IsInteractable(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	if (Actor->Implements<UJRPGInteractable>())
	{
		return true;
	}
	for (const TSoftClassPtr<UInterface>& Legacy : LegacyInterfaces)
	{
		// Carrega na primeira vez; depois é só o ponteiro
		const UClass* InterfaceClass = Legacy.LoadSynchronous();
		if (InterfaceClass && Actor->GetClass()->ImplementsInterface(InterfaceClass))
		{
			return true;
		}
	}
	return false;
}

AActor* UJRPGInteractionComponent::FindBestTarget() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<AActor*> Overlapping;
	Owner->GetOverlappingActors(Overlapping);

	const FVector Location = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();

	// Menor nota vence: distância no chão + o peso de estar de lado/atrás (0 bem na frente,
	// FacingWeight de lado, 2x FacingWeight atrás)
	AActor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (AActor* Actor : Overlapping)
	{
		if (Actor == Owner || !IsInteractable(Actor))
		{
			continue;
		}
		const FVector ToActor = Actor->GetActorLocation() - Location;
		const float Distance = FVector2D(ToActor.X, ToActor.Y).Size();
		const float Facing = FVector::DotProduct(Forward, ToActor.GetSafeNormal2D());
		const float Score = Distance + (1.0f - Facing) * FacingWeight;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Actor;
		}
	}
	return Best;
}

void UJRPGInteractionComponent::UpdateTarget()
{
	AActor* NewTarget = FindBestTarget();
	if (NewTarget != CurrentTarget.Get())
	{
		CurrentTarget = NewTarget;
		OnTargetChanged.Broadcast(NewTarget);
	}
}

AActor* UJRPGInteractionComponent::TryInteract()
{
	UpdateTarget();
	AActor* Target = CurrentTarget.Get();
	if (!Target)
	{
		return nullptr;
	}

	if (Target->Implements<UJRPGInteractable>())
	{
		IJRPGInteractable::Execute_Interact(Target, GetOwner());
	}
	else if (UFunction* LegacyInteract = Target->FindFunction(TEXT("Interact")))
	{
		// Interface de Blueprint antiga: Interact sem parâmetros
		if (LegacyInteract->NumParms == 0)
		{
			Target->ProcessEvent(LegacyInteract, nullptr);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPGInteractionComponent: o Interact de '%s' tem parâmetros — "
				"a ponte das interfaces antigas só chama o Interact sem parâmetros."), *Target->GetName());
			return nullptr;
		}
	}

	OnInteracted.Broadcast(Target);
	return Target;
}
