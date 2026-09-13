#include "Arts/ArtsSubsystem.h"

UArtsSubsystem::UArtsSubsystem()
{
}

void UArtsSubsystem::TeachArtToCharacter(const FString& CharacterName, FName ArtKey)
{
	// TODO: Implementar lógica de registrar o aprendizado da Hyper Art
	UE_LOG(LogTemp, Warning, TEXT("ArtsSubsystem: Ensinando arte %s para o personagem %s"), *ArtKey.ToString(), *CharacterName);
}
