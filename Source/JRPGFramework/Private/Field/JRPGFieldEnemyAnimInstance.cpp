#include "Field/JRPGFieldEnemyAnimInstance.h"
#include "Field/JRPGFieldEnemy.h"
#include "Battle/JRPGBattlePawn.h"
#include "Battle/JRPGAnimNotify_LoopWindow.h"
#include "Animation/AnimNotifies/AnimNotify_PlaySound.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSyncScope.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Sound/SoundBase.h"

void UJRPGFieldEnemyAnimInstance::PlayClip(UAnimSequence* Clip, float PlayRate, float BlendTime, bool bLoop)
{
	if (Clip != RequestedClip)
	{
		FadingClip = RequestedClip;
		RequestedClip = Clip;
	}
	RequestedRate = PlayRate;
	RequestedBlendTime = BlendTime;
	// Um clip de uma vez só pedido de novo recomeça (um flinch em cima de outro, o mesmo golpe duas vezes seguidas); o loop que o
	// Field pede a cada quadro com o mesmo clip não recomeça nada, só uma troca entre loop e uma vez
	if (!bLoop || bRequestedLooping != bLoop)
	{
		++RequestSerial;
	}
	bRequestedLooping = bLoop;
}

FAnimInstanceProxy* UJRPGFieldEnemyAnimInstance::CreateAnimInstanceProxy()
{
	return new FJRPGFieldEnemyAnimProxy(this);
}

void UJRPGFieldEnemyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FJRPGFieldEnemyAnimProxy*>(InProxy);
}

bool UJRPGFieldEnemyAnimInstance::GetCurrentClipCursor(const UAnimSequence* Clip, float& OutSeconds, int32& OutLoopsLeft, bool& bOutHasLoopWindow) const
{
	const FJRPGFieldEnemyAnimProxy& Proxy = GetProxyOnGameThread<FJRPGFieldEnemyAnimProxy>();
	if (!Clip || Proxy.Current.Sequence != Clip)
	{
		return false;
	}
	OutSeconds = Proxy.Current.Time;
	OutLoopsLeft = Proxy.Current.LoopsLeft;
	bOutHasLoopWindow = Proxy.Current.LoopEnd > Proxy.Current.LoopStart;
	return true;
}

bool UJRPGFieldEnemyAnimInstance::HandleNotify(const FAnimNotifyEvent& AnimNotifyEvent)
{
	const UAnimNotify_PlaySound* PlaySound = Cast<UAnimNotify_PlaySound>(AnimNotifyEvent.Notify);
	if (!PlaySound)
	{
		return Super::HandleNotify(AnimNotifyEvent);
	}
	AActor* Owner = GetOwningActor();
	if (AJRPGFieldEnemy* Enemy = Cast<AJRPGFieldEnemy>(Owner))
	{
		Enemy->PlayClipSound(PlaySound->Sound, PlaySound->VolumeMultiplier, PlaySound->PitchMultiplier, PlaySound->bFollow, PlaySound->AttachName);
		return true;
	}
	if (AJRPGBattlePawn* BattlePawn = Cast<AJRPGBattlePawn>(Owner))
	{
		// Na luta o som perto de um golpe espera o resultado dele: o impacto de um Miss não toca (o pawn decide)
		FJRPGBattleSoundSpec Spec;
		Spec.HitSound = PlaySound->Sound;
		Spec.VolumeMultiplier = PlaySound->VolumeMultiplier;
		Spec.PitchMultiplier = PlaySound->PitchMultiplier;
		Spec.bFollow = PlaySound->bFollow;
		Spec.AttachName = PlaySound->AttachName;
		BattlePawn->HandleClipSound(Spec);
		return true;
	}
	return Super::HandleNotify(AnimNotifyEvent);
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

	const int32 Serial = Instance->GetRequestSerial();
	if (Wanted != Current.Sequence)
	{
		// O que estava tocando vira o clip que sai; o novo começa do início
		Previous = Current;
		Current = FJRPGCrossfadeLayer();
		Current.Sequence = Wanted;
		Current.Serial = Serial;
		BlendAlpha = (Previous.Sequence && BlendTime > 0.0f) ? 0.0f : 1.0f;
	}
	else if (Serial != Current.Serial)
	{
		// O mesmo clip pedido de novo (um clip de uma vez só): recomeça do quadro 0, sem mistura
		Current = FJRPGCrossfadeLayer();
		Current.Sequence = Wanted;
		Current.Serial = Serial;
		Previous = FJRPGCrossfadeLayer();
		BlendAlpha = 1.0f;
	}
	Current.PlayRate = Instance->GetRequestedRate();
	Current.bLooping = Instance->GetRequestedLooping();

	// O trecho de laço (UJRPGAnimNotify_LoopWindow): lido quando o clip entra; o cursor que passou do fim do trecho volta o tamanho
	// dele e gasta uma volta (FUN_80047430: o vão é tirado do CURSOR, então o que passou do fim fica)
	if (Current.Sequence && Current.Time == 0.0f && Current.LoopsLeft == 0 && Current.LoopEnd <= 0.0f)
	{
		float Start = 0.0f;
		float End = 0.0f;
		int32 Loops = 0;
		if (UJRPGAnimNotify_LoopWindow::FindLoopWindow(Current.Sequence, Start, End, Loops))
		{
			Current.LoopStart = Start;
			Current.LoopEnd = End;
			Current.LoopsLeft = Loops;
		}
		else
		{
			Current.LoopEnd = -1.0f;   // lido: o clip não tem trecho
		}
	}
	if (Current.LoopsLeft > 0 && Current.LoopEnd > Current.LoopStart && Current.Time >= Current.LoopEnd - KINDA_SMALL_NUMBER)
	{
		Current.Time -= Current.LoopEnd - Current.LoopStart;
		if (Current.LoopsLeft != UJRPGAnimNotify_LoopWindow::Forever)
		{
			--Current.LoopsLeft;
		}
	}

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
		FAnimTickRecord Record(Layer.Sequence, Layer.bLooping, Layer.PlayRate, false, Weight, /*inout*/ Layer.Time, Layer.Marker);
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
		const FAnimExtractContext Extraction(static_cast<double>(Layer.Time), false, Layer.DeltaRecord, Layer.bLooping);
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
