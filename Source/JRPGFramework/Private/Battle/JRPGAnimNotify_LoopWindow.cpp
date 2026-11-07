#include "Battle/JRPGAnimNotify_LoopWindow.h"

#include "Animation/AnimSequenceBase.h"

bool UJRPGAnimNotify_LoopWindow::FindLoopWindow(const UAnimSequenceBase* Clip, float& OutStart, float& OutEnd, int32& OutLoops)
{
	if (!Clip)
	{
		return false;
	}
	for (const FAnimNotifyEvent& Event : Clip->Notifies)
	{
		const UJRPGAnimNotify_LoopWindow* Window = Cast<UJRPGAnimNotify_LoopWindow>(Event.NotifyStateClass);
		if (!Window || Window->Loops <= 0)
		{
			continue;
		}
		OutStart = Event.GetTriggerTime();
		OutEnd = OutStart + Event.GetDuration();
		OutLoops = Window->Loops;
		return OutEnd > OutStart;
	}
	return false;
}
