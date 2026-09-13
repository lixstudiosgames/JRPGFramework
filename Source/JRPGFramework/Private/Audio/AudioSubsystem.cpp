#include "Audio/AudioSubsystem.h"
#include "Audio/JRPGSettingsSave.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

static const TCHAR* SettingsSlotName = TEXT("JRPGSettings");

void UAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Carrega os volumes salvos (defaults 1.0 se não existir/for incompatível)
	if (UJRPGSettingsSave* Settings = Cast<UJRPGSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(SettingsSlotName, 0)))
	{
		if (Settings->SettingsVersion <= UJRPGSettingsSave::CurrentSettingsVersion)
		{
			MasterVolume = FMath::Clamp(Settings->MasterVolume, 0.0f, 1.0f);
			MusicVolume = FMath::Clamp(Settings->MusicVolume, 0.0f, 1.0f);
			SFXVolume = FMath::Clamp(Settings->SFXVolume, 0.0f, 1.0f);
			AmbientVolume = FMath::Clamp(Settings->AmbientVolume, 0.0f, 1.0f);
			UE_LOG(LogTemp, Log, TEXT("AudioSubsystem: Configuração de áudio carregada (M %.2f | Mus %.2f | SFX %.2f | Amb %.2f)."),
				MasterVolume, MusicVolume, SFXVolume, AmbientVolume);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AudioSubsystem: JRPGSettings com versão incompatível — usando defaults (1.0)."));
		}
	}

	// BGM não-persistente morre com o mapa antigo — limpa a referência
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UAudioSubsystem::HandlePostLoadMap);
}

void UAudioSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	// Fim da sessão (PIE stop): para o BGM — componentes persistentes não devem
	// vazar som para fora da sessão de jogo
	StopBGM(0.0f);

	Super::Deinitialize();
}

// ============================================================
// BGM
// ============================================================

void UAudioSubsystem::PlayBGM(USoundBase* Sound, bool bPersistAcrossMaps, float FadeInTime)
{
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("AudioSubsystem: PlayBGM chamado sem som."));
		return;
	}

	// Mesma música já tocando: não reinicia
	if (IsBGMPlaying() && CurrentBGM == Sound)
	{
		return;
	}

	// Outra música tocando: crossfade (ou corte seco com FadeInTime = 0).
	// RetireBGMComponent, e não FadeOut direto: o componente antigo tem
	// bAutoDestroy = false e ficaria vivo para sempre depois que a linha
	// `BGMComponent = NewComponent` lá embaixo larga a referência.
	if (BGMComponent)
	{
		RetireBGMComponent(BGMComponent, FadeInTime);
		BGMComponent = nullptr;
	}

	UAudioComponent* NewComponent = UGameplayStatics::CreateSound2D(
		this, Sound,
		/*VolumeMultiplier=*/GetEffectiveMusicVolume(),
		/*PitchMultiplier=*/1.0f,
		/*StartTime=*/0.0f,
		/*ConcurrencySettings=*/nullptr,
		/*bPersistAcrossLevelTransition=*/bPersistAcrossMaps,
		/*bAutoDestroy=*/false);

	if (!NewComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("AudioSubsystem: Falha ao criar o componente de BGM para '%s'."), *Sound->GetName());
		return;
	}

	if (FadeInTime > 0.0f)
	{
		NewComponent->FadeIn(FadeInTime, 1.0f);
	}
	else
	{
		NewComponent->Play();
	}

	BGMComponent = NewComponent;
	CurrentBGM = Sound;
	bBGMPersistent = bPersistAcrossMaps;

	UE_LOG(LogTemp, Log, TEXT("AudioSubsystem: BGM '%s' tocando (persistente: %s, fade: %.1fs)."),
		*Sound->GetName(), bPersistAcrossMaps ? TEXT("sim") : TEXT("não"), FadeInTime);
}

void UAudioSubsystem::StopBGM(float FadeOutTime)
{
	RetireBGMComponent(BGMComponent, FadeOutTime);

	BGMComponent = nullptr;
	CurrentBGM = nullptr;
	bBGMPersistent = false;
}

void UAudioSubsystem::RetireBGMComponent(UAudioComponent* Component, float FadeOutTime)
{
	if (!Component)
	{
		return;
	}

	if (FadeOutTime > 0.0f && Component->IsPlaying())
	{
		// Ligar o bAutoDestroy AGORA: o componente precisava de false enquanto
		// era o BGM atual (para sobreviver à troca de mapa), mas a partir daqui
		// ele é descartável. Quando o fade terminar, ele se destrói sozinho —
		// e é a única forma de destruí-lo sem cortar o fade no meio.
		Component->bAutoDestroy = true;
		Component->FadeOut(FadeOutTime, 0.0f);
		return;
	}

	Component->Stop();
	Component->DestroyComponent();
}

bool UAudioSubsystem::IsBGMPlaying() const
{
	return BGMComponent != nullptr && BGMComponent->IsPlaying();
}

USoundBase* UAudioSubsystem::GetCurrentBGM() const
{
	return IsBGMPlaying() ? CurrentBGM.Get() : nullptr;
}

void UAudioSubsystem::HandlePostLoadMap(UWorld* NewWorld)
{
	if (!NewWorld || NewWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	// BGM não-persistente foi destruído pela engine junto com o mapa antigo —
	// limpa as referências para IsBGMPlaying() responder false no level novo
	if (BGMComponent && !BGMComponent->IsPlaying())
	{
		UE_LOG(LogTemp, Log, TEXT("AudioSubsystem: BGM não-persistente terminou com a troca de mapa."));
		BGMComponent = nullptr;
		CurrentBGM = nullptr;
		bBGMPersistent = false;
	}
}

// ============================================================
// VOLUMES
// ============================================================

void UAudioSubsystem::SetMasterVolume(float Volume)
{
	MasterVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	ApplyBGMVolume();
	SaveSettings();
}

void UAudioSubsystem::SetMusicVolume(float Volume)
{
	MusicVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	ApplyBGMVolume();
	SaveSettings();
}

void UAudioSubsystem::SetSFXVolume(float Volume)
{
	SFXVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	SaveSettings();
}

void UAudioSubsystem::SetAmbientVolume(float Volume)
{
	AmbientVolume = FMath::Clamp(Volume, 0.0f, 1.0f);
	SaveSettings();
}

void UAudioSubsystem::ApplyBGMVolume()
{
	if (BGMComponent && BGMComponent->IsPlaying())
	{
		BGMComponent->SetVolumeMultiplier(GetEffectiveMusicVolume());
	}
}

void UAudioSubsystem::SaveSettings() const
{
	UJRPGSettingsSave* Settings = Cast<UJRPGSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UJRPGSettingsSave::StaticClass()));
	if (!Settings)
	{
		return;
	}

	Settings->SettingsVersion = UJRPGSettingsSave::CurrentSettingsVersion;
	Settings->MasterVolume = MasterVolume;
	Settings->MusicVolume = MusicVolume;
	Settings->SFXVolume = SFXVolume;
	Settings->AmbientVolume = AmbientVolume;

	if (!UGameplayStatics::SaveGameToSlot(Settings, SettingsSlotName, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("AudioSubsystem: Falha ao gravar o JRPGSettings em disco."));
	}
}
