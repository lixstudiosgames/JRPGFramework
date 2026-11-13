#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Camera/CameraData.h"
#include "JRPGVFXTypes.generated.h"

class AActor;
class UCurveFloat;
class UMaterialInterface;
class UNiagaraSystem;
class UStaticMesh;
class UJRPGVFXRecipe;

/** O que uma camada da receita põe na cena. Valores novos SEMPRE no fim (serializado nas receitas). */
UENUM(BlueprintType)
enum class EJRPGVFXLayerType : uint8
{
	/** Um sistema Niagara (as partículas). As faixas escrevem nos user parameters dele. */
	Niagara,
	/** Uma malha estática com material próprio (a forma do efeito: funil, anel, casca). As faixas escrevem no material. */
	Mesh,
	/** Uma point light. As faixas "Intensity" e "Radius" multiplicam os valores da camada. */
	Light,
	/** Nada novo na cena: as faixas escrevem nos materiais do corpo do ator âncora (tint do golpe, dissolve, aura). */
	BodyParam,
	/** Um shake de câmera (UCameraSubsystem), disparado uma vez no StartTime. */
	CameraShake
};

/** Em quem a camada se apoia. */
UENUM(BlueprintType)
enum class EJRPGVFXAnchor : uint8
{
	/** O alvo do efeito (FJRPGVFXContext::Target). */
	Target,
	/** Quem causou o efeito (FJRPGVFXContext::Instigator). */
	Instigator,
	/** O ponto do mundo em FJRPGVFXContext::Location. */
	Location
};

/** Onde, no ator âncora, a camada nasce (sem Socket). */
UENUM(BlueprintType)
enum class EJRPGVFXPlacement : uint8
{
	/** A posição do ator. */
	Pivot,
	/** O centro do volume do ator. */
	BoundsCenter,
	/** O centro da base do volume do ator (o chão debaixo dele). */
	BoundsBottom
};

/** Como uma faixa vai do From ao To ao longo da camada. */
UENUM(BlueprintType)
enum class EJRPGVFXEase : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	/** Vai do From ao To no meio da camada e volta ao From no fim (um pulso). */
	Pulse
};

/**
 * Um parâmetro escalar animado ao longo da camada. Para onde vai depende do tipo dela: user parameter (Niagara), parâmetro
 * de material (Mesh, BodyParam). Nomes reservados, que não vão a material nenhum:
 *   "Scale"     — multiplica a escala da camada (Niagara, Mesh, Light);
 *   "Intensity" — multiplica a intensidade (Light);
 *   "Radius"    — multiplica o raio de atenuação (Light).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGVFXScalarTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	float From = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	float To = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	EJRPGVFXEase Ease = EJRPGVFXEase::Linear;

	/** Opcional: a curva (tempo 0..1 da camada -> mistura 0..1 entre From e To) no lugar do Ease. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UCurveFloat> Curve;

	float Evaluate(float Alpha) const;
};

/** Um parâmetro de cor animado ao longo da camada (user parameter LinearColor no Niagara, vetor no material; na Light, "Color"). */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGVFXColorTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FLinearColor From = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FLinearColor To = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	EJRPGVFXEase Ease = EJRPGVFXEase::Linear;

	/** Multiplica pela cor do contexto (FJRPGVFXContext::Color): a mesma receita em vermelho para o Vahn, verde para a Noa. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	bool bTintWithContextColor = false;

	FLinearColor Evaluate(float Alpha, const FLinearColor& ContextColor) const;
};

/**
 * FJRPGVFXLayer
 * Uma camada de uma receita (UJRPGVFXRecipe): o que aparece, quando, apoiado em quem, e os parâmetros que se mexem enquanto
 * ela vive. Uma estrutura só para todos os tipos (os campos de cada tipo aparecem conforme o Type), de propósito: uma lista de
 * structs se edita no editor e pelo MCP sem subobjetos.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGVFXLayer
{
	GENERATED_BODY()

	/** Só para ler a receita ("Funil", "Brasas", "Luz"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FName Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EJRPGVFXLayerType Type = EJRPGVFXLayerType::Niagara;

	/** Quando a camada entra, em segundos desde o começo do efeito. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0"))
	float StartTime = 0.0f;

	/**
	 * Quanto ela dura, em segundos. 0 = até o fim da receita (numa receita em loop: até o StopEffect, com as faixas repetindo
	 * a cada volta). No fim, o Niagara para de emitir e as partículas vivas terminam sozinhas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0"))
	float Duration = 0.0f;

	// --- Onde ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake"))
	EJRPGVFXAnchor Anchor = EJRPGVFXAnchor::Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake && Type != EJRPGVFXLayerType::BodyParam"))
	EJRPGVFXPlacement Placement = EJRPGVFXPlacement::BoundsBottom;

	/** Socket ou bone do ator âncora; com ele o Placement não vale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake && Type != EJRPGVFXLayerType::BodyParam"))
	FName Socket;

	/** A camada segue o ator âncora (presa a ele). Desligado: fica onde nasceu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake && Type != EJRPGVFXLayerType::BodyParam"))
	bool bAttach = true;

	/** Posição, rotação e escala em relação ao ponto de apoio (o yaw acompanha o do ator âncora). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake && Type != EJRPGVFXLayerType::BodyParam"))
	FTransform Offset;

	/** Escala a camada pelo tamanho do ator âncora: raio do volume dele (em XY) / ReferenceSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake && Type != EJRPGVFXLayerType::BodyParam"))
	bool bScaleByAnchorSize = false;

	/** O raio (uu) do alvo para o qual a camada foi feita; um alvo com o dobro disto dobra a camada. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "bScaleByAnchorSize", ClampMin = "1.0"))
	float ReferenceSize = 50.0f;

	/** Um yaw aleatório por disparo (stream cosmético do subsistema), para duas explosões seguidas não saírem iguais. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake && Type != EJRPGVFXLayerType::BodyParam"))
	bool bRandomYaw = false;

	// --- Niagara ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara", meta = (EditCondition = "Type == EJRPGVFXLayerType::Niagara"))
	TObjectPtr<UNiagaraSystem> System;

	/**
	 * Opcional: o user parameter (Skeletal Mesh data interface) que recebe a malha do ator âncora, para partículas que nascem
	 * na superfície do corpo (o pó da morte, a absorção do Seru).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara", meta = (EditCondition = "Type == EJRPGVFXLayerType::Niagara"))
	FName SkeletalMeshParameter;

	// --- Mesh ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (EditCondition = "Type == EJRPGVFXLayerType::Mesh"))
	TObjectPtr<UStaticMesh> Mesh;

	/** O material em todos os slots da malha; vazio = os da própria malha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh", meta = (EditCondition = "Type == EJRPGVFXLayerType::Mesh"))
	TObjectPtr<UMaterialInterface> Material;

	// --- Light ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Type == EJRPGVFXLayerType::Light"))
	FLinearColor LightColor = FLinearColor::White;

	/** Intensidade sem unidade (a da point light da engine: 5000 é uma lâmpada forte de perto). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Type == EJRPGVFXLayerType::Light", ClampMin = "0.0"))
	float LightIntensity = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Type == EJRPGVFXLayerType::Light", ClampMin = "1.0"))
	float LightRadius = 600.0f;

	// --- CameraShake ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Shake", meta = (EditCondition = "Type == EJRPGVFXLayerType::CameraShake"))
	ECameraShakePreset ShakePreset = ECameraShakePreset::HitImpact;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Shake", meta = (EditCondition = "Type == EJRPGVFXLayerType::CameraShake", ClampMin = "0.0"))
	float ShakeScale = 1.0f;

	// --- Parâmetros animados ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tracks", meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake", TitleProperty = "Name"))
	TArray<FJRPGVFXScalarTrack> ScalarTracks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tracks", meta = (EditCondition = "Type != EJRPGVFXLayerType::CameraShake", TitleProperty = "Name"))
	TArray<FJRPGVFXColorTrack> ColorTracks;
};

/**
 * FJRPGVFXContext
 * Quem e onde, para um disparo de efeito. A receita diz o que cada camada usa (Anchor); uma âncora que falta cai no Location.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGVFXContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<AActor> Instigator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<AActor> Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FVector Location = FVector::ZeroVector;

	/** A cor do disparo (elemento, cor do herói): entra nas faixas com bTintWithContextColor e no user parameter "Color". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FLinearColor Color = FLinearColor::White;

	/** Escala o efeito inteiro. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	float Scale = 1.0f;
};

/** O recibo de um PlayEffect, para parar um efeito em loop. Id 0 = nenhum. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGVFXHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "VFX")
	int32 Id = 0;

	bool IsValid() const { return Id != 0; }
};

/**
 * FJRPGVFXRow
 * Linha da DataTable DT_VFX: RowName = o id do efeito ("HitFire", "TornadoFlame"...), usado por
 * UJRPGVFXSubsystem::PlayEffectById e pelo UJRPGAnimNotify_VFX. As receitas são assets do projeto.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGVFXRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VFX")
	TSoftObjectPtr<UJRPGVFXRecipe> Recipe;
};

namespace JRPGVFX
{
	/** A mistura 0..1 de um Ease no tempo 0..1 da camada. */
	JRPGFRAMEWORK_API float EvaluateEase(EJRPGVFXEase Ease, float Alpha);
}
