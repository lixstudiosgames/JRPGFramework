#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/JRPGSplineArea.h"
#include "World/RevivalTreeState.h"
#include "JRPGMistBlocker.generated.h"

class USphereComponent;
class UBoxComponent;
class USplineComponent;

/** Forma da área sem névoa (vista de cima: vale para a coluna inteira, chão e ar). */
UENUM(BlueprintType)
enum class EJRPGMistBlockerShape : uint8
{
	/** Um raio em volta do ator. */
	Circle,
	/** Uma caixa (gira com o ator em yaw). */
	Box,
	/** Um contorno desenhado à mão (spline fechado): qualquer formato, inclusive côncavo. */
	Spline
};

/** Como a névoa sai quando o blocker liga (e volta quando ele desliga). */
UENUM(BlueprintType)
enum class EJRPGMistBlockerTransition : uint8
{
	/** A névoa recua do centro da área para as bordas (e volta das bordas para o centro). */
	Recede,
	/** A área inteira afina junto. */
	Fade
};

/**
 * AJRPGMistBlocker
 * Uma área em que a névoa da JRPGMist não entra: depois de um evento da história (uma Genesis
 * Tree revivida, por exemplo) aquele pedaço do mapa fica limpo. Vale para todos os
 * AJRPGMistVolume que ele encosta, em todos os modos, e para a coluna inteira (chão e ar).
 *
 * Quando: sem condição nenhuma ele está sempre ligado. Com RequiredFlagID / DisableFlagID (como
 * a Encounter Zone) e/ou RevivalTreeID + RevivalTreeStage, liga quando TODAS as condições
 * postas valem. Ele confere a cada FlagPollSeconds; no meio do jogo a névoa sai com a transição
 * (TransitionTime); ao entrar no mapa (ou carregar um save) já começa no estado certo.
 *
 * Por dentro: cada volume pinta uma textura pequena (BlockTex, ~50 uu por texel) com a soma dos
 * blockers que o encostam; o shader faz uma leitura dela. A distância de cada texel até a borda
 * é calculada só quando a forma ou o volume mudam — a transição só refaz a conta barata.
 *
 * No editor (bPreviewInEditor) a área aparece limpa no viewport, como se as condições valessem.
 *
 * Guia: Docs/Guides/MIST.md.
 */
UCLASS(Blueprintable, BlueprintType)
class JRPGFRAMEWORK_API AJRPGMistBlocker : public AActor
{
	GENERATED_BODY()

public:
	AJRPGMistBlocker();

	// --- Área ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Area")
	EJRPGMistBlockerShape Shape = EJRPGMistBlockerShape::Circle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Area", meta = (ClampMin = "50.0", EditCondition = "Shape == EJRPGMistBlockerShape::Circle", EditConditionHides))
	float Radius = 1500.0f;

	/** Metade do tamanho da caixa em X e Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Area", meta = (EditCondition = "Shape == EJRPGMistBlockerShape::Box", EditConditionHides))
	FVector2D BoxExtent = FVector2D(1500.0f, 1500.0f);

	/**
	 * Largura em uu da borda, PARA DENTRO da forma: na linha a névoa ainda está inteira e some
	 * aos poucos até esta distância. A névoa nunca passa da forma.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Area", meta = (ClampMin = "1.0"))
	float EdgeSoftness = 400.0f;

	/** Quanto tira da névoa (0..1). 1 = nada de névoa; 0.5 = só afina. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Area", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 1.0f;

	/** Só desenho no editor (sem colisão). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mist Blocker")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mist Blocker")
	TObjectPtr<USphereComponent> AreaCircle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mist Blocker")
	TObjectPtr<UBoxComponent> AreaBox;

	/** Shape = Spline: o contorno da área. Só a forma vista de cima conta. Alt+arrastar um ponto cria outro. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mist Blocker")
	TObjectPtr<USplineComponent> AreaSpline;

	// --- Quando ---

	/** Event flag que LIGA o blocker (no valor bRequiredFlagValue). Vazio = não confere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Condition")
	FName RequiredFlagID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Condition")
	bool bRequiredFlagValue = true;

	/** Event flag que DESLIGA o blocker quando está ligada (a névoa volta). Vazio = não confere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Condition")
	FName DisableFlagID;

	/** Revival (Genesis) Tree que libera a área (UWorldStateSubsystem). Vazio = não confere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Condition")
	FName RevivalTreeID;

	/** Liga quando a árvore chega a este estágio ou passa dele. Árvore sem estado conta como Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Condition")
	ERevivalTreeStage RevivalTreeStage = ERevivalTreeStage::FullRestored;

	/** De quanto em quanto tempo (s) as condições são conferidas no jogo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Condition", meta = (ClampMin = "0.1"))
	float FlagPollSeconds = 1.0f;

	// --- Transição ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Transition")
	EJRPGMistBlockerTransition Transition = EJRPGMistBlockerTransition::Recede;

	/** Segundos para a névoa sair (ou voltar) quando as condições mudam no meio do jogo. 0 = na hora. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Transition", meta = (ClampMin = "0.0"))
	float TransitionTime = 6.0f;

	/** Só no editor: mostra a área limpa no viewport, como se as condições valessem. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mist Blocker|Editor")
	bool bPreviewInEditor = true;

	// --- Uso ---

	/** As condições valem agora (o alvo da transição)? */
	UFUNCTION(BlueprintPure, Category = "JRPG|Mist")
	bool ShouldBlock() const;

	/** O quanto a área já está limpa (0..1, com a transição). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Mist")
	float GetBlockProgress() const { return Progress; }

	/**
	 * Confere as condições agora, sem esperar o FlagPollSeconds (ex.: logo depois que a cena da
	 * árvore liga a flag). bInstant = pula a transição.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Mist")
	void RefreshState(bool bInstant = false);

	/** O quanto este blocker tira da névoa num texel a SignedDistance uu dentro da forma (< 0 = fora), agora. */
	float GetBlockAmount(float SignedDistance) const;

	/** Distância no plano até a borda: > 0 dentro, < 0 fora (só o sinal importa fora). */
	float GetSignedDistance(const FVector2D& Point) const;

	/** A caixa em XY que a forma ocupa (o blocker não tira névoa fora dela). */
	FBox2D GetAreaBounds() const;

	/** Tira alguma névoa agora (já começou a transição ou está ligado)? */
	bool IsAffectingMist() const { return Progress > 0.0f && Strength > 0.0f; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Recalcula a forma (spline, caixa, raio) e a maior distância até a borda. */
	void RebuildArea();

	/** Timer: confere as condições. */
	void PollState();

	/** Avisa os volumes que encostam (ou encostavam) nele. bShapeChanged = refazer as distâncias. */
	void NotifyVolumes(bool bShapeChanged, bool bRemoving = false);

	FJRPGSplineArea SplineArea;

	/** A maior distância até a borda dentro da forma (o "centro" do Recede). */
	float MaxInside = 1.0f;

	/** Estado da transição: Progress anda até Target (0 ou 1). */
	float Progress = 0.0f;
	float Target = 0.0f;

	/** Até quando (tempo do mundo) uma mudança de estado é aplicada sem transição (o save restaura depois do BeginPlay). */
	double SnapUntilTime = 0.0;

	FTimerHandle PollTimer;
};
