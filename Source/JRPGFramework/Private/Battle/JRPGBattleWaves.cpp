#include "Battle/JRPGBattleWaves.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/JRPGBattleArena.h"
#include "Components/BillboardComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AJRPGBattleWaves::AJRPGBattleWaves()
{
	PrimaryActorTick.bCanEverTick = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
#if WITH_EDITORONLY_DATA
	EditorIcon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(TEXT("/Engine/EditorResources/S_Trigger.S_Trigger"));
		EditorIcon->Sprite = Icon.Get();
		EditorIcon->SetupAttachment(GetRootComponent());
		EditorIcon->SetHiddenInGame(true);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}

void AJRPGBattleWaves::BeginPlay()
{
	Super::BeginPlay();

	if (UBattleSubsystem* Battle = GetGameInstance()->GetSubsystem<UBattleSubsystem>())
	{
		Battle->OnBattleEnded.AddDynamic(this, &AJRPGBattleWaves::HandleBattleEnded);
	}
	if (bAutoStart)
	{
		StartWaves();
	}
}

void AJRPGBattleWaves::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UBattleSubsystem* Battle = GI->GetSubsystem<UBattleSubsystem>())
		{
			Battle->OnBattleEnded.RemoveDynamic(this, &AJRPGBattleWaves::HandleBattleEnded);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AJRPGBattleWaves::StartWaves()
{
	if (Waves.Num() == 0)
	{
		Print(TEXT("nenhuma onda configurada"), FColor::Red);
		return;
	}
	bRunning = true;
	ScheduleWave(0, StartDelay);
}

void AJRPGBattleWaves::StopWaves()
{
	bRunning = false;
	GetWorldTimerManager().ClearTimer(WaveTimer);
}

bool AJRPGBattleWaves::StartWave(int32 WaveIndex)
{
	UBattleSubsystem* Battle = GetGameInstance()->GetSubsystem<UBattleSubsystem>();
	if (!Battle || !Waves.IsValidIndex(WaveIndex))
	{
		return false;
	}

	FJRPGBattleSetup Setup;
	Setup.Party = Party;
	Setup.Enemies = UJRPGEncounterLibrary::RollEncounter(Waves[WaveIndex]);
	const AActor* Where = Arena ? static_cast<const AActor*>(Arena.Get()) : this;
	Setup.Location = Where->GetActorLocation();
	Setup.FacingYaw = Where->GetActorRotation().Yaw;
	if (Setup.Enemies.Num() == 0)
	{
		Print(FString::Printf(TEXT("a onda %d não sorteou inimigo nenhum — o encounter está vazio?"), WaveIndex + 1), FColor::Red);
		return false;
	}

	CurrentWave = WaveIndex;
	if (!Battle->BeginBattle(Setup))
	{
		Print(FString::Printf(TEXT("a onda %d não começou (já há uma luta?)"), WaveIndex + 1), FColor::Red);
		return false;
	}
	bWaveInProgress = true;

	TArray<FString> Names;
	for (const FJRPGEncounterCombatant& C : Setup.Enemies)
	{
		Names.Add(FString::Printf(TEXT("%s Lv%d"), *C.EnemyKey.ToString(), C.Level));
	}
	Print(FString::Printf(TEXT("ONDA %d/%d: %s"), WaveIndex + 1, Waves.Num(), *FString::Join(Names, TEXT(", "))), FColor::Yellow);
	OnWaveStarted.Broadcast(WaveIndex);
	return true;
}

void AJRPGBattleWaves::HandleBattleEnded(EJRPGBattleResult Result)
{
	if (!bWaveInProgress)
	{
		return; // uma luta que não era onda (um inimigo do Field no mesmo mapa)
	}
	bWaveInProgress = false;
	if (!bRunning)
	{
		return;
	}

	if (Result != EJRPGBattleResult::Victory)
	{
		bRunning = false;
		Print(FString::Printf(TEXT("onda %d não vencida (%s) — as ondas pararam"), CurrentWave + 1,
			*StaticEnum<EJRPGBattleResult>()->GetNameStringByValue(static_cast<int64>(Result))), FColor::Red);
		OnWavesFailed.Broadcast(Result);
		return;
	}

	int32 Next = CurrentWave + 1;
	if (!Waves.IsValidIndex(Next))
	{
		if (!bLoop)
		{
			bRunning = false;
			Print(TEXT("todas as ondas vencidas!"), FColor::Green);
			OnWavesCleared.Broadcast();
			return;
		}
		Next = 0;
	}
	Print(FString::Printf(TEXT("onda %d vencida"), CurrentWave + 1), FColor::Green);
	ScheduleWave(Next, DelayBetweenWaves);
}

void AJRPGBattleWaves::ScheduleWave(int32 WaveIndex, float Delay)
{
	// O palco ainda está saindo da luta anterior (o fade) no mesmo instante do OnBattleEnded
	GetWorldTimerManager().SetTimer(WaveTimer, FTimerDelegate::CreateWeakLambda(this, [this, WaveIndex]()
	{
		if (bRunning)
		{
			StartWave(WaveIndex);
		}
	}), FMath::Max(Delay, 0.01f), false);
}

void AJRPGBattleWaves::Print(const FString& Message, const FColor& Color) const
{
	if (!bPrintDebug)
	{
		return;
	}
	const FString Line = FString::Printf(TEXT("BattleWaves: %s"), *Message);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Line);
	}
}
