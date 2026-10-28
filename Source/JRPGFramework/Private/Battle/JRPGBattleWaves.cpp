#include "Battle/JRPGBattleWaves.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/JRPGBattleArena.h"
#include "Components/BillboardComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	int32 JsonInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Default)
	{
		double Value = 0.0;
		return Object->TryGetNumberField(Field, Value) ? FMath::RoundToInt32(Value) : Default;
	}

	float JsonFloat(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float Default)
	{
		double Value = 0.0;
		return Object->TryGetNumberField(Field, Value) ? static_cast<float>(Value) : Default;
	}

	bool JsonBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool Default)
	{
		bool Value = false;
		return Object->TryGetBoolField(Field, Value) ? Value : Default;
	}

	FString JsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}
}

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

	if (!WavesFile.IsEmpty())
	{
		FString Error;
		if (LoadWavesFile(Error))
		{
			Print(FString::Printf(TEXT("%d onda(s) lidas de %s"), Waves.Num(), *WavesFile), FColor::Cyan);
		}
		else
		{
			Print(FString::Printf(TEXT("WavesFile: %s — usando as ondas do Details"), *Error), FColor::Orange);
		}
	}

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

// ---------------------------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------------------------

FString AJRPGBattleWaves::GetWavesFilePath() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), WavesFile));
}

bool AJRPGBattleWaves::LoadWavesFile(FString& OutError)
{
	if (WavesFile.IsEmpty())
	{
		OutError = TEXT("WavesFile vazio");
		return false;
	}
	const FString Path = GetWavesFilePath();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("não achei %s"), *Path);
		return false;
	}
	return LoadWavesFromJson(Json, OutError);
}

bool AJRPGBattleWaves::LoadWavesFromJson(const FString& Json, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("JSON inválido: %s"), *Reader->GetErrorMessage());
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* WaveValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("waves"), WaveValues))
	{
		OutError = TEXT("sem o campo \"waves\"");
		return false;
	}

	TArray<FJRPGBattleWave> NewWaves;
	for (const TSharedPtr<FJsonValue>& WaveValue : *WaveValues)
	{
		const TSharedPtr<FJsonObject> WaveObject = WaveValue.IsValid() ? WaveValue->AsObject() : nullptr;
		if (!WaveObject.IsValid())
		{
			continue;
		}
		FJRPGBattleWave& Wave = NewWaves.AddDefaulted_GetRef();
		Wave.Name = JsonString(WaveObject, TEXT("name"));
		Wave.Encounter.EnemiesPerBattleMin = FMath::Max(1, JsonInt(WaveObject, TEXT("enemiesMin"), 1));
		Wave.Encounter.EnemiesPerBattleMax = FMath::Max(Wave.Encounter.EnemiesPerBattleMin, JsonInt(WaveObject, TEXT("enemiesMax"), Wave.Encounter.EnemiesPerBattleMin));

		const TArray<TSharedPtr<FJsonValue>>* EnemyValues = nullptr;
		if (WaveObject->TryGetArrayField(TEXT("enemies"), EnemyValues))
		{
			for (const TSharedPtr<FJsonValue>& EnemyValue : *EnemyValues)
			{
				const TSharedPtr<FJsonObject> EnemyObject = EnemyValue.IsValid() ? EnemyValue->AsObject() : nullptr;
				if (!EnemyObject.IsValid())
				{
					continue;
				}
				FJRPGEncounterEnemy& Enemy = Wave.Encounter.Enemies.AddDefaulted_GetRef();
				Enemy.EnemyKey = FName(*JsonString(EnemyObject, TEXT("key")));
				Enemy.LevelMin = FMath::Max(1, JsonInt(EnemyObject, TEXT("levelMin"), 1));
				Enemy.LevelMax = FMath::Max(Enemy.LevelMin, JsonInt(EnemyObject, TEXT("levelMax"), Enemy.LevelMin));
				Enemy.Weight = FMath::Max(0, JsonInt(EnemyObject, TEXT("weight"), 1));
				Enemy.CountMin = FMath::Max(0, JsonInt(EnemyObject, TEXT("countMin"), 1));
				Enemy.CountMax = FMath::Max(Enemy.CountMin, JsonInt(EnemyObject, TEXT("countMax"), Enemy.CountMin));
				Enemy.bAlwaysJoins = JsonBool(EnemyObject, TEXT("alwaysJoins"), false);
			}
		}
	}
	if (NewWaves.Num() == 0)
	{
		OutError = TEXT("nenhuma onda no arquivo");
		return false;
	}

	Waves = MoveTemp(NewWaves);
	const TArray<TSharedPtr<FJsonValue>>* PartyValues = nullptr;
	if (Root->TryGetArrayField(TEXT("party"), PartyValues))
	{
		Party.Reset();
		for (const TSharedPtr<FJsonValue>& Member : *PartyValues)
		{
			if (Member.IsValid() && !Member->AsString().IsEmpty())
			{
				Party.Add(FName(*Member->AsString()));
			}
		}
	}
	bLoop = JsonBool(Root, TEXT("loop"), bLoop);
	StartDelay = FMath::Max(0.0f, JsonFloat(Root, TEXT("startDelay"), StartDelay));
	DelayBetweenWaves = FMath::Max(0.0f, JsonFloat(Root, TEXT("delayBetweenWaves"), DelayBetweenWaves));
	return true;
}

void AJRPGBattleWaves::ImportWavesFile()
{
#if WITH_EDITOR
	Modify();
#endif
	FString Error;
	const bool bOk = LoadWavesFile(Error);
	const FString Message = bOk
		? FString::Printf(TEXT("BattleWaves: %d onda(s) importadas de %s"), Waves.Num(), *GetWavesFilePath())
		: FString::Printf(TEXT("BattleWaves: %s"), *Error);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, bOk ? FColor::Green : FColor::Red, Message);
	}
}

// ---------------------------------------------------------------------------------------------
// Ondas
// ---------------------------------------------------------------------------------------------

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
	Setup.Enemies = UJRPGEncounterLibrary::RollEncounter(Waves[WaveIndex].Encounter);
	const AActor* Where = Arena ? static_cast<const AActor*>(Arena.Get()) : this;
	Setup.Location = Where->GetActorLocation();
	Setup.FacingYaw = Where->GetActorRotation().Yaw;
	if (Setup.Enemies.Num() == 0)
	{
		Print(FString::Printf(TEXT("%s não sorteou inimigo nenhum — o encounter está vazio?"), *DescribeWave(WaveIndex)), FColor::Red);
		return false;
	}

	CurrentWave = WaveIndex;
	if (!Battle->BeginBattle(Setup))
	{
		Print(FString::Printf(TEXT("%s não começou (já há uma luta?)"), *DescribeWave(WaveIndex)), FColor::Red);
		return false;
	}
	bWaveInProgress = true;

	TArray<FString> Names;
	for (const FJRPGEncounterCombatant& C : Setup.Enemies)
	{
		Names.Add(FString::Printf(TEXT("%s Lv%d"), *C.EnemyKey.ToString(), C.Level));
	}
	Print(FString::Printf(TEXT("%s: %s"), *DescribeWave(WaveIndex), *FString::Join(Names, TEXT(", "))), FColor::Yellow);
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
		Print(FString::Printf(TEXT("%s não vencida (%s) — as ondas pararam"), *DescribeWave(CurrentWave),
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
	Print(FString::Printf(TEXT("%s vencida"), *DescribeWave(CurrentWave)), FColor::Green);
	ScheduleWave(Next, DelayBetweenWaves);
}

void AJRPGBattleWaves::ScheduleWave(int32 WaveIndex, float Delay)
{
	// A cena da luta anterior ainda está saindo (o fade) no instante do OnBattleEnded
	GetWorldTimerManager().SetTimer(WaveTimer, FTimerDelegate::CreateWeakLambda(this, [this, WaveIndex]()
	{
		if (bRunning)
		{
			StartWave(WaveIndex);
		}
	}), FMath::Max(Delay, 0.01f), false);
}

FString AJRPGBattleWaves::DescribeWave(int32 WaveIndex) const
{
	const FString Number = FString::Printf(TEXT("onda %d/%d"), WaveIndex + 1, Waves.Num());
	return Waves.IsValidIndex(WaveIndex) && !Waves[WaveIndex].Name.IsEmpty()
		? FString::Printf(TEXT("%s (%s)"), *Number, *Waves[WaveIndex].Name)
		: Number;
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
