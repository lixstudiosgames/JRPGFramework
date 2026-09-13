#include "UI/WebUISubsystem.h"
#include "UI/WebContainerWidget.h"
#include "UI/WebUIBridge.h"
#include "UI/WebUIScripting.h"
#include "UI/JRPGWebBrowser.h"
#include "Save/SaveSubsystem.h"
#include "Save/SaveTypes.h"
#include "Core/CoreSubsystem.h"
#include "Shop/ShopSubsystem.h"
#include "Shop/ShopData.h"
#include "World/WorldStateSubsystem.h"
#include "Party/PartySubsystem.h"
#include "Party/CharacterData.h"
#include "Inventory/InventorySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/UltralightRenderThread.h"
#include "HAL/RunnableThread.h"
#include "RenderingThread.h"
#include "Misc/CoreDelegates.h"
#include "HAL/IConsoleManager.h"
#include "Engine/UserInterfaceSettings.h"
#include "RHI.h"
#include "DynamicRHI.h"

// DXGI/D3D11 — usados apenas para descobrir o LUID do adapter do device da UE
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

// Thread e render globais — singleton de processo (sobrevivem entre PIE sessions)
static FUltralightRenderThread* GRenderThread = nullptr;
static FRunnableThread* GThreadHandle = nullptr;

// Página única do shell (SPA). A View global do Ultralight preserva o
// documento entre sessões PIE; este guard (game-thread only, singleton de
// processo como GRenderThread) evita LoadURL redundante no PIE 2+ — o zero
// no processo novo (editor reaberto) força o load inicial naturalmente.
static const TCHAR* GShellPage = TEXT("shell.html");
static FString GLastLoadedShellPage;

UWebUISubsystem::UWebUISubsystem()
	: GameVersion(TEXT("v0.1.0"))
	, GameBuild(TEXT("Build 2026.08"))
	, WebContainerWidgetClass(nullptr)
	, WebContainerWidget(nullptr)
	, Bridge(nullptr)
	, bShellReady(false)
	, CurrentState(EJRPGUIState::HudOnly)
	, DLLHandleCore(nullptr)
	, DLLHandleUl(nullptr)
	, DLLHandleWeb(nullptr)
	, DLLHandleApp(nullptr)
{
}

void UWebUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cria a ponte de comunicação primeiro — GetBridge() nunca retorna nulo,
	// mesmo se o carregamento das DLLs falhar abaixo
	Bridge = NewObject<UWebUIBridge>(this);
	Bridge->Initialize(this);

	// Desativa o anel/borda azul de foco padrão do Slate para a UI HTML.
	// Feito 1x aqui (era refeito a cada NativeConstruct do container).
	// NOTA: efeito colateral global no projeto enquanto o plugin estiver ativo.
	if (UUserInterfaceSettings* UISettings = GetMutableDefault<UUserInterfaceSettings>())
	{
		UISettings->RenderFocusRule = ERenderFocusRule::Never;
	}

	// 1. Carregar as DLLs do Ultralight dinamicamente para evitar falhas em PIE
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("JRPGFramework"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: Plugin 'JRPGFramework' nao encontrado — WebUI desabilitada."));
		return;
	}
	FString PluginDir = Plugin->GetBaseDir();
	FString BinariesDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginDir, TEXT("Content"), TEXT("ThirdParty"), TEXT("Ultralight"), TEXT("bin")));
	FPaths::MakePlatformFilename(BinariesDir);

	// Adiciona temporariamente a pasta das DLLs ao caminho de busca do OS para resolver dependências transitivas
	FPlatformProcess::PushDllDirectory(*BinariesDir);

	// Carrega na ordem correta de dependência:
	// 1. UltralightCore (sem dependências internas)
	// 2. WebCore (depende de UltralightCore)
	// 3. Ultralight (depende de WebCore e UltralightCore)
	// 4. AppCore (depende de todos os anteriores)
	DLLHandleCore = FPlatformProcess::GetDllHandle(*FPaths::Combine(BinariesDir, TEXT("UltralightCore.dll")));
	DLLHandleWeb  = FPlatformProcess::GetDllHandle(*FPaths::Combine(BinariesDir, TEXT("WebCore.dll")));
	DLLHandleUl   = FPlatformProcess::GetDllHandle(*FPaths::Combine(BinariesDir, TEXT("Ultralight.dll")));
	DLLHandleApp  = FPlatformProcess::GetDllHandle(*FPaths::Combine(BinariesDir, TEXT("AppCore.dll")));

	FPlatformProcess::PopDllDirectory(*BinariesDir);

	if (DLLHandleCore && DLLHandleUl && DLLHandleWeb && DLLHandleApp)
	{
		UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: DLLs do Ultralight carregadas com sucesso de '%s'."), *BinariesDir);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: Falha ao carregar as DLLs do Ultralight (Core=%s, Web=%s, UI=%s, App=%s)."),
			DLLHandleCore ? TEXT("OK") : TEXT("Erro"),
			DLLHandleWeb ? TEXT("OK") : TEXT("Erro"),
			DLLHandleUl ? TEXT("OK") : TEXT("Erro"),
			DLLHandleApp ? TEXT("OK") : TEXT("Erro"));
	}

	// 2. Criar e iniciar a thread dedicada de renderizacao do Ultralight apenas se as DLLs carregaram corretamente
	// A thread, Renderer, GPUDriver e View sobrevivem entre PIE sessions (singletons de processo).
	if ((DLLHandleCore && DLLHandleUl && DLLHandleWeb && DLLHandleApp) && !GThreadHandle)
	{
		GRenderThread = new FUltralightRenderThread();
		GRenderThread->PluginDir = PluginDir;
		GRenderThread->ViewWidth = 1920;
		GRenderThread->ViewHeight = 1080;
		GRenderThread->bTransparent = true;
		GRenderThread->InitialURL = TEXT(""); // Inicialmente vazia, controlada depois pelo widget

		// Descobre o LUID do adapter do device da UE para que o device D3D11 do
		// Ultralight seja criado na MESMA GPU — obrigatório para shared handles
		// funcionarem em máquinas multi-GPU (notebooks iGPU + dGPU)
		if (GDynamicRHI && FString(GDynamicRHI->GetName()).Equals(TEXT("D3D11"), ESearchCase::IgnoreCase))
		{
			ID3D11Device* UEDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			if (UEDevice)
			{
				Microsoft::WRL::ComPtr<IDXGIDevice> DXGIDevice;
				Microsoft::WRL::ComPtr<IDXGIAdapter> DXGIAdapter;
				if (SUCCEEDED(UEDevice->QueryInterface(IID_PPV_ARGS(&DXGIDevice))) &&
					SUCCEEDED(DXGIDevice->GetAdapter(DXGIAdapter.GetAddressOf())))
				{
					DXGI_ADAPTER_DESC AdapterDesc = {};
					if (SUCCEEDED(DXGIAdapter->GetDesc(&AdapterDesc)))
					{
						GRenderThread->bHasAdapterLuid = true;
						GRenderThread->AdapterLuidHigh = AdapterDesc.AdapterLuid.HighPart;
						GRenderThread->AdapterLuidLow = AdapterDesc.AdapterLuid.LowPart;
					}
				}
			}
		}

		// Prioridade AboveNormal: responsiva para UI sem competir com as threads
		// de game/render da engine (TimeCritical starvava CPUs com poucos cores)
		GThreadHandle = FRunnableThread::Create(GRenderThread, TEXT("UltralightRenderThread"), 0, TPri_AboveNormal);
		if (GThreadHandle)
		{
			UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Thread dedicada de render (UltralightRenderThread) criada com sucesso."));

			// Shutdown definitivo no exit do engine: para a UL thread e destrói os
			// singletons do Ultralight ANTES do teardown das DLLs (registrado 1x por processo)
			static bool bExitHookRegistered = false;
			if (!bExitHookRegistered)
			{
				bExitHookRegistered = true;
				FCoreDelegates::OnEnginePreExit.AddLambda([]()
				{
					if (GRenderThread && GThreadHandle)
					{
						// Garante que comandos de render pendentes (que capturam GRenderThread)
						// terminem antes de parar a thread
						FlushRenderingCommands();

						// Para a UL thread e destrói View/Renderer/GPUDriver dentro dela
						// (antes do teardown das DLLs do Ultralight no exit do processo).
						// GRenderThread/GThreadHandle NÃO são deletados de propósito:
						// widgets Slate ainda vivos no teardown guardam esse ponteiro em
						// cache — manter o objeto (inerte) evita dangling pointers. O leak
						// é irrelevante: o processo está encerrando.
						GRenderThread->RequestFinalShutdown();
						GThreadHandle->WaitForCompletion();

						UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: UltralightRenderThread finalizada no exit do engine."));
					}
				});
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: Falha ao criar a thread dedicada de render (UltralightRenderThread)."));
		}
	}
	else if (!GThreadHandle)
	{
		UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: Ignorando a criacao da thread de render do Ultralight pois as DLLs nao foram carregadas."));
	}

	// Remonta o shell após trocas de mapa (OpenLevel remove os widgets do
	// viewport e destrói o PlayerController dono do widget)
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UWebUISubsystem::HandlePostLoadMap);

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Subsistema de WebUI inicializado com Ultralight."));
}

void UWebUISubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	// Antes do TeardownWidget: ele retorna cedo se o widget já sumiu, e o cap
	// não pode sobreviver ao fim da sessão.
	RestoreFrameRate();

	TeardownWidget();

	// Garantir que os comandos de limpeza RHI (ENQUEUE_RENDER_COMMAND do destrutor do Slate)
	// executem antes da proxima sessao PIE
	FlushRenderingCommands();

	// Desvincula o Bridge da UL thread e AGUARDA o comando ser processado:
	// garante que o load listener (que referencia o Bridge, um UObject) seja
	// destruído na UL thread antes do Bridge poder ser coletado pelo GC
	if (GRenderThread)
	{
		FULThreadCommand Cmd;
		Cmd.Type = EULCommandType::BindBridge;
		Cmd.Bridge = nullptr;
		GRenderThread->EnqueueCommand(MoveTemp(Cmd));
		GRenderThread->FlushCommands(0.25f);
	}

	// Limpar ponte global JS antes de destruir o Bridge (evita dangling pointer no GActiveBridge)
	if (Bridge)
	{
		Bridge->ClearActiveBridge();
	}
	Bridge = nullptr;

	// Thread de render NAO e parada — o Ultralight nao suporta teardown/recreation.
	// A thread, Renderer, GPUDriver e View sobrevivem entre PIE sessions como singletons de processo.
	
	Super::Deinitialize();
}

// ============================================================
// API NOVA (Shell)
// ============================================================

void UWebUISubsystem::InitializeUIShell()
{
	EnsureShellWidget();
}

void UWebUISubsystem::ShowItemPopup(FText ItemName, int32 ItemQty)
{
	if (!EnsureShellWidget())
	{
		return;
	}

	// Overlay passivo: injeta os dados direto na UI carregada, sem reload
	// e sem tocar em input/foco/cursor — o jogador continua controlando o jogo.
	DispatchJS(FString::Printf(TEXT("showItemPopup(%s, %d);"),
		*JRPGWebUI::ToJSStringLiteral(ItemName.ToString()), ItemQty));
}

void UWebUISubsystem::OpenMenu()
{
	if (CurrentState == EJRPGUIState::MenuOpen)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: OpenMenu ignorado — o menu já está aberto."));
		return;
	}
	if (CurrentState != EJRPGUIState::HudOnly)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: OpenMenu ignorado — estado atual '%s' não permite abrir o menu."),
			UIStateToJS(CurrentState));
		return;
	}
	if (!EnsureShellWidget())
	{
		return;
	}

	ApplyUIFocus();
	SetStateInternal(EJRPGUIState::MenuOpen);

	// Gold e tempo de jogo reais do CoreSubsystem antes do menu aparecer.
	// O playtime vai em SEGUNDOS crus — o JS formata e mantém os segundos
	// subindo ao vivo (ticker de 1s) enquanto o menu estiver aberto.
	if (UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>())
	{
		DispatchJS(FString::Printf(TEXT("updateGoldTime(%d, %.3f);"),
			Core->GetGold(),
			Core->GetPlaytimeSeconds()));
	}

	// Os cards de status são montados a partir da FORMAÇÃO: se só o Vahn está
	// ativo, aparece um card só. Antes eram três <div> fixos no HTML.
	DispatchJS(FString::Printf(TEXT("JRPGSetParty(%s);"), *BuildMenuPartyJS()));

	// Gravar só é permitido no Field: o JS apaga a opção Save quando é false.
	DispatchJS(FString::Printf(TEXT("JRPGUI.setCanSave(%s);"),
		CanSaveNow() ? TEXT("true") : TEXT("false")));

	// O JS revalida via UIState.canOpen (defesa em profundidade)
	DispatchJS(TEXT("JRPGUI.openMenu();"));
}

void UWebUISubsystem::CloseMenu()
{
	if (CurrentState != EJRPGUIState::MenuOpen)
	{
		return;
	}

	if (bShellReady && WebContainerWidget)
	{
		// O JS anima a saída (~350ms) e chama bridge.closemenu() ao terminar,
		// que cai em HandleCloseRequestFromJS() — caminho único de fechamento,
		// compartilhado com o Escape/botão B.
		DispatchJS(TEXT("JRPGUI.closeMenu();"));
	}
	else
	{
		// Fallback: shell indisponível — restaura o input do jogo direto
		HandleCloseRequestFromJS();
	}
}

void UWebUISubsystem::OpenDialogue(FText SpeakerName, FText DialogueText, const TArray<FText>& Options)
{
	if (!EnsureShellWidget())
	{
		return;
	}

	// Monta o array JS de opções com cada string devidamente escapada
	FString OptionsLiteral = TEXT("[");
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		if (i > 0)
		{
			OptionsLiteral += TEXT(", ");
		}
		OptionsLiteral += JRPGWebUI::ToJSStringLiteral(Options[i].ToString());
	}
	OptionsLiteral += TEXT("]");

	// PLACEHOLDER: o openDialogue do JS ainda só loga. Quando o sistema de
	// diálogo for implementado, este fluxo deve passar a chamar
	// SetUIState(DialogueActive) e gerenciar o foco.
	DispatchJS(FString::Printf(TEXT("openDialogue(%s, %s, %s);"),
		*JRPGWebUI::ToJSStringLiteral(SpeakerName.ToString()),
		*JRPGWebUI::ToJSStringLiteral(DialogueText.ToString()),
		*OptionsLiteral));
}

void UWebUISubsystem::OpenOptions()
{
	// A origem decide para onde a tela volta ao fechar (o JS cuida do resto)
	const TCHAR* Origin = nullptr;
	switch (CurrentState)
	{
	case EJRPGUIState::HudOnly:      Origin = TEXT("hud");      break;
	case EJRPGUIState::MenuOpen:     Origin = TEXT("menu");     break;
	case EJRPGUIState::MainMenuOpen: Origin = TEXT("mainmenu"); break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: OpenOptions ignorado — estado atual '%s' não permite abrir as opções."),
			UIStateToJS(CurrentState));
		return;
	}

	if (!EnsureShellWidget())
	{
		return;
	}

	// Vindo do menu de pausa ou do main menu a UI JÁ está com o foco — só o
	// caminho direto do jogo precisa tomar o input.
	if (CurrentState == EJRPGUIState::HudOnly)
	{
		ApplyUIFocus();
	}

	SetStateInternal(EJRPGUIState::OptionsOpen);

	// Sincroniza a dificuldade mostrada com a do CoreSubsystem antes de a tela
	// aparecer (a lista do JS casa com a ordem de EJRPGDifficulty).
	if (const UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>())
	{
		DispatchJS(FString::Printf(TEXT("JRPGOptions.setDifficulty(%d);"),
			static_cast<int32>(Core->GetDifficulty())));
	}

	// O JS revalida via UIState.canOpen (defesa em profundidade)
	DispatchJS(FString::Printf(TEXT("openOptionsScreen('%s');"), Origin));
}

void UWebUISubsystem::OpenMainMenu()
{
	if (CurrentState == EJRPGUIState::MainMenuOpen)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: OpenMainMenu ignorado — o main menu já está aberto."));
		return;
	}
	if (CurrentState != EJRPGUIState::HudOnly)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: OpenMainMenu ignorado — estado atual '%s' não permite abrir o main menu."),
			UIStateToJS(CurrentState));
		return;
	}
	if (!EnsureShellWidget())
	{
		return;
	}

	ApplyUIFocus();
	SetStateInternal(EJRPGUIState::MainMenuOpen);

	// "Continue" fica desabilitado quando não existe nenhum save gravado
	bool bHasSaves = false;
	if (USaveSubsystem* SaveSubsystem = GetGameInstance()->GetSubsystem<USaveSubsystem>())
	{
		for (int32 SlotIndex = 0; SlotIndex < USaveSubsystem::NumSaveSlots; ++SlotIndex)
		{
			if (SaveSubsystem->DoesSlotExist(SlotIndex))
			{
				bHasSaves = true;
				break;
			}
		}
	}

	// O JS revalida via UIState.canOpen (defesa em profundidade)
	DispatchJS(FString::Printf(TEXT("JRPGMainMenu.open({hasSaves:%s,version:%s,build:%s});"),
		bHasSaves ? TEXT("true") : TEXT("false"),
		*JRPGWebUI::ToJSStringLiteral(GameVersion),
		*JRPGWebUI::ToJSStringLiteral(GameBuild)));
}

void UWebUISubsystem::OpenSaveLoad()
{
	OpenSaveLoadWith(TEXT("open"), TEXT(""));
}

void UWebUISubsystem::OpenSaveScreen()
{
	OpenSaveLoadWith(TEXT("openSave"), TEXT(""));
}

void UWebUISubsystem::OpenLoadScreen()
{
	OpenSaveLoadWith(TEXT("openLoad"), TEXT(""));
}

void UWebUISubsystem::OpenSaveLoadFromMenu(bool bLoadTab)
{
	// Gravar pelo menu de pausa só é permitido no Field
	// (UWorldStateSubsystem::SetIsInField). O JS já apaga a opção Save quando
	// CanSaveNow() é false — isto aqui é a defesa em profundidade.
	// Carregar continua liberado em qualquer contexto, e a estátua de save
	// (OpenSaveLoad) não passa por este gate: ela só existe no Field.
	if (!bLoadTab && !CanSaveNow())
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: Save pelo menu recusado — fora do Field (bIsInField = false)."));
		return;
	}

	OpenSaveLoadWith(bLoadTab ? TEXT("openLoad") : TEXT("openSave"), TEXT(",{returnTo:'menu'}"));
}

bool UWebUISubsystem::CanSaveNow() const
{
	const UWorldStateSubsystem* WorldState = GetGameInstance()->GetSubsystem<UWorldStateSubsystem>();
	return WorldState && WorldState->IsInField();
}

void UWebUISubsystem::OpenSaveLoadWith(const TCHAR* JSFunction, const TCHAR* ExtraOpts)
{
	if (CurrentState == EJRPGUIState::SaveLoadOpen)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: JRPGSaveLoad.%s ignorado — a tela de save já está aberta."), JSFunction);
		return;
	}

	// MenuOpen é aceito porque o menu de pausa abre Save/Load por cima de si
	// mesmo (o JS já escondeu os cards) e volta para o menu ao cancelar.
	if (CurrentState != EJRPGUIState::HudOnly && CurrentState != EJRPGUIState::MenuOpen)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: JRPGSaveLoad.%s ignorado — estado atual '%s' não permite abrir a tela de save."),
			JSFunction, UIStateToJS(CurrentState));
		return;
	}

	if (!EnsureShellWidget())
	{
		return;
	}

	ApplyUIFocus();
	SetStateInternal(EJRPGUIState::SaveLoadOpen);

	// O JS revalida via UIState.canOpen (defesa em profundidade)
	DispatchJS(FString::Printf(TEXT("JRPGSaveLoad.%s(%s%s);"),
		JSFunction, *BuildSaveSlotsPayloadJS(), ExtraOpts));
}

void UWebUISubsystem::QuitToMainMenu()
{
	UGameInstance* GI = GetGameInstance();

	// Congela o cronômetro: sem isso ele continua correndo na tela de título e
	// infla o tempo do próximo save se o jogador der Continue.
	if (UCoreSubsystem* Core = GI->GetSubsystem<UCoreSubsystem>())
	{
		Core->StopPlaytimeTracking();
	}

	// O título não é Field: trava o Save até o próximo mapa declarar o contexto.
	if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
	{
		WorldState->SetIsInField(false);
	}

	// O resto do estado (inventário, gold, world state) NÃO é limpo de
	// propósito: New Game chama StartNewGame() e o Continue restaura por cima.

	// Input de volta para o jogo ANTES do OpenLevel: o FInputModeUIOnly
	// referencia o Slate widget que o LoadMap vai destruir (mesma ordem
	// crítica do Load de save e do New Game).
	ApplyGameFocus();
	SetStateInternal(EJRPGUIState::HudOnly);

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Quit para o main menu — abrindo '%s'."), *MainMenuLevelName.ToString());
	UGameplayStatics::OpenLevel(GI->GetWorld(), MainMenuLevelName);
}

void UWebUISubsystem::SetDifficultyFromUI(int32 DifficultyIndex)
{
	UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>();
	if (!Core)
	{
		return;
	}

	// O índice vem da lista da tela de opções e casa com a ordem do enum.
	const UEnum* EnumPtr = StaticEnum<EJRPGDifficulty>();
	if (!EnumPtr || DifficultyIndex < 0 || DifficultyIndex >= EnumPtr->NumEnums() - 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: dificuldade %d fora do intervalo — ignorada."), DifficultyIndex);
		return;
	}

	Core->SetDifficulty(static_cast<EJRPGDifficulty>(DifficultyIndex));
}

void UWebUISubsystem::SetUICursorVisible(bool bVisible)
{
	// Só faz sentido com a UI no ar: no jogo o cursor já fica escondido pelo
	// ApplyGameFocus, e mexer aqui atropelaria isso.
	if (CurrentState == EJRPGUIState::HudOnly)
	{
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;
	if (!PC || PC->bShowMouseCursor == bVisible)
	{
		return;
	}

	// Só a visibilidade: o InputMode continua UIOnly, então o clique volta a
	// funcionar assim que o ponteiro reaparecer.
	PC->bShowMouseCursor = bVisible;
}

void UWebUISubsystem::OpenShop(FName ShopID)
{
	if (CurrentState != EJRPGUIState::HudOnly)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: OpenShop ignorado — estado atual '%s' não permite abrir a loja."),
			UIStateToJS(CurrentState));
		return;
	}

	UShopSubsystem* ShopSubsystem = GetGameInstance()->GetSubsystem<UShopSubsystem>();
	if (!ShopSubsystem || !ShopSubsystem->IsShopAvailable(ShopID))
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: OpenShop('%s') ignorado — loja inexistente ou indisponível (verifique IsShopAvailable no BP)."),
			*ShopID.ToString());
		return;
	}

	if (!EnsureShellWidget())
	{
		return;
	}

	ApplyUIFocus();
	SetStateInternal(EJRPGUIState::ShopOpen);
	CurrentShopID = ShopID;

	// O JS revalida via UIState.canOpen (defesa em profundidade)
	DispatchJS(FString::Printf(TEXT("JRPGShop.open(%s);"), *BuildShopPayloadJS()));
}

void UWebUISubsystem::SetUIState(EJRPGUIState NewState)
{
	SetStateInternal(NewState);

	if (!EnsureShellWidget())
	{
		return;
	}
	DispatchJS(FString::Printf(TEXT("UIState.set('%s');"), UIStateToJS(NewState)));
}

// ============================================================
// DEV MENU
//
// Tela de teste. Tudo aqui mexe no estado do jogo SEM validacao de gameplay —
// e o ponto: e para testar. O corpo inteiro sai do build em Shipping, entao a
// chamada pode continuar no Blueprint sem risco de ir para o jogo final.
//
// A ponte tem UMA funcao (ondevcommand) que recebe a linha inteira. Comando
// novo = mais um `else if` aqui, sem tocar em WebUIBridge nem no JSC.
// ============================================================

#if !UE_BUILD_SHIPPING
namespace
{
	/** Parte "gold.add 500 x" em {"gold.add", "500", "x"}. */
	void SplitDevCommand(const FString& In, FString& OutVerb, TArray<FString>& OutArgs)
	{
		TArray<FString> Parts;
		In.TrimStartAndEnd().ParseIntoArray(Parts, TEXT(" "), true);
		OutVerb = Parts.Num() > 0 ? Parts[0].ToLower() : FString();
		for (int32 i = 1; i < Parts.Num(); ++i)
		{
			OutArgs.Add(Parts[i]);
		}
	}

	int32 DevArgInt(const TArray<FString>& Args, int32 Index, int32 Fallback = 0)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Fallback;
	}

	FString DevArgStr(const TArray<FString>& Args, int32 Index, const TCHAR* Fallback = TEXT(""))
	{
		return Args.IsValidIndex(Index) ? Args[Index] : FString(Fallback);
	}
}
#endif // !UE_BUILD_SHIPPING

void UWebUISubsystem::OpenDevMenu()
{
#if UE_BUILD_SHIPPING
	// No-op de proposito: ver o comentario acima.
#else
	if (CurrentState != EJRPGUIState::HudOnly)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("WebUISubsystem: OpenDevMenu ignorado — estado atual '%s'. Feche a tela aberta antes."),
			UIStateToJS(CurrentState));
		return;
	}

	if (!EnsureShellWidget())
	{
		return;
	}

	ApplyUIFocus();
	SetStateInternal(EJRPGUIState::DevMenuOpen);
	DispatchJS(TEXT("JRPGDev.open();"));

	// Primeiro retrato do estado assim que abre — e o teste de fumaca das
	// DataTables: se alguma nao foi importada, aparece aqui.
	RunDevCommand(TEXT("info"));
#endif
}

void UWebUISubsystem::RunDevCommand(const FString& Command)
{
#if UE_BUILD_SHIPPING
	// No-op de proposito.
#else
	FString Verb;
	TArray<FString> Args;
	SplitDevCommand(Command, Verb, Args);
	if (Verb.IsEmpty())
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	UWorldStateSubsystem* World = GI ? GI->GetSubsystem<UWorldStateSubsystem>() : nullptr;
	USaveSubsystem* Save = GI ? GI->GetSubsystem<USaveSubsystem>() : nullptr;
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;

	FString Out;

	// ---------------------------------------------------------------- info
	if (Verb == TEXT("info"))
	{
		if (Core)
		{
			const FDifficultyScaling Scaling = Core->GetDifficultyScaling();
			Out += FString::Printf(TEXT("gold %d | playtime %s | mapa %s\n"),
				Core->GetGold(), *Core->GetPlaytimeFormatted(), *Core->GetCurrentMapName());
			Out += FString::Printf(TEXT("teto de level %d (absoluto %d) | caps HP %d MP %d AGL %d\n"),
				Core->GetMaxLevel(), UCoreSubsystem::AbsoluteMaxLevel,
				Core->GetStatCap(TEXT("HP")), Core->GetStatCap(TEXT("MP")),
				Core->GetStatCap(TEXT("AGL")));
			Out += FString::Printf(TEXT("dificuldade %s — inimigo HP x%.2f ATK x%.2f DEF x%.2f | XP x%.2f\n"),
				*UEnum::GetDisplayValueAsText(Core->GetDifficulty()).ToString(),
				Scaling.EnemyHP, Scaling.EnemyATK, Scaling.EnemyDEF, Scaling.XPReward);
			// Mostra o CAMINHO resolvido: e assim que se confere se a DataTable
			// importada e mesmo a que o subsistema pegou.
			auto Onde = [](const UDataTable* T, const TCHAR* Nome, bool bObrigatoria) -> FString
			{
				if (T)
				{
					return FString::Printf(TEXT("  %s  %s\n"), Nome, *T->GetPathName());
				}
				return FString::Printf(TEXT("  %s  %s\n"), Nome,
					bObrigatoria
						? TEXT("AUSENTE — importe o CSV em /Game/Data/ com esse nome")
						: TEXT("ausente (usando os multiplicadores embutidos no C++)"));
			};
			Out += TEXT("tabelas:\n");
			Out += Onde(Core->StatGrowthTable, TEXT("DT_StatGrowth "), true);
			Out += Onde(Core->GrowthCurveTable, TEXT("DT_GrowthCurve"), true);
			Out += Onde(Core->DifficultyTable, TEXT("DT_Difficulty "), false);
		}
		else
		{
			Out += TEXT("CoreSubsystem indisponivel!\n");
		}
		if (World)
		{
			Out += FString::Printf(TEXT("bIsInField %s (aba SAVE %s)\n"),
				World->IsInField() ? TEXT("true") : TEXT("false"),
				CanSaveNow() ? TEXT("liberada") : TEXT("bloqueada"));
		}
		if (Inv)
		{
			Out += FString::Printf(TEXT("inventario: %d slots ocupados"), Inv->GetInventorySlots().Num());
		}
	}
	// ---------------------------------------------------------------- gold
	else if (Verb == TEXT("gold.add") && Core)
	{
		const int32 N = DevArgInt(Args, 0, 100);
		if (N >= 0) { Core->AddGold(N); }
		else        { Core->RemoveGold(-N); }
		Out = FString::Printf(TEXT("gold = %d"), Core->GetGold());
	}
	// --------------------------------------------------------------- itens
	else if (Verb == TEXT("item.add") && Inv)
	{
		const FName Id(*DevArgStr(Args, 0));
		const int32 Qty = DevArgInt(Args, 1, 1);
		const bool bOk = Inv->AddItem(Id, Qty);
		Out = FString::Printf(TEXT("AddItem(%s, %d) = %s — voce tem %d"),
			*Id.ToString(), Qty, bOk ? TEXT("ok") : TEXT("FALHOU"), Inv->GetItemQuantity(Id));
	}
	else if (Verb == TEXT("item.remove") && Inv)
	{
		const FName Id(*DevArgStr(Args, 0));
		const int32 Qty = DevArgInt(Args, 1, 1);
		const bool bOk = Inv->RemoveItem(Id, Qty);
		Out = FString::Printf(TEXT("RemoveItem(%s, %d) = %s — voce tem %d"),
			*Id.ToString(), Qty, bOk ? TEXT("ok") : TEXT("FALHOU"), Inv->GetItemQuantity(Id));
	}
	// --------------------------------------------------------- progressao
	else if (Verb == TEXT("diff.set") && Core)
	{
		const int32 Idx = FMath::Clamp(DevArgInt(Args, 0, 0), 0, 2);
		Core->SetDifficulty(static_cast<EJRPGDifficulty>(Idx));
		const FDifficultyScaling Scaling = Core->GetDifficultyScaling();
		Out = FString::Printf(TEXT("dificuldade = %s — HP x%.2f ATK x%.2f DEF x%.2f"),
			*UEnum::GetDisplayValueAsText(Core->GetDifficulty()).ToString(),
			Scaling.EnemyHP, Scaling.EnemyATK, Scaling.EnemyDEF);
	}
	else if (Verb == TEXT("prog.stat") && Core)
	{
		const FName Who(*DevArgStr(Args, 0, TEXT("Vahn")));
		const FName Stat(*DevArgStr(Args, 1, TEXT("HP")));
		const int32 Level = FMath::Clamp(DevArgInt(Args, 2, 1), 1, Core->GetMaxLevel());
		Out = FString::Printf(TEXT("%s %s no L%d = %d  (ganho do proximo level: +%d)\n"),
			*Who.ToString(), *Stat.ToString(), Level,
			Core->GetStatAtLevel(Who, Stat, Level),
			Core->GetStatGainCore(Who, Stat, Level));
		Out += FString::Printf(TEXT("cresce na dificuldade atual: %s"),
			Core->DoesCharacterGrow(Who) ? TEXT("sim") : TEXT("NAO"));
	}
	else if (Verb == TEXT("prog.sheet") && Core)
	{
		const FName Who(*DevArgStr(Args, 0, TEXT("Vahn")));
		const int32 Level = FMath::Clamp(DevArgInt(Args, 1, 1), 1, Core->GetMaxLevel());
		static const TCHAR* Stats[] = { TEXT("HP"), TEXT("MP"), TEXT("AGL"), TEXT("ATK"),
		                                TEXT("UDF"), TEXT("LDF"), TEXT("SPD"), TEXT("INT") };
		Out = FString::Printf(TEXT("%s no L%d (XP %d)\n"),
			*Who.ToString(), Level, Core->GetXPForLevel(Level));
		for (const TCHAR* St : Stats)
		{
			Out += FString::Printf(TEXT("  %-4s %5d\n"), St, Core->GetStatAtLevel(Who, FName(St), Level));
		}
		Out += FString::Printf(TEXT("cresce na dificuldade atual: %s"),
			Core->DoesCharacterGrow(Who) ? TEXT("sim") : TEXT("NAO"));
	}
	else if (Verb == TEXT("prog.xp") && Core)
	{
		const int32 Level = FMath::Clamp(DevArgInt(Args, 0, 2), 1, Core->GetMaxLevel());
		Out = FString::Printf(TEXT("XP para o L%d — base %d | Vahn %d | Noa %d | Gala %d | Terra %d"),
			Level,
			Core->GetXPForLevel(Level),
			Core->GetXPForLevelForSlot(Level, 0),
			Core->GetXPForLevelForSlot(Level, 1),
			Core->GetXPForLevelForSlot(Level, 2),
			Core->GetXPForLevelForSlot(Level, 3));
	}
	// -------------------------------------------------------------- party
	else if (Verb == TEXT("party") && Party)
	{
		const TArray<FJRPGPartyMember> Todos = Party->GetRoster();
		if (Todos.Num() == 0)
		{
			Out = TEXT("roster vazio — chame StartNewGame ou 'party.recruit Vahn'");
		}
		else
		{
			Out = FString::Printf(TEXT("formação %d de %d\n"),
				Party->GetActiveParty().Num(), Party->MaxActiveMembers);
			Out += TEXT("  estado   nome    LV    HP        MP       AP  ATK UDF LDF SPD INT AGL\n");
			for (const FJRPGPartyMember& M : Todos)
			{
				const TCHAR* Estado =
					!M.bAvailable ? TEXT("[fora ]") :
					M.IsDead()    ? TEXT("[morto]") :
					M.bActive     ? TEXT("[ATIVO]") : TEXT("[banco]");
				Out += FString::Printf(
					TEXT("  %s %-7s %3d %5d/%-5d %4d/%-4d %3d %4d %3d %3d %3d %3d %3d\n"),
					Estado, *M.Key.ToString(), M.Level,
					M.HP, M.MaxHP, M.MP, M.MaxMP, M.AP,
					M.ATK, M.UDF, M.LDF, M.SPD, M.INT, M.AGL);
			}
			for (const FJRPGPartyMember& M : Todos)
			{
				Out += FString::Printf(TEXT("  %s: XP %d, faltam %d para o LV%d\n"),
					*M.Key.ToString(), M.ExperienceTotal,
					Party->GetXPToNextLevel(M.Key), M.Level + 1);
			}
		}
	}
	else if (Verb == TEXT("party.recruit") && Party)
	{
		const FName Who(*DevArgStr(Args, 0, TEXT("Vahn")));
		Out = FString::Printf(TEXT("RecruitCharacter(%s) = %s"), *Who.ToString(),
			Party->RecruitCharacter(Who) ? TEXT("ok") : TEXT("FALHOU"));
	}
	else if (Verb == TEXT("party.active") && Party)
	{
		const FName Who(*DevArgStr(Args, 0));
		const bool bVal = DevArgInt(Args, 1, 1) != 0;
		const bool bOk = Party->SetActive(Who, bVal);
		Out = FString::Printf(TEXT("SetActive(%s, %s) = %s — formação com %d"),
			*Who.ToString(), bVal ? TEXT("true") : TEXT("false"),
			bOk ? TEXT("ok") : TEXT("RECUSADO"), Party->GetActiveParty().Num());
	}
	else if (Verb == TEXT("party.available") && Party)
	{
		const FName Who(*DevArgStr(Args, 0));
		Party->SetAvailable(Who, DevArgInt(Args, 1, 1) != 0);
		Out = FString::Printf(TEXT("%s disponível: %s"), *Who.ToString(),
			Party->IsAvailable(Who) ? TEXT("sim") : TEXT("não"));
	}
	else if (Verb == TEXT("party.max") && Party)
	{
		Party->SetMaxActiveMembers(DevArgInt(Args, 0, 3));
		Out = FString::Printf(TEXT("máximo da formação = %d (ativos: %d)"),
			Party->MaxActiveMembers, Party->GetActiveParty().Num());
	}
	else if (Verb == TEXT("party.xp") && Party)
	{
		const int32 Bruto = DevArgInt(Args, 0, 1000);
		const int32 Cada = Party->AwardBattleXP(Bruto);
		Out = FString::Printf(TEXT("AwardBattleXP(%d): x3/4 e dividido entre os vivos = %d cada\n"),
			Bruto, Cada);
		for (const FJRPGPartyMember& M : Party->GetRoster())
		{
			Out += FString::Printf(TEXT("  %-7s LV%-3d XP %d\n"),
				*M.Key.ToString(), M.Level, M.ExperienceTotal);
		}
	}
	else if (Verb == TEXT("party.levelup") && Party)
	{
		const FName Who(*DevArgStr(Args, 0, TEXT("Vahn")));
		FJRPGPartyMember Antes;
		Party->GetMember(Who, Antes);
		const int32 N = Party->LevelUpMember(Who, DevArgInt(Args, 1, 1));
		FJRPGPartyMember Dep;
		Party->GetMember(Who, Dep);
		Out = FString::Printf(TEXT("%s subiu %d level(s): LV%d -> LV%d\n"),
			*Who.ToString(), N, Antes.Level, Dep.Level);
		Out += FString::Printf(TEXT("  HP %d -> %d | MP %d -> %d | ATK %d -> %d\n"),
			Antes.MaxHP, Dep.MaxHP, Antes.MaxMP, Dep.MaxMP, Antes.ATK, Dep.ATK);
		Out += FString::Printf(TEXT("  UDF %d -> %d | LDF %d -> %d | SPD %d -> %d | INT %d -> %d | AGL %d -> %d"),
			Antes.UDF, Dep.UDF, Antes.LDF, Dep.LDF, Antes.SPD, Dep.SPD,
			Antes.INT, Dep.INT, Antes.AGL, Dep.AGL);
	}
	else if (Verb == TEXT("party.setlevel") && Party)
	{
		const FName Who(*DevArgStr(Args, 0, TEXT("Vahn")));
		const int32 Alvo = DevArgInt(Args, 1, 10);
		if (!Party->SetMemberLevel(Who, Alvo))
		{
			Out = FString::Printf(TEXT("SetMemberLevel(%s, %d) FALHOU"), *Who.ToString(), Alvo);
		}
		else
		{
			FJRPGPartyMember M;
			Party->GetMember(Who, M);
			Out = FString::Printf(TEXT("%s no LV%d — cada level foi simulado, com jitter\n"),
				*Who.ToString(), M.Level);
			Out += FString::Printf(TEXT("  HP %d  MP %d  ATK %d  UDF %d  LDF %d  SPD %d  INT %d  AGL %d\n"),
				M.MaxHP, M.MaxMP, M.ATK, M.UDF, M.LDF, M.SPD, M.INT, M.AGL);
			Out += FString::Printf(TEXT("  XP %d"), M.ExperienceTotal);
		}
	}
	else if (Verb == TEXT("party.stat") && Party)
	{
		const FName Who(*DevArgStr(Args, 0, TEXT("Vahn")));
		const FName Stat(*DevArgStr(Args, 1, TEXT("ATK")));
		if (Args.IsValidIndex(2))
		{
			Party->SetMemberStat(Who, Stat, DevArgInt(Args, 2, 0));
		}
		FJRPGPartyMember M;
		Party->GetMember(Who, M);
		Out = FString::Printf(TEXT("%s: HP %d/%d MP %d/%d AP %d | ATK %d UDF %d LDF %d SPD %d INT %d AGL %d"),
			*Who.ToString(), M.HP, M.MaxHP, M.MP, M.MaxMP, M.AP,
			M.ATK, M.UDF, M.LDF, M.SPD, M.INT, M.AGL);
	}
	else if (Verb == TEXT("party.unrecruit") && Party)
	{
		const FName Who(*DevArgStr(Args, 0));
		Out = FString::Printf(TEXT("RemoveFromRoster(%s) = %s — o registro foi APAGADO ")
			TEXT("(para só sair da história use party.available)"),
			*Who.ToString(), Party->RemoveFromRoster(Who) ? TEXT("ok") : TEXT("não estava no roster"));
	}
	else if (Verb == TEXT("party.damage") && Party)
	{
		const FName Who(*DevArgStr(Args, 0));
		Party->ApplyDamage(Who, DevArgInt(Args, 1, 99999));
		FJRPGPartyMember M;
		Party->GetMember(Who, M);
		Out = FString::Printf(TEXT("%s HP %d/%d %s"), *Who.ToString(), M.HP, M.MaxHP,
			M.IsDead() ? TEXT("(morto — fora da divisão de XP)") : TEXT(""));
	}
	else if (Verb == TEXT("party.heal") && Party)
	{
		Party->FullRestoreAll();
		Out = TEXT("HP e MP cheios em todo o roster (mortos continuam mortos — use party.revive)");
	}
	else if (Verb == TEXT("party.revive") && Party)
	{
		const FName Who(*DevArgStr(Args, 0));
		FJRPGPartyMember M;
		Party->GetMember(Who, M);
		Party->ReviveMember(Who, FMath::Max(1, M.MaxHP / 2));
		Party->GetMember(Who, M);
		Out = FString::Printf(TEXT("%s HP %d/%d"), *Who.ToString(), M.HP, M.MaxHP);
	}
	else if (Verb == TEXT("level.max") && Core)
	{
		Core->SetMaxLevel(DevArgInt(Args, 0, 99));
		Out = FString::Printf(TEXT("teto de level = %d (absoluto %d)\n"),
			Core->GetMaxLevel(), UCoreSubsystem::AbsoluteMaxLevel);
		Out += FString::Printf(TEXT("XP para o topo: %d"), Core->GetXPForLevel(Core->GetMaxLevel()));
	}
	else if (Verb == TEXT("stat.cap") && Core)
	{
		const FName Stat(*DevArgStr(Args, 0, TEXT("HP")));
		if (Args.IsValidIndex(1))
		{
			Core->SetStatCap(Stat, DevArgInt(Args, 1, 999));
		}
		Out = FString::Printf(TEXT("cap de %s = %d"), *Stat.ToString(), Core->GetStatCap(Stat));
	}
	// -------------------------------------------------------------- mundo
	else if (Verb == TEXT("world.field") && World)
	{
		const bool bValue = DevArgInt(Args, 0, 1) != 0;
		World->SetIsInField(bValue);
		Out = FString::Printf(TEXT("bIsInField = %s — aba SAVE %s"),
			bValue ? TEXT("true") : TEXT("false"),
			CanSaveNow() ? TEXT("liberada") : TEXT("bloqueada"));
	}
	else if (Verb == TEXT("world.flag") && World)
	{
		const FName Id(*DevArgStr(Args, 0));
		const bool bValue = DevArgInt(Args, 1, 1) != 0;
		World->SetEventFlag(Id, bValue);
		Out = FString::Printf(TEXT("flag %s = %s"), *Id.ToString(),
			World->GetEventFlagValue(Id) ? TEXT("true") : TEXT("false"));
	}
	// --------------------------------------------------------------- save
	else if (Verb == TEXT("save.save") && Save)
	{
		const int32 Slot = DevArgInt(Args, 0, 0);
		Out = FString::Printf(TEXT("SaveGame(%d) = %s"), Slot,
			Save->SaveGame(Slot) ? TEXT("ok") : TEXT("FALHOU"));
	}
	else if (Verb == TEXT("save.load") && Save)
	{
		const int32 Slot = DevArgInt(Args, 0, 0);
		Out = FString::Printf(TEXT("LoadGame(%d) = %s"), Slot,
			Save->LoadGame(Slot) ? TEXT("ok") : TEXT("FALHOU"));
	}
	// -------------------------------------------------------------- telas
	else if (Verb.StartsWith(TEXT("screen.")))
	{
		const FString Which = Verb.RightChop(7);
		// Fecha o dev menu antes: as telas do jogo exigem HudOnly.
		DispatchJS(TEXT("JRPGDev.close();"));
		SetStateInternal(EJRPGUIState::HudOnly);

		if      (Which == TEXT("menu"))     { OpenMenu(); }
		else if (Which == TEXT("save"))     { OpenSaveScreen(); }
		else if (Which == TEXT("load"))     { OpenLoadScreen(); }
		else if (Which == TEXT("options"))  { OpenOptions(); }
		else if (Which == TEXT("mainmenu")) { OpenMainMenu(); }
		else if (Which == TEXT("shop"))     { OpenShop(FName(*DevArgStr(Args, 0))); }
		else if (Which == TEXT("popup"))
		{
			ShowItemPopup(FText::FromString(DevArgStr(Args, 0, TEXT("Item de Teste"))),
				DevArgInt(Args, 1, 1));
		}
		return;   // a tela nova assumiu; nao ha para onde responder
	}
	else
	{
		Out = FString::Printf(TEXT("comando desconhecido: %s"), *Verb);
	}

	DispatchJS(FString::Printf(TEXT("JRPGDev.result(%s);"),
		*JRPGWebUI::ToJSStringLiteral(Out)));
#endif
}

void UWebUISubsystem::ReloadShell()
{
	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: ReloadShell — forçando recarga do shell.html."));

	GLastLoadedShellPage.Empty();
	bShellReady = false;
	PendingShellJS.Empty();
	SetStateInternal(EJRPGUIState::HudOnly);
	ApplyGameFocus();
	EnsureShellWidget();
}

void UWebUISubsystem::SetStateInternal(EJRPGUIState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	const EJRPGUIState OldState = CurrentState;
	CurrentState = NewState;
	OnUIStateChanged.Broadcast(OldState, NewState);
}

// ============================================================
// Callbacks do Bridge (já na Game Thread)
// ============================================================

void UWebUISubsystem::HandleShellReady()
{
	if (bShellReady)
	{
		return;
	}
	bShellReady = true;

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Shell pronto — %d comando(s) JS pendente(s) para despachar."), PendingShellJS.Num());

	// Limpa estado JS herdado (o documento sobrevive entre sessões PIE)
	// ANTES de qualquer comando enfileirado
	if (WebContainerWidget)
	{
		WebContainerWidget->ExecuteJS(TEXT("resetUIShell();"));
		for (const FString& JSCode : PendingShellJS)
		{
			WebContainerWidget->ExecuteJS(JSCode);
		}
	}
	PendingShellJS.Empty();
}

void UWebUISubsystem::HandleCloseRequestFromJS()
{
	ApplyGameFocus();
	SetStateInternal(EJRPGUIState::HudOnly);
	CurrentShopID = NAME_None;
	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Input devolvido ao jogo (hud_only)."));
}

void UWebUISubsystem::HandleMainMenuActionFromJS(const FString& Action)
{
	if (CurrentState != EJRPGUIState::MainMenuOpen)
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: onmainmenuaction('%s') ignorado — estado atual '%s' não é o main menu."),
			*Action, UIStateToJS(CurrentState));
		return;
	}

	// Continue: abre a tela de save existente TRAVADA em Load e voltando ao
	// título ao cancelar. O JS já escondeu o main menu; o foco continua na UI.
	if (Action == TEXT("continue"))
	{
		SetStateInternal(EJRPGUIState::SaveLoadOpen);
		DispatchJS(FString::Printf(TEXT("JRPGSaveLoad.open(%s, {mode:'load', returnTo:'mainmenu'});"),
			*BuildSaveSlotsPayloadJS()));
		return;
	}

	if (Action == TEXT("options"))
	{
		OpenOptions();
		return;
	}

	// New Game e Quit saem do main menu de vez: o input volta para o jogo ANTES
	// do OpenLevel do Blueprint (o FInputModeUIOnly referencia o Slate widget
	// que o LoadMap vai destruir — mesma ordem crítica do Load de save).
	if (Action == TEXT("newgame"))
	{
		ApplyGameFocus();
		SetStateInternal(EJRPGUIState::HudOnly);

		if (UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>())
		{
			Core->StartNewGame();
		}

		UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: New Game — StartNewGame aplicado; o Blueprint deve abrir o mapa inicial (OnMainMenuAction)."));
		OnMainMenuAction.Broadcast(Action);
		return;
	}

	if (Action == TEXT("quit"))
	{
		ApplyGameFocus();
		SetStateInternal(EJRPGUIState::HudOnly);
		OnMainMenuAction.Broadcast(Action);

		UWorld* World = GetGameInstance()->GetWorld();
		APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World);
		UKismetSystemLibrary::QuitGame(World, PC, EQuitPreference::Quit, false);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: onmainmenuaction('%s') desconhecido — ignorado."), *Action);
}

void UWebUISubsystem::HandleUIStateChangedFromJS(const FString& State)
{
	// Só as transições internas da UI (uma tela devolvendo o controle para a
	// tela que a abriu) chegam aqui — nenhuma delas devolve o input ao jogo,
	// então NÃO se mexe em foco/input. hud_only continua vindo por closemenu.
	EJRPGUIState NewState = EJRPGUIState::HudOnly;
	if (State == TEXT("menu_open"))
	{
		NewState = EJRPGUIState::MenuOpen;
	}
	else if (State == TEXT("main_menu"))
	{
		NewState = EJRPGUIState::MainMenuOpen;
	}
	else if (State == TEXT("options_open"))
	{
		NewState = EJRPGUIState::OptionsOpen;
	}
	else if (State == TEXT("saveload_open"))
	{
		NewState = EJRPGUIState::SaveLoadOpen;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: onuistatechanged('%s') não reconhecido — ignorado."), *State);
		return;
	}

	SetStateInternal(NewState);

	// Voltar para o menu remonta os cards de status.
	//
	// Eles nasciam SÓ no OpenMenu(). Quem entrasse na tela de Party, mudasse a
	// formação e voltasse via a UI (sem passar pelo OpenMenu de novo) continuava
	// vendo a formação velha — inclusive personagens que acabaram de sair dela.
	if (NewState == EJRPGUIState::MenuOpen)
	{
		DispatchJS(FString::Printf(TEXT("JRPGSetParty(%s);"), *BuildMenuPartyJS()));
	}
}

void UWebUISubsystem::HandleSaveSlotFromJS(int32 SlotIndex)
{
	// O índice vem do JS — validar antes de tocar no SaveSubsystem
	if (SlotIndex < 0 || SlotIndex >= USaveSubsystem::NumSaveSlots)
	{
		UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: onsaveslot com índice inválido (%d) — ignorado."), SlotIndex);
		return;
	}

	USaveSubsystem* SaveSubsystem = GetGameInstance()->GetSubsystem<USaveSubsystem>();
	const bool bOk = SaveSubsystem && SaveSubsystem->SaveGame(SlotIndex);

	if (bOk)
	{
		// Atualiza o grid antes do feedback (o slot recém-salvo aparece ocupado)
		RefreshSaveLoadSlots();
	}
	DispatchJS(FString::Printf(TEXT("JRPGSaveLoad.onSaveResult(%d, %s);"),
		SlotIndex, bOk ? TEXT("true") : TEXT("false")));
}

void UWebUISubsystem::HandleLoadSlotFromJS(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= USaveSubsystem::NumSaveSlots)
	{
		UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: onloadslot com índice inválido (%d) — ignorado."), SlotIndex);
		return;
	}

	// ORDEM CRÍTICA: restaurar o input mode do jogo ANTES do OpenLevel — o
	// FInputModeUIOnly referencia o Slate widget que o LoadMap vai destruir.
	// O JS já escondeu a seção de save antes de chamar onloadslot.
	HandleCloseRequestFromJS();

	USaveSubsystem* SaveSubsystem = GetGameInstance()->GetSubsystem<USaveSubsystem>();
	if (!SaveSubsystem || !SaveSubsystem->LoadGame(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: Load do slot %d falhou — a tela de save já foi fechada."), SlotIndex);
	}
}

void UWebUISubsystem::RefreshSaveLoadSlots()
{
	if (CurrentState != EJRPGUIState::SaveLoadOpen)
	{
		return;
	}
	DispatchJS(FString::Printf(TEXT("JRPGSaveLoad.updateSlots(%s);"), *BuildSaveSlotsPayloadJS()));
}

void UWebUISubsystem::HandleShopBuyFromJS(const FString& ItemID, int32 Quantity)
{
	if (CurrentState != EJRPGUIState::ShopOpen || CurrentShopID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: onshopbuy ignorado — nenhuma loja aberta."));
		return;
	}

	UShopSubsystem* ShopSubsystem = GetGameInstance()->GetSubsystem<UShopSubsystem>();
	const bool bOk = ShopSubsystem && ShopSubsystem->BuyItem(CurrentShopID, FName(*ItemID), Quantity);

	// Atualiza gold/possuídos/aba de venda antes do feedback sonoro
	DispatchJS(FString::Printf(TEXT("JRPGShop.update(%s);"), *BuildShopPayloadJS()));
	DispatchJS(FString::Printf(TEXT("JRPGShop.onTransactionResult(%s);"), bOk ? TEXT("true") : TEXT("false")));
}

void UWebUISubsystem::HandleShopSellFromJS(const FString& ItemID, int32 Quantity)
{
	if (CurrentState != EJRPGUIState::ShopOpen || CurrentShopID.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: onshopsell ignorado — nenhuma loja aberta."));
		return;
	}

	UShopSubsystem* ShopSubsystem = GetGameInstance()->GetSubsystem<UShopSubsystem>();
	const bool bOk = ShopSubsystem && ShopSubsystem->SellItem(FName(*ItemID), Quantity);

	DispatchJS(FString::Printf(TEXT("JRPGShop.update(%s);"), *BuildShopPayloadJS()));
	DispatchJS(FString::Printf(TEXT("JRPGShop.onTransactionResult(%s);"), bOk ? TEXT("true") : TEXT("false")));
}

// ============================================================
// Internos
// ============================================================

bool UWebUISubsystem::EnsureShellWidget()
{
	if (!WebContainerWidget)
	{
		UWorld* World = GetGameInstance()->GetWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: Falha ao obter referência do World para criar a interface."));
			return false;
		}

		APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World);
		if (!PC || !PC->GetLocalPlayer())
		{
			UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: O PlayerController do mundo atual não possui um LocalPlayer associado. Ignorando criação da UI."));
			return false;
		}

		// Define a classe do Widget: se não houver um Blueprint customizado, usa a classe base C++
		TSubclassOf<UUserWidget> WidgetClass = WebContainerWidgetClass ? WebContainerWidgetClass : TSubclassOf<UUserWidget>(UWebContainerWidget::StaticClass());

		WebContainerWidget = Cast<UWebContainerWidget>(CreateWidget<UUserWidget>(PC, WidgetClass));
		if (!WebContainerWidget)
		{
			UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: Falha ao criar instância do WebContainerWidget."));
			return false;
		}

		// Widget PERMANENTE: fica no viewport a sessão inteira. HitTestInvisible
		// = renderiza (popup/HUD visíveis) mas nunca intercepta mouse — o input
		// só vai para a UI quando o menu abre (ApplyUIFocus -> Visible).
		WebContainerWidget->AddToViewport(100);
		WebContainerWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

		UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Widget permanente do shell montado no viewport."));
	}

	if (GLastLoadedShellPage != GShellPage)
	{
		// Primeiro load do processo (ou ReloadShell): carrega o documento único
		bShellReady = false;
		GLastLoadedShellPage = GShellPage;
		WebContainerWidget->LoadHTMLPage(GShellPage);
		// bShellReady vira true quando o boot do JS chamar bridge.onuiready()
	}
	else if (!bShellReady)
	{
		// PIE 2+: a View global preservou o documento — não haverá novo load
		// nem onuiready. O BindBridge do NativeConstruct já rebindou a ponte
		// no documento vivo; o shell está pronto imediatamente.
		HandleShellReady();
	}

	return true;
}

void UWebUISubsystem::DispatchJS(const FString& JSCode)
{
	if (bShellReady && WebContainerWidget)
	{
		WebContainerWidget->ExecuteJS(JSCode);
	}
	else
	{
		// Shell ainda carregando: enfileira para o flush do HandleShellReady
		PendingShellJS.Add(JSCode);
	}
}

void UWebUISubsystem::ApplyUIFocus()
{
	if (!WebContainerWidget)
	{
		return;
	}

	UWorld* World = GetGameInstance()->GetWorld();
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World);
	if (!PC)
	{
		return;
	}

	// GetCachedWidget() — e NÃO TakeWidget(). TakeWidget() reconstrói a árvore Slate
	// inteira quando ela já foi liberada (troca de modo de janela, por exemplo),
	// rodando RebuildWidget()/NativeConstruct() no meio da configuração de foco.
	// Isso criava um SEGUNDO SUltralightBrowser disputando a mesma shared texture
	// com o antigo — a porta de entrada para o travamento do menu.
	TSharedPtr<SWidget> ContainerSlateWidget = WebContainerWidget->GetCachedWidget();
	if (!ContainerSlateWidget.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: Slate do shell foi liberado — remontando o widget antes de focar."));
		TeardownWidget();
		if (!EnsureShellWidget() || !WebContainerWidget)
		{
			return;
		}
		ContainerSlateWidget = WebContainerWidget->GetCachedWidget();
		if (!ContainerSlateWidget.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("WebUISubsystem: Falha ao remontar o Slate do shell — foco da UI abortado."));
			return;
		}
	}

	// Menu aberto: widget passa a aceitar mouse
	WebContainerWidget->SetVisibility(ESlateVisibility::Visible);

	// Casa a cadência do jogo com os 60Hz da UL thread enquanto a UI está no ar.
	ClampFrameRateForUI();

	// Configuração do mouse e do foco do input para interagir com a tela HTML
	PC->bShowMouseCursor = true;
	FInputModeUIOnly InputMode;

	TSharedPtr<SWidget> BrowserSlateWidget;
	if (WebContainerWidget->GetWebBrowser())
	{
		BrowserSlateWidget = WebContainerWidget->GetWebBrowser()->GetSlateWidget();
	}

	if (BrowserSlateWidget.IsValid())
	{
		InputMode.SetWidgetToFocus(BrowserSlateWidget.ToSharedRef());
	}
	else
	{
		InputMode.SetWidgetToFocus(ContainerSlateWidget.ToSharedRef());
	}

	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);

	// Força o foco do teclado/gamepad diretamente via Slate no navegador interno
	// para capturar os inputs imediatamente
	if (FSlateApplication::IsInitialized())
	{
		if (BrowserSlateWidget.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(BrowserSlateWidget.ToSharedRef());
			FSlateApplication::Get().SetUserFocus(0, BrowserSlateWidget.ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().SetKeyboardFocus(ContainerSlateWidget);
			FSlateApplication::Get().SetUserFocus(0, ContainerSlateWidget.ToSharedRef());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: UI focada (menu aberto)."));
}

void UWebUISubsystem::ApplyGameFocus()
{
	// Volta a ser um overlay passivo (popups/HUD continuam renderizando)
	if (WebContainerWidget)
	{
		WebContainerWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// Devolve o teto de FPS que o jogador tinha antes de abrir a UI
	RestoreFrameRate();

	UWorld* World = GetGameInstance()->GetWorld();
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World);
	if (PC)
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);

		// Retorna o foco do teclado/mouse de volta para a tela de jogo
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}
}

void UWebUISubsystem::TeardownWidget()
{
	if (!WebContainerWidget)
	{
		return;
	}

	// Nunca deixar o jogo preso no cap da UI depois de um fechamento anormal
	RestoreFrameRate();

	WebContainerWidget->RemoveFromParent();
	WebContainerWidget = nullptr;

	UWorld* World = GetGameInstance()->GetWorld();
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World);
	if (PC)
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}

	bShellReady = false;
	PendingShellJS.Empty();
	CurrentState = EJRPGUIState::HudOnly;
	// GLastLoadedShellPage NÃO é limpo: a View global preserva o documento
	// entre sessões PIE — é exatamente isso que o guard rastreia.

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Widget do shell desmontado da Viewport (fim da sessão)."));
}

// ============================================================
// Teto de FPS enquanto a UI está aberta
// ============================================================

// t.MaxFPS é o mesmo CVar que o jogador altera pelo console. Gravar com
// ECVF_SetByCode seria IGNORADO EM SILÊNCIO quando o valor atual veio do console
// (prioridade maior), então usamos ECVF_SetByConsole nos dois sentidos — é a
// única prioridade que casa com quem realisticamente mexe nele.
static IConsoleVariable* GetMaxFPSVar()
{
	static IConsoleVariable* MaxFPSVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
	return MaxFPSVar;
}

void UWebUISubsystem::ClampFrameRateForUI()
{
	if (bFrameRateClamped || UIFrameRateCap <= 0.f)
	{
		return;
	}

	IConsoleVariable* MaxFPSVar = GetMaxFPSVar();
	if (!MaxFPSVar)
	{
		return;
	}

	const float Current = MaxFPSVar->GetFloat();

	// Já está mais restrito que o cap (ex.: jogador limitou em 30): não mexer.
	if (Current > 0.f && Current <= UIFrameRateCap)
	{
		return;
	}

	SavedMaxFPS = Current;
	bFrameRateClamped = true;
	MaxFPSVar->Set(UIFrameRateCap, ECVF_SetByConsole);

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: FPS limitado a %.0f enquanto a UI está aberta (anterior: %.0f)."),
		UIFrameRateCap, SavedMaxFPS);
}

void UWebUISubsystem::RestoreFrameRate()
{
	if (!bFrameRateClamped)
	{
		return;
	}

	bFrameRateClamped = false;

	if (IConsoleVariable* MaxFPSVar = GetMaxFPSVar())
	{
		MaxFPSVar->Set(SavedMaxFPS, ECVF_SetByConsole);
		UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Limite de FPS restaurado para %.0f."), SavedMaxFPS);
	}

	SavedMaxFPS = 0.f;
}

const TCHAR* UWebUISubsystem::UIStateToJS(EJRPGUIState State)
{
	switch (State)
	{
	case EJRPGUIState::MenuOpen:       return TEXT("menu_open");
	case EJRPGUIState::DialogueActive: return TEXT("dialogue_active");
	case EJRPGUIState::OptionsOpen:    return TEXT("options_open");
	case EJRPGUIState::SaveLoadOpen:   return TEXT("saveload_open");
	case EJRPGUIState::ShopOpen:       return TEXT("shop_open");
	case EJRPGUIState::MainMenuOpen:   return TEXT("main_menu");
	case EJRPGUIState::HudOnly:
	default:                           return TEXT("hud_only");
	}
}

// Slug em minúsculas de cada categoria, usado pela UI da loja para escolher o
// ícone SVG da linha e montar a barra de filtros. Os nomes casam com os ids do
// <symbol> no sprite de 60_shop.html — mudou aqui, muda lá.
static const TCHAR* ItemCategoryToJS(EItemCategory Category)
{
	switch (Category)
	{
	case EItemCategory::Consumable:    return TEXT("consumable");
	case EItemCategory::PermanentStat: return TEXT("permanent");
	case EItemCategory::ArtBook:       return TEXT("artbook");
	case EItemCategory::Key:           return TEXT("key");
	case EItemCategory::FishingLure:   return TEXT("lure");
	case EItemCategory::Weapon:        return TEXT("weapon");
	case EItemCategory::Armor:         return TEXT("armor");
	case EItemCategory::Accessory:     return TEXT("accessory");
	default:                           return TEXT("consumable");
	}
}

void UWebUISubsystem::OpenPartyScreen()
{
	// Mesmo contrato de Options/Save/Load: só do menu de pausa, e o JS já
	// escondeu os cards antes de chamar.
	if (CurrentState != EJRPGUIState::MenuOpen)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("WebUISubsystem: OpenPartyScreen ignorado — estado atual '%s'."),
			UIStateToJS(CurrentState));
		return;
	}

	if (!EnsureShellWidget())
	{
		return;
	}

	SetStateInternal(EJRPGUIState::PartyOpen);
	DispatchJS(FString::Printf(TEXT("JRPGParty.open(%s);"), *BuildPartyPayloadJS()));
}

void UWebUISubsystem::HandlePartyToggle(FName Character, bool bActivate)
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party)
	{
		return;
	}

	// Quem valida é o subsystem — formação cheia, último ativo, indisponível.
	if (!Party->SetActive(Character, bActivate))
	{
		const FString Motivo = bActivate
			? FString::Printf(TEXT("não dá para ativar %s (formação cheia ou fora da história)"),
				*Character.ToString())
			: FString::Printf(TEXT("%s é o último da formação"), *Character.ToString());

		DispatchJS(FString::Printf(TEXT("JRPGParty.onToggleRejected(%s);"),
			*JRPGWebUI::ToJSStringLiteral(Motivo)));
		return;
	}

	DispatchJS(FString::Printf(TEXT("JRPGParty.update(%s);"), *BuildPartyPayloadJS()));
}

FString UWebUISubsystem::BuildMenuPartyJS() const
{
	// Só a formação e só o que o card de status mostra — o payload gordo da
	// tela de Party (com os 6 atributos) não faz falta aqui.
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party)
	{
		return TEXT("[]");
	}

	const UDataTable* Chars = Party->CharacterTable;
	const TArray<FJRPGPartyMember> Ativos = Party->GetActiveMembers();

	FString Out = TEXT("[");
	for (int32 i = 0; i < Ativos.Num(); ++i)
	{
		const FJRPGPartyMember& M = Ativos[i];
		if (i > 0)
		{
			Out += TEXT(",");
		}

		FString Nome = M.Key.ToString();
		FString Retrato = M.Key.ToString().ToLower();
		if (Chars)
		{
			if (const FCharacterData* Row =
					Chars->FindRow<FCharacterData>(M.Key, TEXT("BuildMenuPartyJS"), false))
			{
				if (!Row->DisplayName.IsEmpty()) { Nome = Row->DisplayName.ToString(); }
				if (!Row->PortraitId.IsNone())   { Retrato = Row->PortraitId.ToString(); }
			}
		}

		Out += FString::Printf(
			TEXT("{id:%s,name:%s,portrait:%s,lv:%d,hp:%d,maxhp:%d,mp:%d,maxmp:%d,ap:%d,maxap:100}"),
			*JRPGWebUI::ToJSStringLiteral(M.Key.ToString()),
			*JRPGWebUI::ToJSStringLiteral(Nome),
			*JRPGWebUI::ToJSStringLiteral(Retrato),
			M.Level, M.HP, M.MaxHP, M.MP, M.MaxMP, M.AP);
	}
	Out += TEXT("]");
	return Out;
}

FString UWebUISubsystem::BuildPartyPayloadJS() const
{
	UGameInstance* GI = GetGameInstance();
	UPartySubsystem* Party = GI ? GI->GetSubsystem<UPartySubsystem>() : nullptr;
	if (!Party)
	{
		return TEXT("{max:3,members:[]}");
	}

	// Resolve o nome de exibição e o retrato em DT_Characters; sem a tabela,
	// cai na key mesmo — a tela continua funcionando.
	const UDataTable* Chars = Party->CharacterTable;

	// Ordem: a formação primeiro (é o que o jogador quer ver em cima), depois
	// a reserva, depois quem saiu da história.
	TArray<FJRPGPartyMember> Ordenado;
	for (const FJRPGPartyMember& M : Party->GetActiveMembers())
	{
		Ordenado.Add(M);
	}
	for (const FJRPGPartyMember& M : Party->GetRoster())
	{
		if (!M.bActive)
		{
			Ordenado.Add(M);
		}
	}

	FString Membros = TEXT("[");
	for (int32 i = 0; i < Ordenado.Num(); ++i)
	{
		const FJRPGPartyMember& M = Ordenado[i];
		if (i > 0)
		{
			Membros += TEXT(",");
		}

		FString Nome = M.Key.ToString();
		FString Retrato = M.Key.ToString().ToLower();
		if (Chars)
		{
			if (const FCharacterData* Row =
					Chars->FindRow<FCharacterData>(M.Key, TEXT("BuildPartyPayloadJS"), false))
			{
				if (!Row->DisplayName.IsEmpty())
				{
					Nome = Row->DisplayName.ToString();
				}
				if (!Row->PortraitId.IsNone())
				{
					Retrato = Row->PortraitId.ToString();
				}
			}
		}

		// HP e MP vão como ATUAL e MÁXIMO. O máximo é o do personagem naquele
		// level — nada de teto fixo.
		Membros += FString::Printf(
			TEXT("{id:%s,name:%s,portrait:%s,lv:%d,")
			TEXT("hp:%d,maxhp:%d,mp:%d,maxmp:%d,ap:%d,maxap:100,")
			TEXT("atk:%d,udf:%d,ldf:%d,spd:%d,int:%d,agl:%d,")
			TEXT("active:%s,available:%s,dead:%s}"),
			*JRPGWebUI::ToJSStringLiteral(M.Key.ToString()),
			*JRPGWebUI::ToJSStringLiteral(Nome),
			*JRPGWebUI::ToJSStringLiteral(Retrato),
			M.Level,
			M.HP, M.MaxHP, M.MP, M.MaxMP, M.AP,
			M.ATK, M.UDF, M.LDF, M.SPD, M.INT, M.AGL,
			M.bActive ? TEXT("true") : TEXT("false"),
			M.bAvailable ? TEXT("true") : TEXT("false"),
			M.IsDead() ? TEXT("true") : TEXT("false"));
	}
	Membros += TEXT("]");

	return FString::Printf(TEXT("{max:%d,members:%s}"), Party->MaxActiveMembers, *Membros);
}

void UWebUISubsystem::OpenItemsScreen()
{
	// Mesmo contrato de Options/Save/Load/Party: só do menu de pausa, e o JS já
	// escondeu os cards antes de chamar.
	if (CurrentState != EJRPGUIState::MenuOpen)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("WebUISubsystem: OpenItemsScreen ignorado — estado atual '%s'."),
			UIStateToJS(CurrentState));
		return;
	}

	if (!EnsureShellWidget())
	{
		return;
	}

	SetStateInternal(EJRPGUIState::ItemsOpen);
	DispatchJS(FString::Printf(TEXT("JRPGItems.open(%s);"), *BuildItemsPayloadJS()));
}

void UWebUISubsystem::HandleItemDiscard(FName ItemID, int32 Quantity)
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	if (!Inv)
	{
		return;
	}

	// Quem valida é o InventorySubsystem — a UI é só mais um caminho até a
	// regra, nunca o guarda único.
	if (!Inv->CanDiscardItem(ItemID))
	{
		DispatchJS(FString::Printf(TEXT("JRPGItems.onDiscardRejected(%s);"),
			*JRPGWebUI::ToJSStringLiteral(
				FString::Printf(TEXT("%s cannot be discarded"), *ItemID.ToString()))));
		return;
	}

	if (!Inv->RemoveItem(ItemID, FMath::Max(1, Quantity)))
	{
		DispatchJS(FString::Printf(TEXT("JRPGItems.onDiscardRejected(%s);"),
			*JRPGWebUI::ToJSStringLiteral(
				FString::Printf(TEXT("could not discard %s"), *ItemID.ToString()))));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: %d x %s descartado(s)."),
		Quantity, *ItemID.ToString());

	DispatchJS(FString::Printf(TEXT("JRPGItems.update(%s);"), *BuildItemsPayloadJS()));
}

FString UWebUISubsystem::BuildItemsPayloadJS() const
{
	UGameInstance* GI = GetGameInstance();
	UInventorySubsystem* Inv = GI ? GI->GetSubsystem<UInventorySubsystem>() : nullptr;
	UCoreSubsystem* Core = GI ? GI->GetSubsystem<UCoreSubsystem>() : nullptr;
	if (!Inv)
	{
		return TEXT("{gold:0,items:[]}");
	}

	const TArray<FInventorySlot> Slots = Inv->GetInventorySlots();

	FString Lista = TEXT("[");
	int32 Escritos = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		FItemData Item;
		if (!Inv->GetItemData(Slot.ItemID, Item) || Slot.Quantity <= 0)
		{
			continue;
		}

		if (Escritos > 0)
		{
			Lista += TEXT(",");
		}
		++Escritos;

		// Os efeitos vão CRUS: quem traduz para texto é o JS, que é onde mora a
		// apresentação. Assim um EffectClass novo no CSV não exige recompilar.
		Lista += FString::Printf(
			TEXT("{id:%s,name:%s,cat:%s,qty:%d,ord:%d,price:%d,desc:%s,")
			TEXT("healhp:%d,healmp:%d,healap:%s,cure:%s,revive:%s,all:%s,")
			TEXT("atk:%d,udf:%d,ldf:%d,")
			TEXT("effect:%s,effectvalue:%d,status:%s,element:%s,art:%s,summon:%s,")
			TEXT("key:%s,candiscard:%s}"),
			*JRPGWebUI::ToJSStringLiteral(Slot.ItemID.ToString()),
			*JRPGWebUI::ToJSStringLiteral(Item.Name.ToString()),
			*JRPGWebUI::ToJSStringLiteral(ItemCategoryToJS(Item.Category)),
			Slot.Quantity,
			Slot.AcquiredOrder,
			Item.BuyPrice,
			*JRPGWebUI::ToJSStringLiteral(Item.EffectDescription.ToString()),
			Item.HealHP, Item.HealMP,
			Item.bHealAP     ? TEXT("true") : TEXT("false"),
			Item.bCureStatus ? TEXT("true") : TEXT("false"),
			Item.bRevive     ? TEXT("true") : TEXT("false"),
			Item.bTargetAll  ? TEXT("true") : TEXT("false"),
			Item.AttackBonus, Item.UDF, Item.LDF,
			*JRPGWebUI::ToJSStringLiteral(Item.EffectClass),
			Item.EffectValue,
			*JRPGWebUI::ToJSStringLiteral(Item.StatusType),
			*JRPGWebUI::ToJSStringLiteral(Item.ElementType),
			*JRPGWebUI::OptionalNameToJS(Item.TeachesArt),
			*JRPGWebUI::OptionalNameToJS(Item.Summons),
			(Item.Category == EItemCategory::Key) ? TEXT("true") : TEXT("false"),
			Inv->CanDiscardItem(Slot.ItemID) ? TEXT("true") : TEXT("false"));
	}
	Lista += TEXT("]");

	return FString::Printf(TEXT("{gold:%d,items:%s}"),
		Core ? Core->GetGold() : 0, *Lista);
}

FString UWebUISubsystem::BuildShopPayloadJS() const
{
	UShopSubsystem* ShopSubsystem = GetGameInstance()->GetSubsystem<UShopSubsystem>();
	UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>();
	if (!ShopSubsystem || !Core)
	{
		return TEXT("{}");
	}

	FText ShopName;
	FString Town;
	TArray<FShopStockEntry> Stock;
	if (!ShopSubsystem->GetShopStock(CurrentShopID, ShopName, Town, Stock))
	{
		return TEXT("{}");
	}

	// Helper local: array literal JS de entradas de item (compra e venda)
	auto BuildEntriesLiteral = [](const TArray<FShopStockEntry>& Entries) -> FString
	{
		FString Out = TEXT("[");
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const FShopStockEntry& E = Entries[i];
			if (i > 0)
			{
				Out += TEXT(",");
			}

			FString OthersLiteral = TEXT("[");
			for (int32 o = 0; o < E.EquipOthers.Num(); ++o)
			{
				if (o > 0)
				{
					OthersLiteral += TEXT(",");
				}
				OthersLiteral += JRPGWebUI::ToJSStringLiteral(E.EquipOthers[o]);
			}
			OthersLiteral += TEXT("]");

			// 'cat' é o slug em minúsculas que a UI usa para escolher o ícone
			// SVG e montar a barra de filtros.
			Out += FString::Printf(
				TEXT("{id:%s,name:%s,desc:%s,stats:%s,cat:%s,price:%d,featured:%s,owned:%d,")
				TEXT("atk:%d,udf:%d,ldf:%d,best:%s,others:%s}"),
				*JRPGWebUI::ToJSStringLiteral(E.ItemID.ToString()),
				*JRPGWebUI::ToJSStringLiteral(E.DisplayName.ToString()),
				*JRPGWebUI::ToJSStringLiteral(E.Description.ToString()),
				*JRPGWebUI::ToJSStringLiteral(E.StatsLine.ToString()),
				*JRPGWebUI::ToJSStringLiteral(ItemCategoryToJS(E.Category)),
				E.Price,
				E.bFeatured ? TEXT("true") : TEXT("false"),
				E.OwnedQuantity,
				E.AttackBonus, E.UDF, E.LDF,
				*JRPGWebUI::ToJSStringLiteral(E.EquipBest),
				*OthersLiteral);
		}
		Out += TEXT("]");
		return Out;
	};

	const TArray<FShopStockEntry> Sellable = ShopSubsystem->GetSellableInventory();

	// A loja mostra ATK/UDF/LDF porque é o que arma e armadura mexem — INT, SPD
	// e AGL são tocados por 1 ou 2 acessórios no jogo inteiro e ficariam mortos
	// aqui. Só a FORMAÇÃO aparece: se só o Vahn está ativo, só ele.
	FString PartyLiteral = TEXT("[");
	if (UPartySubsystem* Party = GetGameInstance()->GetSubsystem<UPartySubsystem>())
	{
		const TArray<FJRPGPartyMember> Ativos = Party->GetActiveMembers();
		for (int32 i = 0; i < Ativos.Num(); ++i)
		{
			const FJRPGPartyMember& M = Ativos[i];
			if (i > 0)
			{
				PartyLiteral += TEXT(",");
			}

			FString Nome = M.Key.ToString();
			if (const UDataTable* Chars = Party->CharacterTable)
			{
				if (const FCharacterData* Row =
						Chars->FindRow<FCharacterData>(M.Key, TEXT("BuildShopPayloadJS"), false))
				{
					if (!Row->DisplayName.IsEmpty())
					{
						Nome = Row->DisplayName.ToString();
					}
				}
			}

			PartyLiteral += FString::Printf(TEXT("{id:%s,name:%s,atk:%d,udf:%d,ldf:%d}"),
				*JRPGWebUI::ToJSStringLiteral(M.Key.ToString().ToLower()),
				*JRPGWebUI::ToJSStringLiteral(Nome),
				M.ATK, M.UDF, M.LDF);
		}
	}
	PartyLiteral += TEXT("]");

	return FString::Printf(TEXT("{name:%s,town:%s,gold:%d,buy:%s,sell:%s,party:%s}"),
		*JRPGWebUI::ToJSStringLiteral(ShopName.ToString()),
		*JRPGWebUI::ToJSStringLiteral(Town),
		Core->GetGold(),
		*BuildEntriesLiteral(Stock),
		*BuildEntriesLiteral(Sellable),
		*PartyLiteral);
}

FString UWebUISubsystem::BuildSaveSlotsPayloadJS() const
{
	USaveSubsystem* SaveSubsystem = GetGameInstance()->GetSubsystem<USaveSubsystem>();
	if (!SaveSubsystem)
	{
		return TEXT("[]");
	}

	const TArray<FSaveSlotMetadata> AllSlots = SaveSubsystem->GetAllSlotsMetadata();

	// Array literal JS montado manualmente (padrão OpenDialogue): números crus,
	// strings SEMPRE escapadas via ToJSStringLiteral
	FString Payload = TEXT("[");
	for (int32 i = 0; i < AllSlots.Num(); ++i)
	{
		const FSaveSlotMetadata& Slot = AllSlots[i];
		if (i > 0)
		{
			Payload += TEXT(",");
		}

		if (!Slot.bOccupied)
		{
			Payload += FString::Printf(TEXT("{i:%d,used:false}"), Slot.SlotIndex);
			continue;
		}

		FString PartyLiteral = TEXT("[");
		for (int32 p = 0; p < Slot.Party.Num(); ++p)
		{
			const FSavePartyMemberDisplay& Member = Slot.Party[p];
			if (p > 0)
			{
				PartyLiteral += TEXT(",");
			}
			PartyLiteral += FString::Printf(TEXT("{id:%s,lv:%d,hp:%d,maxhp:%d,mp:%d,maxmp:%d,ap:%d,maxap:%d}"),
				*JRPGWebUI::ToJSStringLiteral(Member.CharacterID.ToString()),
				Member.Level, Member.HP, Member.MaxHP, Member.MP, Member.MaxMP,
				Member.AP, Member.MaxAP);
		}
		PartyLiteral += TEXT("]");

		Payload += FString::Printf(TEXT("{i:%d,used:true,map:%s,time:%s,date:%s,gold:%d,party:%s}"),
			Slot.SlotIndex,
			*JRPGWebUI::ToJSStringLiteral(Slot.MapDisplayName),
			*JRPGWebUI::ToJSStringLiteral(Slot.PlaytimeFormatted),
			*JRPGWebUI::ToJSStringLiteral(Slot.TimestampFormatted),
			Slot.Gold,
			*PartyLiteral);
	}
	Payload += TEXT("]");
	return Payload;
}

void UWebUISubsystem::HandlePostLoadMap(UWorld* NewWorld)
{
	// Só interessa se o shell já estava montado (o LoadMap acabou de derrubar
	// o widget do viewport junto com o PlayerController antigo)
	if (!WebContainerWidget || !NewWorld)
	{
		return;
	}
	if (NewWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WebUISubsystem: Mapa trocado — remontando o shell no mundo novo."));

	// Mesmo caminho provado do PIE 2+: o documento da View global sobrevive;
	// EnsureShellWidget recria o widget e o HandleShellReady roda resetUIShell
	TeardownWidget();
	EnsureShellWidget();
}

void UWebUISubsystem::ExecuteJS(const FString& JSCode)
{
	if (WebContainerWidget)
	{
		WebContainerWidget->ExecuteJS(JSCode);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUISubsystem: Tentativa de executar JavaScript, mas a UI não está aberta: %s"), *JSCode);
	}
}

FUltralightRenderThread* UWebUISubsystem::GetRenderThread() const
{
	return GRenderThread;
}
