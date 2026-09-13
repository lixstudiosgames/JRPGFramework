#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CameraData.generated.h"

/**
 * Tipo de um alvo registrado no CameraSubsystem — usado para filtrar
 * (GetCameraTargetsByType) e para o auto-detect por tag inferir a categoria.
 */
UENUM(BlueprintType)
enum class ECameraTargetType : uint8
{
	Player UMETA(DisplayName = "Player"),
	NPC    UMETA(DisplayName = "NPC"),
	Enemy  UMETA(DisplayName = "Enemy"),
	// Novos tipos SEMPRE no fim — os valores são serializados em Blueprints
	Prop   UMETA(DisplayName = "Prop")
};

/**
 * Presets de enquadramento prontos (defaults hardcoded em JRPGCameraPresets::GetBuiltIn).
 * Podem ser sobrescritos por uma DataTable de FCameraPresetRow cujo row name
 * seja o nome do enum (ex: "ShopFront") — ver UCameraSubsystem::SetCameraPresetTable.
 */
UENUM(BlueprintType)
enum class ECameraFramingPreset : uint8
{
	Default      UMETA(DisplayName = "Default"),
	CloseUp      UMETA(DisplayName = "Close Up"),
	Conversation UMETA(DisplayName = "Conversation"),
	OverShoulder UMETA(DisplayName = "Over Shoulder"),
	ShopFront    UMETA(DisplayName = "Shop Front"),
	ThreeQuarter UMETA(DisplayName = "Three Quarter"),
	TopDown      UMETA(DisplayName = "Top Down"),
	// Novos presets SEMPRE no fim — os valores são serializados em Blueprints
	Wide         UMETA(DisplayName = "Wide"),
	BattleWide   UMETA(DisplayName = "Battle Wide"),
	Side45Left   UMETA(DisplayName = "Side 45 Left"),
	Side45Right  UMETA(DisplayName = "Side 45 Right"),
	PortraitLeft  UMETA(DisplayName = "Portrait Left (DOF)"),
	PortraitRight UMETA(DisplayName = "Portrait Right (DOF)")
};

/**
 * Variações de camera shake prontas (defaults em JRPGCameraPresets::GetShakePreset).
 * Custom é usado apenas nos broadcasts de shakes disparados via params customizados.
 */
UENUM(BlueprintType)
enum class ECameraShakePreset : uint8
{
	Light      UMETA(DisplayName = "Light"),
	Medium     UMETA(DisplayName = "Medium"),
	Heavy      UMETA(DisplayName = "Heavy"),
	Explosion  UMETA(DisplayName = "Explosion"),
	Earthquake UMETA(DisplayName = "Earthquake"),
	HitImpact  UMETA(DisplayName = "Hit Impact"),
	Rumble     UMETA(DisplayName = "Rumble (loop)"),
	// Novos presets SEMPRE no fim — os valores são serializados em Blueprints
	Handheld   UMETA(DisplayName = "Handheld (loop)"),
	Custom     UMETA(DisplayName = "Custom")
};

/**
 * FCameraPresetRow
 * Um enquadramento de câmera relativo a um alvo. Serve tanto de linha da
 * DataTable de presets (row name = nome do preset) quanto de struct avulsa
 * para FocusWithCustomParams.
 *
 * Matemática aplicada pelo AJRPGCameraActor:
 *   FocusPoint = TargetTransform.TransformPosition(FocusOffset)
 *   CamRot     = FRotator(-PitchDeg, TargetYaw + YawDeg (+ órbita), 0)
 *   CamPos     = FocusPoint - CamRot.Vector() * Distance
 *   AimRot     = olhar para (FocusPoint + LookAtOffset em espaço do alvo)
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FCameraPresetRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Distância da câmera ao ponto de foco (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float Distance = 600.0f;

	/** Elevação da câmera (graus; positivo = câmera acima olhando para baixo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float PitchDeg = 15.0f;

	/** Yaw relativo ao facing do alvo: 180 = de frente para o rosto; 0 = por trás. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float YawDeg = 180.0f;

	/** Offset local no alvo do ponto de foco (default ~altura do peito de um char de 180cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector FocusOffset = FVector(0.0f, 0.0f, 120.0f);

	/** Offset local extra aplicado só na mira — composição (ex: regra dos terços). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector LookAtOffset = FVector::ZeroVector;

	/** Campo de visão (graus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float FOV = 60.0f;

	/** true = a câmera re-enquadra o alvo em movimento a cada frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bFollowTarget = true;

	/** Velocidade de interpolação do follow (0 = snap sem suavização). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float FollowLagSpeed = 5.0f;

	/**
	 * true = profundidade de campo cinematográfica: foco cravado no personagem
	 * (distância focal = Distance) e fundo desfocado.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bEnableDepthOfField = false;

	/** Abertura (f-stop) do DOF — menor = fundo mais desfocado (1.2 = bem cinematográfico). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float Aperture = 1.2f;
};

/**
 * FJRPGCameraShakeParams
 * Parâmetros de um camera shake procedural (Perlin noise por eixo).
 * Duration = 0 significa loop infinito até StopAllCameraShakes.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGCameraShakeParams
{
	GENERATED_BODY()

	/** Duração total (s). 0 = loop até StopAllCameraShakes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	float Duration = 0.5f;

	/** Rampa de entrada (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	float BlendInTime = 0.05f;

	/** Rampa de saída (s) — também usada no fade do StopAllCameraShakes suave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	float BlendOutTime = 0.2f;

	/** Amplitude de posição por eixo, em espaço da câmera (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	FVector LocationAmplitude = FVector(3.0f, 3.0f, 2.0f);

	/** Frequência do ruído de posição (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	float LocationFrequency = 14.0f;

	/** Amplitude de rotação (graus, Pitch/Yaw/Roll). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	FRotator RotationAmplitude = FRotator(0.6f, 0.6f, 0.3f);

	/** Frequência do ruído de rotação (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	float RotationFrequency = 12.0f;

	/** Amplitude de FOV (graus; 0 = sem pulso de FOV). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	float FOVAmplitude = 0.0f;

	/** Frequência do ruído de FOV (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	float FOVFrequency = 0.0f;
};

/**
 * Defaults hardcoded dos presets — funcionam sem nenhum setup no editor.
 * Valores assumem personagens de ~180cm; sobrescreva via DataTable se precisar.
 */
namespace JRPGCameraPresets
{
	/** Enquadramento built-in do preset (fallback quando não há DataTable/row). */
	JRPGFRAMEWORK_API FCameraPresetRow GetBuiltIn(ECameraFramingPreset Preset);

	/** Parâmetros built-in da variação de shake. */
	JRPGFRAMEWORK_API FJRPGCameraShakeParams GetShakePreset(ECameraShakePreset Preset);

	/** Nome curto do preset ("ShopFront") — row name na DataTable e ID nos broadcasts. */
	JRPGFRAMEWORK_API FName FramingPresetToName(ECameraFramingPreset Preset);
}
