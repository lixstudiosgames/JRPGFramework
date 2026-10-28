#include "Core/JRPGSplineArea.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Quantos pontos um trecho curvo do spline vira. */
	constexpr int32 SamplesPerCurvedSegment = 8;
}

void FJRPGSplineArea::Build(const USplineComponent* Spline)
{
	Points.Reset();
	Bounds = FBox2D(ForceInit);
	if (!Spline)
	{
		return;
	}

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	const int32 NumSegments = Spline->GetNumberOfSplineSegments();
	for (int32 Segment = 0; Segment < NumSegments; ++Segment)
	{
		const bool bStraight = Spline->GetSplinePointType(Segment) == ESplinePointType::Linear;
		const int32 Samples = bStraight ? 1 : SamplesPerCurvedSegment;
		for (int32 i = 0; i < Samples; ++i)
		{
			const FVector P = Spline->GetLocationAtSplineInputKey(Segment + static_cast<float>(i) / Samples, ESplineCoordinateSpace::World);
			Points.Add(FVector2D(P.X, P.Y));
		}
	}
	// Spline aberto: o último ponto não abre trecho; o polígono fecha sozinho até o primeiro
	if (!Spline->IsClosedLoop() && NumPoints > 0)
	{
		const FVector Last = Spline->GetLocationAtSplinePoint(NumPoints - 1, ESplineCoordinateSpace::World);
		Points.Add(FVector2D(Last.X, Last.Y));
	}

	for (const FVector2D& P : Points)
	{
		Bounds += P;
	}
}

bool FJRPGSplineArea::Contains(const FVector& Point) const
{
	if (!IsValid())
	{
		return false;
	}
	const FVector2D P(Point.X, Point.Y);
	if (!Bounds.IsInside(P))
	{
		return false;
	}

	// Par-ímpar: um raio para +X cruza a borda um número ímpar de vezes se o ponto está dentro
	bool bInside = false;
	for (int32 i = 0, j = Points.Num() - 1; i < Points.Num(); j = i++)
	{
		const FVector2D& A = Points[i];
		const FVector2D& B = Points[j];
		if ((A.Y > P.Y) != (B.Y > P.Y)
			&& P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

float FJRPGSplineArea::DistanceOutside(const FVector& Point) const
{
	if (!IsValid() || Contains(Point))
	{
		return 0.0f;
	}
	const FVector2D P(Point.X, Point.Y);
	double Best = TNumericLimits<double>::Max();
	for (int32 i = 0, j = Points.Num() - 1; i < Points.Num(); j = i++)
	{
		const FVector2D Closest = FMath::ClosestPointOnSegment2D(P, Points[j], Points[i]);
		Best = FMath::Min(Best, FVector2D::DistSquared(P, Closest));
	}
	return static_cast<float>(FMath::Sqrt(Best));
}

bool FJRPGSplineArea::RandomPoint(FVector2D& OutPoint, int32 Attempts) const
{
	if (!IsValid())
	{
		return false;
	}
	// Sorteia na caixa e descarta o que cai fora: serve para qualquer formato, côncavo ou não
	for (int32 i = 0; i < Attempts; ++i)
	{
		const FVector2D Candidate(FMath::FRandRange(Bounds.Min.X, Bounds.Max.X), FMath::FRandRange(Bounds.Min.Y, Bounds.Max.Y));
		if (Contains(FVector(Candidate.X, Candidate.Y, 0.0f)))
		{
			OutPoint = Candidate;
			return true;
		}
	}
	return false;
}

void FJRPGSplineArea::SnapSplineToGround(USplineComponent* Spline, float SearchDistance, const AActor* IgnoreActor)
{
	UWorld* World = Spline ? Spline->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	Spline->Modify();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGSplineAreaSnap), false, IgnoreActor);
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); ++i)
	{
		const FVector P = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, P + FVector(0.0f, 0.0f, SearchDistance), P - FVector(0.0f, 0.0f, SearchDistance), Objects, Params))
		{
			// Um pouco acima do chão, para a linha não sumir dentro do terreno
			Spline->SetLocationAtSplinePoint(i, FVector(Hit.ImpactPoint) + FVector(0.0f, 0.0f, 10.0f), ESplineCoordinateSpace::World, false);
		}
	}
	Spline->UpdateSpline();
}
