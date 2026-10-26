#include "Field/JRPGFieldEnemyAnimInstance.h"
#include "Field/JRPGFieldEnemy.h"
#include "Animation/AnimNotifies/AnimNotify_PlaySound.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSyncScope.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"

void UJRPGFieldEnemyAnimInstance::PlayClip(UAnimSequence* Clip, float PlayRate, float BlendTime)
{
	if (Clip != RequestedClip)
	{
		FadingClip = RequestedClip;
		RequestedClip = Clip;
	}
	RequestedRate = PlayRate;
	RequestedBlendTime = BlendTime;
}

FAnimInstanceProxy* UJRPGFieldEnemyAnimInstance::CreateAnimInstanceProxy()
{
	return new FJRPGFieldEnemyAnimProxy(this);
}

void UJRPGFieldEnemyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FJRPGFieldEnemyAnimProxy*>(InProxy);
}

bool UJRPGFieldEnemyAnimInstance::HandleNotify(const FAnimNotifyEvent& AnimNotifyEvent)
{
	const UAnimNotify_PlaySound* PlaySound = Cast<UAnimNotify_PlaySound>(AnimNotifyEvent.Notify);
	AJRPGFieldEnemy* Enemy = Cast<AJRPGFieldEnemy>(GetOwningActor());
	if (!PlaySound || !Enemy)
	{
		return Super::HandleNotify(AnimNotifyEvent);
	}
	Enemy->PlayClipSound(PlaySound->Sound, PlaySound->VolumeMultiplier, PlaySound->PitchMultiplier, PlaySound->bFollow, PlaySound->AttachName);
	return true;
}

// ---------------------------------------------------------------------------------------------

void FJRPGFieldEnemyAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	Current = FJRPGCrossfadeLayer();
	Previous = FJRPGCrossfadeLayer();
	BlendAlpha = 1.0f;

	Node.Proxy = this;
	FAnimationInitializeContext InitContext(this);
	Node.Initialize_AnyThread(InitContext);
}

void FJRPGFieldEnemyAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	// Thread do jogo: copia o pedido do AnimInstance para o proxy
	const UJRPGFieldEnemyAnimInstance* Instance = CastChecked<UJRPGFieldEnemyAnimInstance>(InAnimInstance);
	UAnimSequence* Wanted = Instance->GetRequestedClip();
	BlendTime = Instance->GetRequestedBlendTime();

	if (Wanted != Current.Sequence)
	{
		// O que estava tocando vira o clip que sai; o novo começa do início
		Previous = Current;
		Current = FJRPGCrossfadeLayer();
		Current.Sequence = Wanted;
		BlendAlpha = (Previous.Sequence && BlendTime > 0.0f) ? 0.0f : 1.0f;
	}
	Current.PlayRate = Instance->GetRequestedRate();

	if (BlendAlpha < 1.0f)
	{
		BlendAlpha = FMath::Min(1.0f, BlendAlpha + DeltaSeconds / FMath::Max(BlendTime, KINDA_SMALL_NUMBER));
	}
	if (BlendAlpha >= 1.0f)
	{
		Previous = FJRPGCrossfadeLayer();
	}
}

void FJRPGFieldEnemyAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	Node.Update_AnyThread(InContext);
}

bool FJRPGFieldEnemyAnimProxy::Evaluate(FPoseContext& Output)
{
	Node.Evaluate_AnyThread(Output);
	return true;
}

// ---------------------------------------------------------------------------------------------

void FAnimNode_JRPGCrossfade::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	if (!Proxy)
	{
		return;
	}
	UE::Anim::FAnimSyncGroupScope& SyncScope = Context.GetMessageChecked<UE::Anim::FAnimSyncGroupScope>();

	// Os dois clips avançam e disparam notifies pela engine, cada um com o seu peso
	auto AddLayer = [&SyncScope, &Context](FJRPGCrossfadeLayer& Layer, float Weight)
	{
		if (!Layer.Sequence || Weight <= ZERO_ANIMWEIGHT_THRESH)
		{
			return;
		}
		FAnimTickRecord Record(Layer.Sequence, true, Layer.PlayRate, false, Weight, /*inout*/ Layer.Time, Layer.Marker);
		Record.DeltaTimeRecord = &Layer.DeltaRecord;
#if ANIM_TRACE_ENABLED
		Record.SourceProxy = Context.AnimInstanceProxy;
#endif
		SyncScope.AddTickRecord(Record);
	};
	AddLayer(Proxy->Current, Proxy->BlendAlpha);
	AddLayer(Proxy->Previous, 1.0f - Proxy->BlendAlpha);
}

void FAnimNode_JRPGCrossfade::Evaluate_AnyThread(FPoseContext& Output)
{
	if (!Proxy || !Proxy->Current.Sequence)
	{
		Output.ResetToRefPose();
		return;
	}

	auto Extract = [](const FJRPGCrossfadeLayer& Layer, FPoseContext& Into)
	{
		FAnimationPoseData PoseData(Into);
		const FAnimExtractContext Extraction(static_cast<double>(Layer.Time), false, Layer.DeltaRecord, true);
		Layer.Sequence->GetAnimationPose(PoseData, Extraction);
	};

	if (!Proxy->Previous.Sequence || Proxy->BlendAlpha >= 1.0f)
	{
		Extract(Proxy->Current, Output);
		return;
	}

	FPoseContext CurrentPose(Output);
	FPoseContext PreviousPose(Output);
	Extract(Proxy->Current, CurrentPose);
	Extract(Proxy->Previous, PreviousPose);

	const FAnimationPoseData CurrentData(CurrentPose);
	const FAnimationPoseData PreviousData(PreviousPose);
	FAnimationPoseData OutData(Output);
	FAnimationRuntime::BlendTwoPosesTogether(CurrentData, PreviousData, Proxy->BlendAlpha, OutData);
}
