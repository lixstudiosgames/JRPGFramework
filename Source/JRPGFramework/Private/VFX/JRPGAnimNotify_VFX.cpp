#include "VFX/JRPGAnimNotify_VFX.h"
#include "VFX/JRPGVFXRecipe.h"
#include "VFX/JRPGVFXSubsystem.h"
#include "Battle/JRPGBattlePawn.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogJRPGVFXNotify, Log, All);

UJRPGAnimNotify_VFX::UJRPGAnimNotify_VFX()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(120, 200, 255, 255);
#endif
}

void UJRPGAnimNotify_VFX::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	// Sem chamar o Super, como os outros notifies de luta: nenhum retorno ao Blueprint
	UJRPGVFXSubsystem* VFX = MeshComp ? UJRPGVFXSubsystem::Get(MeshComp) : nullptr;
	if (!VFX)
	{
		return;
	}
	const UJRPGVFXRecipe* Effect = Recipe ? Recipe.Get() : UJRPGVFXSubsystem::FindRecipe(EffectId);
	if (!Effect)
	{
		return;
	}
	if (Effect->bLooping)
	{
		UE_LOG(LogJRPGVFXNotify, Warning, TEXT("Battle VFX: a receita '%s' é em loop e um notify não tem como pará-la; ignorada"), *Effect->GetName());
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	FJRPGVFXContext Context;
	Context.Instigator = Owner;
	Context.Target = Owner;
	if (bOnActionTarget)
	{
		const AJRPGBattlePawn* BattlePawn = Cast<AJRPGBattlePawn>(Owner);
		if (AJRPGBattlePawn* ActionTarget = BattlePawn ? BattlePawn->GetActionTarget() : nullptr)
		{
			Context.Target = ActionTarget;
		}
	}
	// Sem dono (um preview sem ator) o efeito sai na malha
	Context.Location = Owner ? Owner->GetActorLocation() : MeshComp->GetComponentLocation();
	Context.Color = Color;
	Context.Scale = Scale;
	VFX->PlayEffect(Effect, Context);
}

FString UJRPGAnimNotify_VFX::GetNotifyName_Implementation() const
{
	if (Recipe)
	{
		return FString::Printf(TEXT("VFX: %s"), *Recipe->GetName());
	}
	return EffectId.IsNone() ? FString(TEXT("VFX")) : FString::Printf(TEXT("VFX: %s"), *EffectId.ToString());
}
