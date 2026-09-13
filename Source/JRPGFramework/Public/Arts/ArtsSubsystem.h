#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ArtsSubsystem.generated.h"

/**
 * UArtsSubsystem
 * Stub do subsistema que gerencia a lista de Arts e Hyper Arts conhecidas pelos personagens.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UArtsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UArtsSubsystem();

	/**
	 * Ensina uma Hyper Art a um personagem específico (via Art Book).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	void TeachArtToCharacter(const FString& CharacterName, FName ArtKey);
};
