#include "Battle/JRPGAnimNotify_HitSound.h"
#include "Battle/JRPGBattlePawn.h"
#include "Field/JRPGFieldEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundBase.h"

UJRPGAnimNotify_HitSound::UJRPGAnimNotify_HitSound()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 160, 90, 255);
#endif
}

void UJRPGAnimNotify_HitSound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	// Sem chamar o Super, como o Play Sound da engine: nenhum retorno ao Blueprint
	if (!MeshComp || (!HitSound && !MissSound))
	{
		return;
	}
	if (AJRPGBattlePawn* BattlePawn = Cast<AJRPGBattlePawn>(MeshComp->GetOwner()))
	{
		FJRPGBattleSoundSpec Spec;
		Spec.HitSound = HitSound;
		Spec.MissSound = MissSound;
		Spec.VolumeMultiplier = VolumeMultiplier;
		Spec.PitchMultiplier = PitchMultiplier;
		Spec.bFollow = bFollow;
		Spec.AttachName = AttachName;
		Spec.bExplicitHitSound = true;
		BattlePawn->HandleClipSound(Spec);
		return;
	}
	// No Field o inimigo toca o som dele (3D, raio curto, um por vez, e escondido não toca): o HitSound é o som do clip
	if (AJRPGFieldEnemy* FieldEnemy = Cast<AJRPGFieldEnemy>(MeshComp->GetOwner()))
	{
		FieldEnemy->PlayClipSound(HitSound, VolumeMultiplier, PitchMultiplier, bFollow, AttachName);
		return;
	}
	// Fora da luta (preview, outro dono): o som do impacto toca como o Play Sound tocaria
	AJRPGBattlePawn::PlayNotifySound(MeshComp, HitSound, VolumeMultiplier, PitchMultiplier, bFollow, AttachName);
}

FString UJRPGAnimNotify_HitSound::GetNotifyName_Implementation() const
{
	return HitSound ? FString::Printf(TEXT("Hit: %s"), *HitSound->GetName()) : FString(TEXT("Hit Sound"));
}
