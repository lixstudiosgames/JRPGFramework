#include "Camera/CameraSubsystem.h"
#include "Camera/JRPGCameraActor.h"
#include "Camera/JRPGCameraShakeModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectGlobals.h"

void UCameraSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Força o WebUISubsystem a inicializar ANTES — ordem determinística para
	// podermos assinar o evento de estado da UI já aqui
	if (UWebUISubsystem* WebUI = Collection.InitializeDependency<UWebUISubsystem>())
	{
		WebUI->OnUIStateChanged.AddDynamic(this, &UCameraSubsystem::HandleUIStateChanged);
	}

	// Mapa novo invalida registry, câmera gerenciada e shake modifier
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UCameraSubsystem::HandlePostLoadMap);
}

void UCameraSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UWebUISubsystem* WebUI = GI->GetSubsystem<UWebUISubsystem>())
		{
			WebUI->OnUIStateChanged.RemoveDynamic(this, &UCameraSubsystem::HandleUIStateChanged);
		}
	}

	if (ManagedCamera.IsValid())
	{
		ManagedCamera->Destroy();
	}

	TargetRegistry.Empty();
	ManagedCamera = nullptr;
	ShakeModifier = nullptr;
	CurrentFocusActor = nullptr;
	ConversationNPC = nullptr;
	ConversationPlayer = nullptr;
	bFocusActive = false;
	bConversationActive = false;
	bAutoRestoreFlag = false;

	Super::Deinitialize();
}

// ============================================================
// REGISTRY DE ALVOS
// ============================================================

void UCameraSubsystem::RegisterCameraTarget(FName TargetID, AActor* TargetActor, ECameraTargetType Type)
{
	if (TargetID.IsNone() || !IsValid(TargetActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: RegisterCameraTarget com ID vazio ou ator inválido."));
		return;
	}

	FCameraTargetEntry Entry;
	Entry.Actor = TargetActor;
	Entry.Type = Type;
	TargetRegistry.Add(TargetID, Entry); // re-registrar sobrescreve
}

void UCameraSubsystem::UnregisterCameraTarget(FName TargetID)
{
	TargetRegistry.Remove(TargetID);
}

AActor* UCameraSubsystem::GetCameraTarget(FName TargetID) const
{
	const FCameraTargetEntry* Entry = TargetRegistry.Find(TargetID);
	return Entry ? Entry->Actor.Get() : nullptr;
}

TArray<AActor*> UCameraSubsystem::GetCameraTargetsByType(ECameraTargetType Type) const
{
	TArray<AActor*> Result;
	for (const TPair<FName, FCameraTargetEntry>& Pair : TargetRegistry)
	{
		if (Pair.Value.Type == Type)
		{
			if (AActor* Actor = Pair.Value.Actor.Get())
			{
				Result.Add(Actor);
			}
		}
	}
	return Result;
}

void UCameraSubsystem::ScanWorldForCameraTargets()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	int32 Found = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor->ActorHasTag(AutoDetectTag))
		{
			continue;
		}

		// Tipo inferido por tags extras no próprio ator
		ECameraTargetType Type = ECameraTargetType::NPC;
		if (Actor->ActorHasTag(TEXT("Player")))
		{
			Type = ECameraTargetType::Player;
		}
		else if (Actor->ActorHasTag(TEXT("Enemy")))
		{
			Type = ECameraTargetType::Enemy;
		}
		else if (Actor->ActorHasTag(TEXT("Prop")))
		{
			Type = ECameraTargetType::Prop;
		}

		RegisterCameraTarget(Actor->GetFName(), Actor, Type);
		++Found;
	}

	UE_LOG(LogTemp, Log, TEXT("CameraSubsystem: Scan por tag '%s' registrou %d alvo(s)."),
		*AutoDetectTag.ToString(), Found);
}

// ============================================================
// FOCO
// ============================================================

void UCameraSubsystem::FocusOnActor(AActor* TargetActor, ECameraFramingPreset Preset,
	float BlendTime, bool bAutoRestoreOnUIClose, float FadeOutTime)
{
	FocusInternal(TargetActor, ResolvePreset(Preset), JRPGCameraPresets::FramingPresetToName(Preset),
		BlendTime, bAutoRestoreOnUIClose, FadeOutTime);
}

void UCameraSubsystem::FocusOnTarget(FName TargetID, ECameraFramingPreset Preset,
	float BlendTime, bool bAutoRestoreOnUIClose, float FadeOutTime)
{
	AActor* Target = GetCameraTarget(TargetID);
	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: FocusOnTarget — alvo '%s' não registrado ou destruído."),
			*TargetID.ToString());
		return;
	}
	FocusOnActor(Target, Preset, BlendTime, bAutoRestoreOnUIClose, FadeOutTime);
}

void UCameraSubsystem::FocusOnActorWithPreset(AActor* TargetActor, FName PresetID,
	float BlendTime, bool bAutoRestoreOnUIClose, float FadeOutTime)
{
	// 1º: row da DataTable com esse nome (presets novos ou overrides)
	if (CameraPresetTable)
	{
		if (const FCameraPresetRow* Row = CameraPresetTable->FindRow<FCameraPresetRow>(
			PresetID, TEXT("FocusOnActorWithPreset"), false))
		{
			FocusInternal(TargetActor, *Row, PresetID, BlendTime, bAutoRestoreOnUIClose, FadeOutTime);
			return;
		}
	}

	// 2º: built-in de mesmo nome (ex: "ShopFront")
	const UEnum* Enum = StaticEnum<ECameraFramingPreset>();
	const int64 Value = Enum ? Enum->GetValueByName(PresetID) : INDEX_NONE;
	if (Value != INDEX_NONE)
	{
		FocusOnActor(TargetActor, static_cast<ECameraFramingPreset>(Value), BlendTime, bAutoRestoreOnUIClose, FadeOutTime);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: preset '%s' não existe (nem na DataTable, nem built-in)."),
		*PresetID.ToString());
}

void UCameraSubsystem::FocusWithCustomParams(AActor* TargetActor, const FCameraPresetRow& Params,
	float BlendTime, bool bAutoRestoreOnUIClose, float FadeOutTime)
{
	FocusInternal(TargetActor, Params, TEXT("Custom"), BlendTime, bAutoRestoreOnUIClose, FadeOutTime);
}

void UCameraSubsystem::FocusOnLevelCamera(AActor* CameraActor, float BlendTime,
	bool bAutoRestoreOnUIClose, float FadeOutTime)
{
	if (!IsValid(CameraActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: FocusOnLevelCamera sem câmera válida."));
		return;
	}

	APlayerController* PC = ResolvePlayerController();
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: FocusOnLevelCamera sem PlayerController utilizável."));
		return;
	}

	// A câmera gerenciada não participa — para de seguir o que estava seguindo
	if (ManagedCamera.IsValid())
	{
		ManagedCamera->ClearMode();
	}

	PC->SetViewTargetWithBlend(CameraActor, BlendTime, VTBlend_Cubic);

	bFocusActive = true;
	FocusWorld = PC->GetWorld();
	bAutoRestoreFlag = bAutoRestoreOnUIClose;
	PendingRestoreBlendTime = FadeOutTime;
	CurrentFocusActor = CameraActor;

	OnCameraFocusChanged.Broadcast(CameraActor, TEXT("LevelCamera"));
}

void UCameraSubsystem::RestoreToPlayerCamera(float BlendTime)
{
	if (!bFocusActive)
	{
		return;
	}

	if (APlayerController* PC = ResolvePlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, BlendTime, VTBlend_Cubic);
		}
		else
		{
			// Sem pawn (janela de travel/possessão) — devolve ao próprio PC
			UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: Restore sem pawn possuído — view target no PlayerController."));
			PC->SetViewTargetWithBlend(PC, BlendTime, VTBlend_Cubic);
		}
	}

	if (ManagedCamera.IsValid())
	{
		ManagedCamera->ClearMode();
	}

	bFocusActive = false;
	bConversationActive = false;
	bAutoRestoreFlag = false;
	CurrentFocusActor = nullptr;
	ConversationNPC = nullptr;
	ConversationPlayer = nullptr;

	OnCameraRestored.Broadcast();
}

FCameraPresetRow UCameraSubsystem::GetFramingPreset(ECameraFramingPreset Preset) const
{
	return ResolvePreset(Preset);
}

void UCameraSubsystem::SetCameraPresetTable(UDataTable* PresetTable)
{
	CameraPresetTable = PresetTable;
	UE_LOG(LogTemp, Log, TEXT("CameraSubsystem: DataTable de presets %s."),
		PresetTable ? *PresetTable->GetName() : TEXT("removida"));
}

// ============================================================
// DIÁLOGO
// ============================================================

void UCameraSubsystem::StartConversation(AActor* NPC, AActor* PlayerOverride, float BlendTime)
{
	if (!IsValid(NPC))
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: StartConversation sem NPC."));
		return;
	}

	AActor* PlayerActor = PlayerOverride;
	if (!PlayerActor)
	{
		APlayerController* PC = ResolvePlayerController();
		PlayerActor = PC ? static_cast<AActor*>(PC->GetPawn()) : nullptr;
	}
	if (!PlayerActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: StartConversation sem ator do player — FocusSpeaker(false) não vai funcionar."));
	}

	ConversationNPC = NPC;
	ConversationPlayer = PlayerActor;
	bConversationActive = true;

	// Abertura da conversa: enquadra o NPC. Auto-restore ligado — o diálogo
	// roda pela WebUI (DialogueActive), então fechar a UI também limpa a câmera
	FocusInternal(NPC, ResolvePreset(ECameraFramingPreset::Conversation),
		JRPGCameraPresets::FramingPresetToName(ECameraFramingPreset::Conversation), BlendTime, true,
		DefaultRestoreBlendTime);
}

void UCameraSubsystem::FocusSpeaker(bool bNPCSpeaking, float BlendTime)
{
	if (!bConversationActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: FocusSpeaker sem conversa ativa — chame StartConversation antes."));
		return;
	}

	AActor* Speaker = bNPCSpeaking ? ConversationNPC.Get() : ConversationPlayer.Get();
	AActor* Listener = bNPCSpeaking ? ConversationPlayer.Get() : ConversationNPC.Get();
	if (!Speaker || !Listener)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: FocusSpeaker — participante da conversa destruído."));
		return;
	}

	// Shot-reverse-shot: a câmera fica no eixo ouvinte→falante (por trás do
	// ouvinte), deslocada ~25° para o lado. O sinal alterna com o falante para
	// os dois shots ficarem do MESMO lado da linha de ação (regra dos 180°)
	FCameraPresetRow Preset = ResolvePreset(ECameraFramingPreset::OverShoulder);

	const FVector Axis = Speaker->GetActorLocation() - Listener->GetActorLocation();
	const float AxisYaw = Axis.Rotation().Yaw;
	const float LateralDeg = bNPCSpeaking ? 25.0f : -25.0f;

	// YawDeg do preset é relativo ao facing do alvo (falante)
	Preset.YawDeg = (AxisYaw + LateralDeg) - Speaker->GetActorRotation().Yaw;

	FocusInternal(Speaker, Preset,
		JRPGCameraPresets::FramingPresetToName(ECameraFramingPreset::OverShoulder), BlendTime, true,
		DefaultRestoreBlendTime);
}

void UCameraSubsystem::EndConversation(float BlendTime)
{
	if (!bConversationActive)
	{
		return;
	}
	RestoreToPlayerCamera(BlendTime); // já limpa o estado da conversa
}

// ============================================================
// BATALHA / GRUPO
// ============================================================

void UCameraSubsystem::FrameGroup(const TArray<AActor*>& Actors, float BlendTime, float Padding,
	ECameraFramingPreset BasePreset, bool bAutoRestoreOnUIClose)
{
	bool bAnyValid = false;
	for (AActor* Actor : Actors)
	{
		if (IsValid(Actor))
		{
			bAnyValid = true;
			break;
		}
	}
	if (!bAnyValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: FrameGroup sem nenhum ator válido."));
		return;
	}

	APlayerController* PC = ResolvePlayerController();
	AJRPGCameraActor* Camera = EnsureManagedCamera();
	if (!PC || !Camera)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: FrameGroup sem PlayerController/câmera utilizável."));
		return;
	}

	Camera->SetupGroup(Actors, ResolvePreset(BasePreset), Padding);
	PC->SetViewTargetWithBlend(Camera, BlendTime, VTBlend_Cubic);

	bFocusActive = true;
	FocusWorld = PC->GetWorld();
	bAutoRestoreFlag = bAutoRestoreOnUIClose;
	PendingRestoreBlendTime = DefaultRestoreBlendTime;
	CurrentFocusActor = nullptr; // grupo, não um ator só

	OnCameraFocusChanged.Broadcast(nullptr, JRPGCameraPresets::FramingPresetToName(BasePreset));
}

void UCameraSubsystem::OrbitAroundActor(AActor* TargetActor, float DegreesPerSecond,
	ECameraFramingPreset Preset, float BlendTime)
{
	// Battle camera: auto-restore desligado — a batalha controla o ciclo
	FocusInternal(TargetActor, ResolvePreset(Preset), JRPGCameraPresets::FramingPresetToName(Preset),
		BlendTime, false, DefaultRestoreBlendTime);

	if (bFocusActive && ManagedCamera.IsValid())
	{
		ManagedCamera->StartOrbit(DegreesPerSecond);
	}
}

void UCameraSubsystem::StopOrbit()
{
	if (ManagedCamera.IsValid())
	{
		ManagedCamera->StopOrbit();
	}
}

// ============================================================
// CAMERA SHAKE
// ============================================================

void UCameraSubsystem::PlayCameraShake(ECameraShakePreset Preset, float Scale)
{
	if (UJRPGCameraShakeModifier* Modifier = EnsureShakeModifier())
	{
		Modifier->AddShake(JRPGCameraPresets::GetShakePreset(Preset), Scale, Preset);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: PlayCameraShake sem PlayerCameraManager utilizável."));
	}
}

void UCameraSubsystem::PlayCameraShakeCustom(const FJRPGCameraShakeParams& Params, float Scale)
{
	if (UJRPGCameraShakeModifier* Modifier = EnsureShakeModifier())
	{
		Modifier->AddShake(Params, Scale, ECameraShakePreset::Custom);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: PlayCameraShakeCustom sem PlayerCameraManager utilizável."));
	}
}

void UCameraSubsystem::StopAllCameraShakes(bool bImmediate)
{
	if (ShakeModifier.IsValid())
	{
		ShakeModifier->StopAll(bImmediate);
	}
}

FJRPGCameraShakeParams UCameraSubsystem::GetShakePresetParams(ECameraShakePreset Preset) const
{
	return JRPGCameraPresets::GetShakePreset(Preset);
}

void UCameraSubsystem::NotifyShakeFinished(ECameraShakePreset Preset)
{
	OnCameraShakeFinished.Broadcast(Preset);
}

// ============================================================
// INTERNOS
// ============================================================

APlayerController* UCameraSubsystem::ResolvePlayerController() const
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	return World ? GI->GetFirstLocalPlayerController(World) : nullptr;
}

AJRPGCameraActor* UCameraSubsystem::EnsureManagedCamera()
{
	if (ManagedCamera.IsValid())
	{
		return ManagedCamera.Get();
	}

	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient; // nunca salvo — morre com o mapa

	AJRPGCameraActor* Camera = World->SpawnActor<AJRPGCameraActor>(SpawnParams);
	if (Camera)
	{
		Camera->OnFollowTargetLost.AddUObject(this, &UCameraSubsystem::HandleFocusTargetLost);
		ManagedCamera = Camera;
	}
	return Camera;
}

UJRPGCameraShakeModifier* UCameraSubsystem::EnsureShakeModifier()
{
	APlayerController* PC = ResolvePlayerController();
	APlayerCameraManager* PCM = PC ? PC->PlayerCameraManager : nullptr;
	if (!PCM)
	{
		return nullptr;
	}

	// O PlayerCameraManager é recriado por mapa/PIE — sempre pergunta ao PCM
	// ATUAL se ele já tem o nosso modifier; senão adiciona um novo nele
	UJRPGCameraShakeModifier* Modifier = Cast<UJRPGCameraShakeModifier>(
		PCM->FindCameraModifierByClass(UJRPGCameraShakeModifier::StaticClass()));
	if (!Modifier)
	{
		Modifier = Cast<UJRPGCameraShakeModifier>(
			PCM->AddNewCameraModifier(UJRPGCameraShakeModifier::StaticClass()));
	}

	if (Modifier)
	{
		Modifier->OwnerSubsystem = this;
		ShakeModifier = Modifier;
	}
	return Modifier;
}

void UCameraSubsystem::FocusInternal(AActor* Target, const FCameraPresetRow& Preset, FName PresetID,
	float BlendTime, bool bAutoRestore, float FadeOutTime)
{
	if (!IsValid(Target))
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: foco sem ator válido (preset '%s')."), *PresetID.ToString());
		return;
	}

	APlayerController* PC = ResolvePlayerController();
	AJRPGCameraActor* Camera = EnsureManagedCamera();
	if (!PC || !Camera)
	{
		UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: foco sem PlayerController/câmera utilizável."));
		return;
	}

	Camera->SetupFocus(Target, Preset);
	PC->SetViewTargetWithBlend(Camera, BlendTime, VTBlend_Cubic);

	// Focos empilhados: a última chamada ganha (câmera única reusada)
	bFocusActive = true;
	FocusWorld = PC->GetWorld();
	bAutoRestoreFlag = bAutoRestore;
	PendingRestoreBlendTime = FadeOutTime;
	CurrentFocusActor = Target;

	OnCameraFocusChanged.Broadcast(Target, PresetID);
}

FCameraPresetRow UCameraSubsystem::ResolvePreset(ECameraFramingPreset Preset) const
{
	// Row da DataTable com o nome do enum sobrescreve o built-in
	if (CameraPresetTable)
	{
		const FName RowName = JRPGCameraPresets::FramingPresetToName(Preset);
		if (const FCameraPresetRow* Row = CameraPresetTable->FindRow<FCameraPresetRow>(
			RowName, TEXT("ResolvePreset"), false))
		{
			return *Row;
		}
	}
	return JRPGCameraPresets::GetBuiltIn(Preset);
}

void UCameraSubsystem::HandleUIStateChanged(EJRPGUIState OldState, EJRPGUIState NewState)
{
	// Auto-restore: uma tela fechou de volta para o HUD com foco ativo e
	// flag ligada — devolve a câmera para o player
	if (NewState == EJRPGUIState::HudOnly && OldState != EJRPGUIState::HudOnly
		&& bFocusActive && bAutoRestoreFlag)
	{
		// FadeOutTime gravado pelo foco (0 = corte seco)
		RestoreToPlayerCamera(PendingRestoreBlendTime);
	}
}

void UCameraSubsystem::HandleFocusTargetLost()
{
	UE_LOG(LogTemp, Warning, TEXT("CameraSubsystem: alvo do foco destruído — restaurando a câmera do player."));
	RestoreToPlayerCamera(PendingRestoreBlendTime);
}

void UCameraSubsystem::HandlePostLoadMap(UWorld* NewWorld)
{
	// Tudo do mundo velho morreu: registry, câmera gerenciada e shake modifier.
	// Só zera os ponteiros (weak) — NUNCA destruir objetos do mundo antigo aqui.
	// O PC novo já nasce com view target no pawn, então não precisa de restore.
	TargetRegistry.Empty();
	ShakeModifier = nullptr;

	// Um foco feito JÁ neste mapa é válido e fica: o PostLoadMap roda no fim do
	// carregamento, depois do BeginPlay — o jogador que nasce dentro de uma
	// JRPGCameraZone já ligou a câmera dela. Zerar aqui deixava o "voltar para o
	// player" sem efeito, e a câmera presa na zona até entrar e sair de novo
	if (bFocusActive && FocusWorld.Get() == NewWorld)
	{
		if (bAutoDetectTargetsByTag)
		{
			ScanWorldForCameraTargets();
		}
		return;
	}

	ManagedCamera = nullptr;
	CurrentFocusActor = nullptr;
	ConversationNPC = nullptr;
	ConversationPlayer = nullptr;
	bFocusActive = false;
	bConversationActive = false;
	bAutoRestoreFlag = false;

	if (bAutoDetectTargetsByTag)
	{
		ScanWorldForCameraTargets();
	}
}
