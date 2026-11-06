#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "JRPGSettingsSave.generated.h"

/**
 * UJRPGSettingsSave
 * Configurações do jogo persistidas em disco no slot "JRPGSettings" (UserIndex 0),
 * SEPARADO dos 15 slots de save da partida. Hoje guarda os volumes de áudio;
 * a futura tela de opções acrescenta os demais campos aqui.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** Incrementar quando o layout mudar de forma incompatível. */
	static constexpr int32 CurrentSettingsVersion = 1;

	UPROPERTY()
	int32 SettingsVersion = CurrentSettingsVersion;

	// --- Volumes (0..1) ---

	UPROPERTY()
	float MasterVolume = 1.0f;

	UPROPERTY()
	float MusicVolume = 1.0f;

	UPROPERTY()
	float SFXVolume = 1.0f;

	UPROPERTY()
	float AmbientVolume = 1.0f;

	// --- Luta ---

	/** A opção Battle Camera (EJRPGBattleCameraOption: 0 Close, 1 Normal, 2 Far); Normal, como o original. */
	UPROPERTY()
	uint8 BattleCameraOption = 1;

	/**
	 * O save das configurações do slot "JRPGSettings", ou um novo (sem o slot, ou de versão incompatível). Quem grava muda só os
	 * campos dele e chama Store: o resto que estava salvo fica.
	 */
	static UJRPGSettingsSave* LoadOrCreate();

	/** Grava no slot "JRPGSettings". */
	static bool Store(UJRPGSettingsSave* Settings);

	/** O nome do slot. */
	static const TCHAR* SlotName;
};
