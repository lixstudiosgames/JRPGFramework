#pragma once

#include "CoreMinimal.h"
#include "JRPGFieldTypes.generated.h"

/**
 * Como a câmera do personagem do Field está: o braço com rotação do mundo, o FOV e o lag.
 * Usada no padrão do personagem, no AJRPGFieldMapSettings (o padrão de cada mapa) e nas
 * AJRPGCameraZone em modo Player Camera. Trocas entre duas configs fazem blend.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGFieldCameraSettings
{
	GENERATED_BODY()

	/** Distância da câmera até o personagem, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float ArmLength = 1800.0f;

	/** Inclinação do braço em graus: negativo olha de cima (-50 = de cima e de frente, -89 = vertical). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "-89.0", ClampMax = "89.0"))
	float Pitch = -50.0f;

	/**
	 * Para onde a câmera olha no mundo, em graus (0 = olhando para +X). É o "norte" da tela: um
	 * mapa feito com o norte em +Y usa 90. Com o controle relativo à câmera, andar acompanha.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float Yaw = 0.0f;

	/** Campo de visão em graus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float FOV = 55.0f;

	/** Atraso da câmera seguindo o personagem (maior = segue mais colado; 0 = sem atraso). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float LagSpeed = 2.5f;

	/** Desloca o ponto que a câmera segue, em uu (Z sobe o foco: enquadra mais acima do personagem). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector FocusOffset = FVector::ZeroVector;

	/** Mistura A -> B (Alpha 0..1). O yaw vai pelo lado mais curto. */
	static FJRPGFieldCameraSettings Blend(const FJRPGFieldCameraSettings& A, const FJRPGFieldCameraSettings& B, float Alpha);
};

/** De onde vem o "para cima" do controle no Field. */
UENUM(BlueprintType)
enum class EJRPGFieldControlMode : uint8
{
	/** Relativo à câmera que está na tela: cima = para longe da câmera. */
	CameraRelative UMETA(DisplayName = "Camera Relative"),
	/**
	 * Relativo a um yaw fixo do mundo (ControlYaw), seja qual for a câmera: cavernas e áreas
	 * com câmera fixa, onde "cima" deve continuar sendo o mesmo lado do mapa.
	 */
	FixedYaw UMETA(DisplayName = "Fixed Yaw")
};
