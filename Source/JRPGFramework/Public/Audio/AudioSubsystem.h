#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AudioSubsystem.generated.h"

class USoundBase;
class UAudioComponent;

/**
 * UAudioSubsystem
 * BGM com persistência entre mapas + configuração de volumes (Master/Music/SFX/Ambient).
 *
 * BGM:
 * - O BGM é escolhido no Level BP: no BeginPlay, cheque IsBGMPlaying() — se false,
 *   chame PlayBGM(BGM_DoMapa). Se true, o BGM persistente do mapa anterior continua.
 * - PlayBGM com bPersistAcrossMaps=true sobrevive a OpenLevel; com false, a engine
 *   destrói o som na troca de level automaticamente.
 * - Trocar de música com FadeInTime > 0 faz crossfade automático.
 *
 * Volumes:
 * - 4 canais (0..1, default 1): Master, Music, SFX, Ambient — persistidos em disco
 *   no slot "JRPGSettings" (separado dos saves da partida).
 * - Ligue os getters EFETIVOS (Master × canal) no pino Volume Multiplier dos seus
 *   PlaySound de Blueprint. Os sons da UI (menu/loja) já obedecem sozinhos.
 * - Mudar Master/Music ajusta o BGM tocando AO VIVO.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ============================================================
	// BGM
	// ============================================================

	/**
	 * Toca a música de fundo. Se a MESMA música já está tocando, ignora (não
	 * reinicia). Se OUTRA música está tocando, faz crossfade (FadeInTime > 0)
	 * ou corte seco (FadeInTime = 0).
	 * @param bPersistAcrossMaps  true = continua tocando ao trocar de mapa.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Audio")
	void PlayBGM(USoundBase* Sound, bool bPersistAcrossMaps = true, float FadeInTime = 0.0f);

	/** Para o BGM atual (com fade out opcional). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Audio")
	void StopBGM(float FadeOutTime = 0.0f);

	/** Há BGM tocando agora? (use no BeginPlay do level para decidir tocar o do mapa) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio")
	bool IsBGMPlaying() const;

	/** O BGM atual é persistente entre mapas? (use na checagem do teleporte) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio")
	bool IsBGMPersistent() const { return bBGMPersistent; }

	/** A música que está tocando (nullptr se nenhuma). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio")
	USoundBase* GetCurrentBGM() const;

	// ============================================================
	// VOLUMES (0..1; toda mudança grava o JRPGSettings em disco)
	// ============================================================

	UFUNCTION(BlueprintCallable, Category = "JRPG|Audio|Volume")
	void SetMasterVolume(float Volume);

	UFUNCTION(BlueprintCallable, Category = "JRPG|Audio|Volume")
	void SetMusicVolume(float Volume);

	UFUNCTION(BlueprintCallable, Category = "JRPG|Audio|Volume")
	void SetSFXVolume(float Volume);

	UFUNCTION(BlueprintCallable, Category = "JRPG|Audio|Volume")
	void SetAmbientVolume(float Volume);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio|Volume")
	float GetMasterVolume() const { return MasterVolume; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio|Volume")
	float GetMusicVolume() const { return MusicVolume; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio|Volume")
	float GetSFXVolume() const { return SFXVolume; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio|Volume")
	float GetAmbientVolume() const { return AmbientVolume; }

	// --- Volumes EFETIVOS (Master × canal) — ligar no Volume Multiplier do PlaySound ---

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio|Volume")
	float GetEffectiveMusicVolume() const { return MasterVolume * MusicVolume; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio|Volume")
	float GetEffectiveSFXVolume() const { return MasterVolume * SFXVolume; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Audio|Volume")
	float GetEffectiveAmbientVolume() const { return MasterVolume * AmbientVolume; }

private:
	/** Aplica o volume efetivo de música no BGM tocando (ao vivo). */
	void ApplyBGMVolume();

	/** Grava os volumes no slot "JRPGSettings". */
	void SaveSettings() const;

	/** Limpa a referência quando o BGM não-persistente morreu com o mapa antigo. */
	void HandlePostLoadMap(UWorld* NewWorld);

	/**
	 * Aposenta um componente de BGM: para (ou desvanece) E GARANTE que ele será
	 * destruído.
	 *
	 * Os componentes nascem com `bAutoDestroy = false` — é o que os faz
	 * sobreviver à troca de mapa. O preço é que parar um deles NÃO o destrói:
	 * quem larga a referência sem mais nada deixa um `UAudioComponent` parado
	 * para trás, um por troca de música. Passe SEMPRE por aqui em vez de chamar
	 * `Stop()`/`FadeOut()` direto.
	 */
	static void RetireBGMComponent(UAudioComponent* Component, float FadeOutTime);

	UPROPERTY()
	TObjectPtr<UAudioComponent> BGMComponent;

	UPROPERTY()
	TObjectPtr<USoundBase> CurrentBGM;

	bool bBGMPersistent = false;

	FDelegateHandle PostLoadMapHandle;

	// Volumes carregados do JRPGSettings (defaults 1.0)
	float MasterVolume = 1.0f;
	float MusicVolume = 1.0f;
	float SFXVolume = 1.0f;
	float AmbientVolume = 1.0f;
};
