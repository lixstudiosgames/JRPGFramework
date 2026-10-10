#include "Field/JRPGFieldMapSettings.h"
#include "Components/BillboardComponent.h"
#include "EngineUtils.h"

AJRPGFieldMapSettings::AJRPGFieldMapSettings()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

#if WITH_EDITORONLY_DATA
	// Só um ícone no editor para achar o ator no level
	EditorIcon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		EditorIcon->SetupAttachment(RootComponent);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}

AJRPGFieldMapSettings* AJRPGFieldMapSettings::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AJRPGFieldMapSettings> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
