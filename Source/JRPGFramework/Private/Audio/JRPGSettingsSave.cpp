#include "Audio/JRPGSettingsSave.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* UJRPGSettingsSave::SlotName = TEXT("JRPGSettings");

UJRPGSettingsSave* UJRPGSettingsSave::LoadOrCreate()
{
	if (UJRPGSettingsSave* Loaded = Cast<UJRPGSettingsSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
	{
		if (Loaded->SettingsVersion <= CurrentSettingsVersion)
		{
			return Loaded;
		}
	}
	return Cast<UJRPGSettingsSave>(UGameplayStatics::CreateSaveGameObject(UJRPGSettingsSave::StaticClass()));
}

bool UJRPGSettingsSave::Store(UJRPGSettingsSave* Settings)
{
	if (!Settings)
	{
		return false;
	}
	Settings->SettingsVersion = CurrentSettingsVersion;
	return UGameplayStatics::SaveGameToSlot(Settings, SlotName, 0);
}
