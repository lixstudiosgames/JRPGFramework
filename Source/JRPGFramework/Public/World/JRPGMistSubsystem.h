#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "JRPGMistSubsystem.generated.h"

class AJRPGMistVolume;
class UJRPGMistDisturberComponent;
class UMaterialParameterCollection;

/**
 * UJRPGMistSubsystem
 * Decide, a cada frame, quem mexe a névoa da JRPGMist e escreve isso na MPC_JRPGMist.
 *
 * O shader (Shaders/JRPGMist.ush) tem 8 slots fixos — o custo por froxel não depende de
 * quantos UJRPGMistDisturberComponent existem no mapa. A divisão:
 *  - o pawn do jogador (se o componente dele tem bLeavesTrail) ocupa 1 slot com a posição
 *    atual + TrailSlots-1 pontos de rastro, que se fecham em TrailLifetime segundos;
 *  - os slots que sobram vão para os outros disturbers mais próximos da câmera.
 *
 * Só tica com algum AJRPGMistVolume no mundo. Volumes e disturbers se registram sozinhos
 * em BeginPlay/EndPlay; quando o último volume sai (troca de mapa), a MPC e o rastro são
 * zerados.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGMistSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Slots do shader. Tem que bater com JRPG_MIST_SLOTS em Shaders/JRPGMist.ush. */
	static constexpr int32 NumSlots = 8;

	/** Slots usados pelo dono do rastro (posição atual + pontos de rastro). */
	static constexpr int32 TrailSlots = 6;

	void RegisterVolume(AJRPGMistVolume* Volume);
	void UnregisterVolume(AJRPGMistVolume* Volume);
	void RegisterDisturber(UJRPGMistDisturberComponent* Disturber);
	void UnregisterDisturber(UJRPGMistDisturberComponent* Disturber);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual TStatId GetStatId() const override;

private:
	struct FTrailPoint
	{
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Direction = FVector2D::ZeroVector;
		float Age = 0.0f;
		float Strength = 0.0f;
	};

	struct FSlot
	{
		FLinearColor Slot = FLinearColor::Transparent;   // X, Y, Raio, Força
		FLinearColor Dir = FLinearColor::Transparent;    // DirX, DirY, Giro, 0
	};

	AJRPGMistVolume* GetFirstVolume() const;
	void UpdateTrail(const UJRPGMistDisturberComponent* Owner, float DeltaTime, TArray<FSlot>& OutSlots);
	void WriteSlots(const AJRPGMistVolume* Volume, const TArray<FSlot>& Slots);

	TArray<TWeakObjectPtr<AJRPGMistVolume>> Volumes;
	TArray<TWeakObjectPtr<UJRPGMistDisturberComponent>> Disturbers;

	/** Mais novo primeiro. No máximo TrailSlots - 1 pontos. */
	TArray<FTrailPoint> Trail;
	float TimeSinceTrailPush = 0.0f;
	TWeakObjectPtr<const UJRPGMistDisturberComponent> TrailOwner;

	bool bWarnedMissingMPC = false;
};
