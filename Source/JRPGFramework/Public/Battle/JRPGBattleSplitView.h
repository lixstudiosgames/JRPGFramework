#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "JRPGBattleSplitView.generated.h"

class ACameraActor;
class APlayerController;
class ULocalPlayer;
class UCameraComponent;
class UWorld;

/**
 * A tela dividida em colunas (protótipo da vitória em tela dividida, AJRPGBattleArena::bVictorySplitScreen; nosso, não do original).
 *
 * Usa o split screen da própria engine: uma renderização com N vistas, não N renderizações. A vista 0 é a do jogador (a câmera da
 * luta, o PlayerController dele); cada vista a mais ganha um jogador local temporário, sem pawn e sem input próprio, com um
 * APlayerController puro (não o do jogo: o GameMode não faz login, então nada spawna) olhando para uma ACameraActor daqui.
 *
 * O layout: liga o split da engine (UGameMapsSettings::bUseSplitscreen, colunas verticais para 2, 3 e 4) só enquanto está aberto e
 * devolve os valores do projeto no Stop. A UI do WebUI está no viewport inteiro (AddToViewport), não na tela de um jogador: ela não
 * se divide.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGBattleSplitView : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Abre Count colunas (2..4; limitado ao MaxSplitscreenPlayers do viewport). MainLens: a lente da câmera da luta (a coluna 0), que
	 * as câmeras extras copiam; nula = a padrão. Falso (e nada muda) se já está aberta, se o jogo já tem mais de um jogador local, ou
	 * sem viewport.
	 *
	 * Enquanto está aberta, nenhuma coluna usa o SSGI (o pós-processo das câmeras força a GI dinâmica em None; a da luta volta no
	 * Stop): o SSGI da engine guarda um AO só para a tela inteira (o ensure "one AO texture per view" com mais de uma vista) e custa
	 * de novo em cada coluna. Nem o Distance Field AO do sky light (o show flag do viewport, devolvido no Stop): cada coluna nova
	 * monta o cache dele do zero, e eram ~450 ms de travada nos primeiros quadros da divisão (medido em PIE, 2026-11-11).
	 */
	bool Start(UWorld* InWorld, int32 Count, UCameraComponent* MainLens);

	/** Fecha: os jogadores temporários saem, as câmeras somem e o split do projeto volta ao que era. Seguro chamar fechada. */
	void Stop();

	bool IsActive() const { return bActive; }

	/** Quantas colunas estão na tela (1 fechada). */
	int32 GetViewCount() const { return bActive ? ExtraCameras.Num() + 1 : 1; }

	/** A câmera da coluna View (1..N-1). Nula na 0 (a câmera da luta é a do jogador) e fora do intervalo. */
	ACameraActor* GetExtraCamera(int32 View) const;

	/** O fade das colunas extras (a 0 é do PlayerCameraManager do jogador, que quem chama já faz). */
	void FadeExtraViews(float FromAlpha, float ToAlpha, float Duration, FLinearColor Color, bool bHoldWhenFinished);

private:
	UPROPERTY()
	TArray<TObjectPtr<ULocalPlayer>> Players;

	UPROPERTY()
	TArray<TObjectPtr<APlayerController>> Controllers;

	UPROPERTY()
	TArray<TObjectPtr<ACameraActor>> ExtraCameras;

	TWeakObjectPtr<UWorld> World;
	bool bActive = false;

	/** A lente da câmera da luta e o override de GI dela antes do Start (devolvidos no Stop). */
	TWeakObjectPtr<UCameraComponent> MainLensComp;
	bool bSavedMainGIOverride = false;
	uint8 SavedMainGIMethod = 0;

	// O split do projeto, guardado no Start e devolvido no Stop (o CDO dos settings vive a sessão do editor inteira)
	bool bSavedUseSplitscreen = false;
	uint8 SavedTwoLayout = 0;
	uint8 SavedThreeLayout = 0;
	uint8 SavedFourLayout = 0;
	bool bSavedForceDisable = false;
	/** O show flag do Distance Field AO do viewport antes do Start. */
	bool bSavedDistanceFieldAO = true;
};
