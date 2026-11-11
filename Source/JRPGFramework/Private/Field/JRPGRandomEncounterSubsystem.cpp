#include "Field/JRPGRandomEncounterSubsystem.h"
#include "Field/JRPGEncounterZone.h"
#include "Field/JRPGFieldPlayerController.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/JRPGBattleSceneSubsystem.h"
#include "UI/WebUISubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

void UJRPGRandomEncounterSubsystem::RegisterZone(AJRPGEncounterZone* Zone)
{
	if (Zone)
	{
		Zones.AddUnique(Zone);
	}
}

void UJRPGRandomEncounterSubsystem::UnregisterZone(AJRPGEncounterZone* Zone)
{
	Zones.Remove(Zone);
	if (CurrentZone.Get() == Zone)
	{
		CurrentZone.Reset();
		bHasTile = false;
	}
}

bool UJRPGRandomEncounterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UJRPGRandomEncounterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// A entrada no mapa: o contador abaixo de 487 ganha metade de um sorteio (FUN_8003AB2C); sem contador, sorteia um
	if (const UGameInstance* GI = InWorld.GetGameInstance())
	{
		if (UBattleSubsystem* Battle = GI->GetSubsystem<UBattleSubsystem>())
		{
			Battle->TopUpEncounterCounterOnMapEntry();
		}
	}
}

TStatId UJRPGRandomEncounterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UJRPGRandomEncounterSubsystem, STATGROUP_Tickables);
}

AJRPGEncounterZone* UJRPGRandomEncounterSubsystem::FindZoneAt(const FVector& Point) const
{
	AJRPGEncounterZone* Best = nullptr;
	for (const TWeakObjectPtr<AJRPGEncounterZone>& Ptr : Zones)
	{
		// Taxa 0 ainda é a zona do passo (no original, um contador já em 0 dispara até numa região de taxa 0)
		AJRPGEncounterZone* Zone = Ptr.Get();
		if (!Zone || !Zone->bRandomEncounters || !Zone->IsZoneActive() || !Zone->ContainsPoint(Point))
		{
			continue;
		}
		if (!Best || Zone->Priority > Best->Priority)
		{
			Best = Zone;
		}
	}
	return Best;
}

bool UJRPGRandomEncounterSubsystem::IsRollHeld(const APawn* Player) const
{
	const UWorld* World = GetWorld();
	if (!World || World->IsPaused())
	{
		return true;
	}
	const UGameInstance* GI = World->GetGameInstance();
	const UBattleSubsystem* Battle = GI ? GI->GetSubsystem<UBattleSubsystem>() : nullptr;
	if (!Battle || Battle->IsInBattle() || Battle->IsInEncounterGrace())
	{
		return true;
	}
	const UJRPGBattleSceneSubsystem* Scene = World->GetSubsystem<UJRPGBattleSceneSubsystem>();
	if (Scene && Scene->IsFieldHidden())
	{
		return true;
	}
	// Menu, diálogo, loja...: o mundo está parado
	const UWebUISubsystem* WebUI = GI->GetSubsystem<UWebUISubsystem>();
	if (WebUI && WebUI->GetUIState() != EJRPGUIState::HudOnly)
	{
		return true;
	}
	// O controle do Field desligado (porta, cena, a luta montando)
	const AJRPGFieldPlayerController* Controller = Player ? Cast<AJRPGFieldPlayerController>(Player->GetController()) : nullptr;
	return Controller && !Controller->IsFieldInputEnabled();
}

void UJRPGRandomEncounterSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Zones.Num() == 0)
	{
		return;
	}
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		bHasTile = false;
		return;
	}

	const FVector Location = Player->GetActorLocation();
	AJRPGEncounterZone* Zone = FindZoneAt(Location);
	if (Zone != CurrentZone.Get())
	{
		// Trocou de zona (ou saiu de todas): o tile é guardado de novo na grade da zona nova, sem contar passo
		CurrentZone = Zone;
		bHasTile = false;
		if (Zone && Zone->bPrintDebug && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, FString::Printf(TEXT("RandomEncounter: entrou em %s (taxa %d, passo %.0f cm)"),
				*Zone->GetActorNameOrLabel(), Zone->GetEffectiveEncounterRate(), Zone->GetStepLength(Player)));
		}
	}
	if (!Zone)
	{
		return;
	}

	if (!bHasTile)
	{
		// O tamanho do passo fica o mesmo enquanto ele está nesta zona (a grade não muda debaixo dele)
		StepLength = FMath::Max(static_cast<double>(Zone->GetStepLength(Player)), 1.0);
	}
	const FIntPoint Tile(FMath::FloorToInt32(Location.X / StepLength), FMath::FloorToInt32(Location.Y / StepLength));
	if (!bHasTile)
	{
		LastTile = Tile;
		bHasTile = true;
		return;
	}
	if (Tile == LastTile)
	{
		return;
	}
	const bool bStep = UJRPGEncounterLibrary::IsEncounterStep(LastTile, Tile);
	LastTile = Tile;
	if (!bStep || IsRollHeld(Player))
	{
		return;   // o tile anda junto, mas não é passo (pulo, porta) ou o passo não conta (menu, luta, carência)
	}

	UGameInstance* GI = GetWorld()->GetGameInstance();
	UBattleSubsystem* Battle = GI ? GI->GetSubsystem<UBattleSubsystem>() : nullptr;
	if (!Battle)
	{
		return;
	}
	const int32 Rate = Zone->GetEffectiveEncounterRate();
	const bool bFire = Battle->ConsumeEncounterStep(Rate);
	if (Zone->bPrintDebug && GEngine)
	{
		// Uma linha só, atualizada a cada passo (a chave é a da zona)
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(Zone->GetUniqueID()), 2.0f, FColor::Silver,
			FString::Printf(TEXT("RandomEncounter %s: passo, taxa %d, contador %d%s"), *Zone->GetActorNameOrLabel(), Rate,
				Battle->GetEncounterCounter(), bFire ? TEXT(" — LUTA") : TEXT("")));
	}
	if (bFire)
	{
		Zone->StartRandomEncounter(Player);
	}
}
