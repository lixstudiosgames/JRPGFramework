#include "Battle/JRPGAnimNotify_ChainCut.h"
#include "Animation/AnimSequenceBase.h"

float UJRPGAnimNotify_ChainCut::FindCutTime(const UAnimSequenceBase* Clip)
{
	if (Clip)
	{
		for (const FAnimNotifyEvent& Event : Clip->Notifies)
		{
			if (Event.Notify && Event.Notify->IsA<UJRPGAnimNotify_ChainCut>())
			{
				return Event.GetTriggerTime();
			}
		}
	}
	return -1.0f;
}
