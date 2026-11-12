#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "Styling/SlateBrush.h"
#include "JRPGBattleIntroEffect.h"
#include "JRPGBattleTransition.generated.h"

class FSlateWindowElementList;
class SWidget;
class UGameViewportClient;
class UTexture2D;
struct FGeometry;

/** Os tempos da transição (da arena: AJRPGBattleArena::TransitionPlaybackRate / HoldSeconds / RevealSeconds). */
struct FJRPGBattleTransitionTiming
{
	/** A velocidade do efeito (1 = os 60 quadros por segundo do port). */
	float PlaybackRate = 1.0f;
	/** Quanto a cor final fica cheia depois do efeito, com a luta já montada embaixo. */
	float HoldSeconds = 0.6f;
	/** O fade da cor final para a luta. */
	float RevealSeconds = 0.5f;
};

/**
 * UJRPGBattleTransition
 * A transição Field -> luta na tela (BATTLE_RE_SWEEP.md 10.3, passo 5b): captura o último quadro do viewport (o original copia o
 * framebuffer mostrado), põe por cima de tudo um overlay Slate que desenha um FJRPGBattleIntroEffect a 60 quadros por segundo e,
 * quando o estilo acaba (a tela inteira na cor do fade), avisa quem chamou (a luta monta debaixo dele). Depois segura a cor final
 * por HoldSeconds (o original carrega a luta do CD nessa hora), avisa de novo (a luta aparece) e some num fade de RevealSeconds.
 *
 * Desenho: uma chamada de triângulos com a UV afim (a interpolação do PS1), a captura como textura e o modo pré-multiplicado do
 * Slate (One, InvSrcAlpha), que dá os três blends do original pelo alpha do vértice: 1 opaco, 0 aditivo (o ABR 1), 0,5 a meia
 * mistura. O fade preto subtrativo (ABR 2) vira um escurecer pelo alpha. A captura leva umas linhas brancas embaixo (o texel das
 * primitivas sem textura). A cortina desenha a imagem que a CPU montou.
 *
 * Dono: UJRPGBattleSceneSubsystem (uma por luta).
 */
UCLASS(Transient)
class UJRPGBattleTransition : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Começa: Style 0..4 (FJRPGBattleIntroEffect::EStyle), Sub, a escala de trabalho da cortina. Falso sem viewport ou sem a captura
	 * (quem chama cai no fade). OnFinished(bWhite): a tela está toda na cor final; monte a luta agora. OnReveal: a cor começa a abrir
	 * sobre a luta (a abertura da luta e o som do começo abaixando).
	 */
	bool Start(UWorld* World, int32 Style, int32 Sub, int32 CurtainScale, const FJRPGBattleTransitionTiming& InTiming, TFunction<void(bool)> OnFinished,
		TFunction<void()> OnReveal);

	/** Tira o overlay e para (a luta acabou antes, ou o mapa saiu). */
	void Stop();

	bool IsRunning() const { return bRunning; }

	/** Segundos até a tela terminar de abrir (o fim do efeito, a espera e o fade), contando do agora; 0 parada. */
	float GetSecondsUntilOpen() const;

	/** O overlay desenha (chamado pelo widget). */
	int32 Paint(const FGeometry& Geometry, FSlateWindowElementList& OutElements, int32 LayerId) const;

	virtual void BeginDestroy() override;

private:
	bool TickTransition(float DeltaSeconds);
	bool CaptureViewport(UGameViewportClient* Viewport, int32 CurtainScale);
	void UploadCurtain();
	static UTexture2D* MakeTexture(int32 Width, int32 Height, const TArray<FColor>& Pixels);

	UPROPERTY()
	TObjectPtr<UTexture2D> Capture;

	UPROPERTY()
	TObjectPtr<UTexture2D> CurtainTexture;

	FSlateBrush CaptureBrush;
	FSlateBrush CurtainBrush;
	TSharedPtr<SWidget> Overlay;
	TWeakObjectPtr<UGameViewportClient> ViewportClient;
	FTSTicker::FDelegateHandle TickHandle;
	TFunction<void(bool)> FinishedCallback;
	TFunction<void()> RevealCallback;
	FJRPGBattleTransitionTiming Timing;

	FJRPGBattleIntroEffect Effect;
	int32 TotalFrames = 132;
	double Accumulator = 0.0;
	bool bRunning = false;
	bool bFinished = false;
	bool bEndWhite = false;
	/** Segundos desde o fim do efeito (a espera e o fade que abre a luta). */
	float AfterSeconds = 0.0f;
	bool bRevealed = false;

	/** A captura: as linhas do quadro (o resto da página de 256 é preto) e a linha branca, em fração da textura. */
	float CaptureRowsV = 1.0f;
	float WhiteV = 1.0f;
};
