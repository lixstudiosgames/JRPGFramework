#pragma once

#include "CoreMinimal.h"

class AActor;
class USplineComponent;

/**
 * FJRPGSplineArea
 * Uma área desenhada à mão no chão: um spline fechado visto de cima, como um polígono 2D
 * (côncavo ou não). Só o contorno no plano conta — a altura dos pontos é ignorada.
 *
 * O polígono fica em espaço de mundo: Build de novo se o dono se mexer (OnConstruction no
 * editor, BeginPlay no jogo). Quem usa: AJRPGEncounterZone (Shape = Spline) e
 * AJRPGBattleArena (a área de cada arena).
 */
struct JRPGFRAMEWORK_API FJRPGSplineArea
{
	/** Amostra o spline: trecho reto vira 1 ponto, trecho curvo vira vários. */
	void Build(const USplineComponent* Spline);

	/** Precisa de 3 pontos para ser uma área. */
	bool IsValid() const { return Points.Num() >= 3; }

	/** O ponto está dentro do contorno (no plano)? */
	bool Contains(const FVector& Point) const;

	/** 0 dentro; fora, a distância no plano até a borda mais próxima. */
	float DistanceOutside(const FVector& Point) const;

	/** Um ponto XY ao acaso dentro do contorno. False se não achou (área inválida ou fina demais). */
	bool RandomPoint(FVector2D& OutPoint, int32 Attempts = 32) const;

	/** O centro da caixa que envolve o contorno. */
	FVector2D GetCenter() const { return Bounds.GetCenter(); }

	/**
	 * Só no editor: desce (ou sobe) cada ponto do spline até o chão, para o contorno ficar
	 * visível em terreno irregular. Não muda a área — só o desenho.
	 */
	static void SnapSplineToGround(USplineComponent* Spline, float SearchDistance, const AActor* IgnoreActor);

	TArray<FVector2D> Points;
	FBox2D Bounds = FBox2D(ForceInit);
};
