#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "JRPGGameInstance.generated.h"

/**
 * UJRPGGameInstance
 * Classe base de GameInstance para o framework. O Blueprint principal (GI_Main) do jogo
 * pode herdar desta classe para compatibilidade e recursos C++.
 *
 * O estado global da partida (Gold, playtime, mapa, New Game) vive no
 * UCoreSubsystem (Core/CoreSubsystem.h) — não aqui.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UJRPGGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UJRPGGameInstance();

	virtual void Init() override;
	virtual void Shutdown() override;
};
