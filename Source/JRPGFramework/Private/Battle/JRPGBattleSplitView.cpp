#include "Battle/JRPGBattleSplitView.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameMapsSettings.h"
#include "HAL/IConsoleManager.h"

bool UJRPGBattleSplitView::Start(UWorld* InWorld, int32 Count, UCameraComponent* MainLens)
{
	const UCameraComponent* LensFrom = MainLens;
	const double StartSeconds = FPlatformTime::Seconds();
	if (bActive || !InWorld)
	{
		return false;
	}
	UGameInstance* GI = InWorld->GetGameInstance();
	UGameViewportClient* Viewport = InWorld->GetGameViewport();
	if (!GI || !Viewport)
	{
		return false;
	}
	// Um jogo que já está em split (co-op local) não é mexido: a vitória fica numa vista só
	if (GI->GetNumLocalPlayers() != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSplitView: %d jogadores locais já existem — a tela não é dividida"), GI->GetNumLocalPlayers());
		return false;
	}
	Count = FMath::Clamp(Count, 1, FMath::Min(4, Viewport->MaxSplitscreenPlayers));
	if (Count < 2)
	{
		return false;
	}

	// O split da engine em colunas, só enquanto a tela está dividida (antes de criar os jogadores: o NotifyPlayerAdded já faz o layout)
	UGameMapsSettings* Settings = GetMutableDefault<UGameMapsSettings>();
	bSavedUseSplitscreen = Settings->bUseSplitscreen;
	SavedTwoLayout = static_cast<uint8>(Settings->TwoPlayerSplitscreenLayout.GetValue());
	SavedThreeLayout = static_cast<uint8>(Settings->ThreePlayerSplitscreenLayout.GetValue());
	SavedFourLayout = static_cast<uint8>(Settings->FourPlayerSplitscreenLayout);
	bSavedForceDisable = Viewport->IsSplitscreenForceDisabled();
	Settings->bUseSplitscreen = true;
	Settings->TwoPlayerSplitscreenLayout = ETwoPlayerSplitScreenType::Vertical;
	Settings->ThreePlayerSplitscreenLayout = EThreePlayerSplitScreenType::Vertical;
	Settings->FourPlayerSplitscreenLayout = EFourPlayerSplitScreenType::Vertical;
	if (bSavedForceDisable)
	{
		Viewport->SetForceDisableSplitscreen(false);
	}
	// Sem os distance fields da cena enquanto a tela está dividida (ver o .h), antes de as colunas renderizarem. Com a mesma
	// prioridade de quem pôs o valor: a escalabilidade ou o console continuam mandando nele depois
	bDistanceFieldAOChanged = false;
	if (IConsoleVariable* DistanceFieldAO = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DistanceFieldAO")))
	{
		SavedDistanceFieldAO = DistanceFieldAO->GetInt();
		SavedDistanceFieldAOSetBy = static_cast<uint32>(DistanceFieldAO->GetFlags()) & static_cast<uint32>(ECVF_SetByMask);
		if (SavedDistanceFieldAO != 0)
		{
			DistanceFieldAO->Set(0, static_cast<EConsoleVariableFlags>(SavedDistanceFieldAOSetBy));
			bDistanceFieldAOChanged = true;
		}
	}

	World = InWorld;
	bActive = true;

	// Sem SSGI em nenhuma coluna enquanto a tela está dividida (ver o .h); a câmera da luta guarda o que tinha
	MainLensComp = MainLens;
	if (MainLens)
	{
		bSavedMainGIOverride = MainLens->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod != 0;
		SavedMainGIMethod = static_cast<uint8>(MainLens->PostProcessSettings.DynamicGlobalIlluminationMethod.GetValue());
		MainLens->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
		MainLens->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	for (int32 View = 1; View < Count; ++View)
	{
		// Sem PlayerController do jogo: o SpawnPlayActor faria o login no GameMode (o controller do Field e um pawn novo)
		FString Error;
		ULocalPlayer* Player = GI->CreateLocalPlayer(-1, Error, false);
		if (!Player)
		{
			UE_LOG(LogTemp, Warning, TEXT("BattleSplitView: o jogador local da coluna %d não foi criado (%s)"), View, *Error);
			break;
		}
		Players.Add(Player);

		APlayerController* Controller = InWorld->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity, Params);
		ACameraActor* Camera = InWorld->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		if (!Controller || !Camera)
		{
			UE_LOG(LogTemp, Warning, TEXT("BattleSplitView: o controller / a câmera da coluna %d não nasceu"), View);
			if (Camera)
			{
				Camera->Destroy();
			}
			if (Controller)
			{
				Controller->Destroy();
			}
			break;
		}
		Controller->SetPlayer(Player);
		Controllers.Add(Controller);

		// A lente da câmera da luta (o FOV do original com a vertical presa): numa coluna estreita a altura do herói fica a mesma
		if (UCameraComponent* Lens = Camera->GetCameraComponent())
		{
			if (LensFrom)
			{
				Lens->SetFieldOfView(LensFrom->FieldOfView);
				Lens->SetAspectRatio(LensFrom->AspectRatio);
				Lens->bConstrainAspectRatio = false;
				Lens->bOverrideAspectRatioAxisConstraint = LensFrom->bOverrideAspectRatioAxisConstraint;
				Lens->SetAspectRatioAxisConstraint(LensFrom->AspectRatioAxisConstraint);
				Lens->PostProcessSettings = LensFrom->PostProcessSettings;
				Lens->PostProcessBlendWeight = LensFrom->PostProcessBlendWeight;
			}
			else
			{
				Lens->bConstrainAspectRatio = false;
				Lens->PostProcessBlendWeight = 1.0f;
			}
			Lens->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
			Lens->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
		}
		ExtraCameras.Add(Camera);
		Controller->SetViewTarget(Camera);
	}

	if (ExtraCameras.Num() == 0)
	{
		Stop();
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("BattleSplitView: tela dividida em %d colunas (Start levou %.2f ms no game thread)"), ExtraCameras.Num() + 1,
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	return true;
}

void UJRPGBattleSplitView::Stop()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;

	UWorld* CurrentWorld = World.Get();
	UGameInstance* GI = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	for (int32 Index = Players.Num() - 1; Index >= 0; --Index)
	{
		ULocalPlayer* Player = Players[Index];
		if (!Player)
		{
			continue;
		}
		// O controller já morreu (o mundo desmontando): o RemoveLocalPlayer não pode tentar destruí-lo de novo
		if (Player->PlayerController && !IsValid(Player->PlayerController))
		{
			Player->PlayerController = nullptr;
		}
		if (GI && GI->GetLocalPlayers().Contains(Player))
		{
			GI->RemoveLocalPlayer(Player);
		}
	}
	for (APlayerController* Controller : Controllers)
	{
		if (IsValid(Controller))
		{
			Controller->Destroy();
		}
	}
	for (ACameraActor* Camera : ExtraCameras)
	{
		if (IsValid(Camera))
		{
			Camera->Destroy();
		}
	}
	Players.Reset();
	Controllers.Reset();
	ExtraCameras.Reset();

	// A GI da câmera da luta volta ao que era
	if (UCameraComponent* MainLens = MainLensComp.Get())
	{
		MainLens->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = bSavedMainGIOverride;
		MainLens->PostProcessSettings.DynamicGlobalIlluminationMethod = static_cast<EDynamicGlobalIlluminationMethod::Type>(SavedMainGIMethod);
	}
	MainLensComp = nullptr;

	UGameMapsSettings* Settings = GetMutableDefault<UGameMapsSettings>();
	Settings->bUseSplitscreen = bSavedUseSplitscreen;
	Settings->TwoPlayerSplitscreenLayout = static_cast<ETwoPlayerSplitScreenType::Type>(SavedTwoLayout);
	Settings->ThreePlayerSplitscreenLayout = static_cast<EThreePlayerSplitScreenType::Type>(SavedThreeLayout);
	Settings->FourPlayerSplitscreenLayout = static_cast<EFourPlayerSplitScreenType>(SavedFourLayout);
	if (UGameViewportClient* Viewport = CurrentWorld ? CurrentWorld->GetGameViewport() : nullptr)
	{
		// O SetForceDisableSplitscreen refaz o layout: o jogador 0 volta para a tela inteira
		Viewport->SetForceDisableSplitscreen(bSavedForceDisable);
	}
	// Os distance fields voltam (a tela já está preta: o Global DF da vista do jogador se refaz sem ninguém ver)
	if (bDistanceFieldAOChanged)
	{
		if (IConsoleVariable* DistanceFieldAO = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DistanceFieldAO")))
		{
			DistanceFieldAO->Set(SavedDistanceFieldAO, static_cast<EConsoleVariableFlags>(SavedDistanceFieldAOSetBy));
		}
		bDistanceFieldAOChanged = false;
	}
	World = nullptr;
	UE_LOG(LogTemp, Log, TEXT("BattleSplitView: tela inteira de novo"));
}

ACameraActor* UJRPGBattleSplitView::GetExtraCamera(int32 View) const
{
	const int32 Index = View - 1;
	return bActive && ExtraCameras.IsValidIndex(Index) ? ExtraCameras[Index].Get() : nullptr;
}

void UJRPGBattleSplitView::FadeExtraViews(float FromAlpha, float ToAlpha, float Duration, FLinearColor Color, bool bHoldWhenFinished)
{
	if (!bActive)
	{
		return;
	}
	for (APlayerController* Controller : Controllers)
	{
		if (APlayerCameraManager* CameraManager = IsValid(Controller) ? Controller->PlayerCameraManager.Get() : nullptr)
		{
			CameraManager->StartCameraFade(FromAlpha, ToAlpha, Duration, Color, false, bHoldWhenFinished);
		}
	}
}
