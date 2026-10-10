#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Field/JRPGFieldTypes.h"
#include "JRPGFieldMapSettings.generated.h"

class UBillboardComponent;

/**
 * AJRPGFieldMapSettings
 * O padrão do personagem do Field NESTE mapa: câmera, controle e velocidades. Arraste um no
 * level (um por mapa) — o AJRPGFieldCharacter lê no BeginPlay. Substitui chamar uma função de
 * config no Level Blueprint de cada mapa.
 *
 * É a câmera "base": as AJRPGCameraZone mudam a câmera por cima dela e, ao sair da última
 * zona, o personagem volta para esta. Sem um deste no mapa, valem os padrões do personagem.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGFieldMapSettings : public AActor
{
	GENERATED_BODY()

public:
	AJRPGFieldMapSettings();

	/** Câmera base do mapa (o Yaw é o "norte" da tela). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field")
	FJRPGFieldCameraSettings Camera;

	/** De onde vem o "cima" do controle neste mapa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field")
	EJRPGFieldControlMode ControlMode = EJRPGFieldControlMode::CameraRelative;

	/** Fixed Yaw: o yaw do mundo que é "cima" no controle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field", meta = (EditCondition = "ControlMode == EJRPGFieldControlMode::FixedYaw", EditConditionHides))
	float ControlYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field", meta = (InlineEditConditionToggle))
	bool bOverrideCanSprint = false;

	/** Pode correr neste mapa (World Map, cidades onde não se corre...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field", meta = (EditCondition = "bOverrideCanSprint"))
	bool bCanSprint = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field", meta = (InlineEditConditionToggle))
	bool bOverrideWalkSpeed = false;

	/** Velocidade andando neste mapa (a escala do mapa muda o que parece rápido). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field", meta = (EditCondition = "bOverrideWalkSpeed", ClampMin = "0.0"))
	float WalkSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field", meta = (InlineEditConditionToggle))
	bool bOverrideSprintSpeed = false;

	/** Velocidade correndo neste mapa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field", meta = (EditCondition = "bOverrideSprintSpeed", ClampMin = "0.0"))
	float SprintSpeed = 700.0f;

	/** O AJRPGFieldMapSettings do mundo (o primeiro, se houver mais de um). */
	static AJRPGFieldMapSettings* Find(const UWorld* World);

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif
};
