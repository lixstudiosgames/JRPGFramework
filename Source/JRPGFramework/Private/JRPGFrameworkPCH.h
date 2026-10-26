#pragma once

/**
 * PCH do módulo (PrivatePCHHeaderFile no JRPGFramework.Build.cs).
 *
 * Existe para calar os avisos C4996 que os PRÓPRIOS headers da engine (5.8) disparam: overrides
 * de funções que a Epic marcou como obsoletas em Class.h, WorldSubsystem.h, Character.h... São
 * ~150 avisos por build, nenhum do nosso código. Cada header é lido uma vez aqui, com o aviso
 * de obsolescência desligado; o #pragma once impede que ele seja lido de novo nos .cpp.
 *
 * O aviso volta a valer depois deste bloco: chamar API obsoleta no NOSSO código continua
 * avisando. Header novo da engine que comece a avisar no build: inclua aqui.
 */

#include "CoreMinimal.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "UObject/Class.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Character.h"
#include "Curves/CurveFloat.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundWave.h"
#include "Engine/ChildConnection.h"
#include "Animation/AnimSequence.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
