#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "JRPGMistSubsystem.generated.h"

class AJRPGMistVolume;
class UJRPGMistDisturberComponent;
class UMaterialParameterCollection;
class UTexture2D;

/**
 * UJRPGMistSubsystem
 * Decide, a cada frame, quem mexe a névoa da JRPGMist e escreve isso na MPC_JRPGMist, e
 * pinta o rastro do jogador na textura do rastro.
 *
 * O shader (Shaders/JRPGMist.ush) tem 8 slots fixos — o custo por froxel não depende de
 * quantos UJRPGMistDisturberComponent existem no mapa. A divisão:
 *  - o pawn do jogador ocupa 1 slot com a posição atual (abre a névoa e gira em volta dele);
 *  - Slot7 leva o mapeamento da textura do rastro (origem e lado em uu), não um disturber;
 *  - os slots que sobram vão para os outros disturbers mais próximos da câmera.
 *
 * O rastro (bLeavesTrail no componente — jogador e NPCs) é pintado na CPU numa textura pequena
 * que acompanha o jogador: a cada frame, uma cápsula do ponto anterior até o atual de cada um —
 * o caminho exato, qualquer curva. Cada texel guarda também a config de quem o pintou (raio,
 * vida, alargamento, borda), então cada um fecha do seu jeito. Cada texel guarda a distância até o caminho, quando ele passou e a
 * força; o valor sai disso na hora: gaussiana que alarga com a idade, aberta por inteiro até
 * TrailHold da vida e fechando suave até zerar em TrailLifetime. A textura vai para todos os
 * volumes (TrailTex); o shader faz uma leitura dela.
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

	/** Slots para disturbers: o último (Slot7) leva o mapeamento da textura do rastro. */
	static constexpr int32 DisturberSlots = NumSlots - 1;

	void RegisterVolume(AJRPGMistVolume* Volume);
	void UnregisterVolume(AJRPGMistVolume* Volume);
	void RegisterDisturber(UJRPGMistDisturberComponent* Disturber);
	void UnregisterDisturber(UJRPGMistDisturberComponent* Disturber);

	/** Textura do rastro (criada com o primeiro volume). Os volumes passam para o material. */
	UTexture2D* GetTrailTexture() const { return TrailTexture; }

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual TStatId GetStatId() const override;

private:
	struct FSlot
	{
		FLinearColor Slot = FLinearColor::Transparent;   // X, Y, Raio, Força
		FLinearColor Dir = FLinearColor::Transparent;    // DirX, DirY, Giro, 0
	};

	AJRPGMistVolume* GetFirstVolume() const;

	/**
	 * Slot da posição atual do jogador (Player) + pinta o rastro de todos os disturbers com
	 * bLeavesTrail na textura, que acompanha o jogador (sem jogador, o primeiro que deixa rastro).
	 */
	void UpdateTrail(const UJRPGMistDisturberComponent* Player, float DeltaTime, TArray<FSlot>& OutSlots);

	/** Pinta o trecho andado desde o frame anterior por um disturber. */
	void PaintDisturberTrail(const UJRPGMistDisturberComponent& Disturber);

	void ResetTrail();

	/** Cria (ou recria, se a resolução mudou) a textura e o buffer do rastro. */
	void EnsureTrailTexture(int32 Resolution);

	/** Leva a área do rastro para o jogador, deslocando o que já foi pintado (só em texels inteiros). */
	void RecenterTrail(const FVector2D& Center, float AreaSize);

	/**
	 * Pinta a cápsula A-B (raio em uu, força como a do slot). Um texel só troca de passada se a nova
	 * abre mais que a antiga abre agora: passar de novo não acumula.
	 */
	void PaintTrail(const FVector2D& A, const FVector2D& B, const UJRPGMistDisturberComponent& Owner, float Strength);

	/** Quanto o texel abre agora (0..1), com a config de quem o pintou. */
	float TrailValue(int32 Index) const;

	/** Calcula os valores, expira o que fechou e manda para a GPU. */
	void UploadTrail();

	void DrawTrailDebug(const UWorld* World, float Z) const;
	void WriteSlots(const AJRPGMistVolume* Volume, const TArray<FSlot>& Slots);

	TArray<TWeakObjectPtr<AJRPGMistVolume>> Volumes;
	TArray<TWeakObjectPtr<UJRPGMistDisturberComponent>> Disturbers;

	/** Textura do rastro (G8, transient), Res x Res texels cobrindo TrailArea uu. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TrailTexture;

	/**
	 * Por texel: distância (uu) até o caminho, hora da passada (TrailClock; < 0 = vazio), força
	 * e a config do componente que pintou (cada um fecha do seu jeito, mesmo se ele sumir).
	 */
	struct FTrailTexel
	{
		float Dist = 0.0f;
		float Time = -1.0f;
		float Strength = 0.0f;
		float Radius = 1.0f;
		float Lifetime = 1.0f;
		float Widening = 0.0f;
		float Hardness = 0.0f;
		float Hold = 0.0f;
	};
	TArray<FTrailTexel> TrailTexels;
	TArray<FTrailTexel> TrailScratch;
	TArray<uint8> TrailPixels;
	float TrailClock = 0.0f;
	int32 TrailRes = 0;
	float TrailArea = 0.0f;
	/** Canto (texel 0,0) da área do rastro no mundo, sempre múltiplo do tamanho do texel. */
	FVector2D TrailOrigin = FVector2D::ZeroVector;
	bool bTrailOriginValid = false;
	bool bTrailHasContent = false;

	/** Posição do frame anterior de cada disturber que pinta (para pintar o trecho andado). */
	TMap<TWeakObjectPtr<const UJRPGMistDisturberComponent>, FVector2D> TrailLastPositions;
	/** Quem a área do rastro acompanha (o jogador, ou o primeiro que deixa rastro). */
	TWeakObjectPtr<const UJRPGMistDisturberComponent> TrailOwner;

	bool bWarnedMissingMPC = false;
};
