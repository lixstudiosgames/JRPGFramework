#include "Battle/JRPGAnimNotify_Hit.h"
#include "Animation/AnimSequenceBase.h"

TArray<float> UJRPGAnimNotify_Hit::FindHitTimes(const UAnimSequenceBase* Clip)
{
	TArray<float> Times;
	if (Clip)
	{
		for (const FAnimNotifyEvent& Event : Clip->Notifies)
		{
			if (Event.Notify && Event.Notify->IsA<UJRPGAnimNotify_Hit>())
			{
				Times.Add(Event.GetTriggerTime());
			}
		}
		Times.Sort();
	}
	return Times;
}
