#include "UI/WebUIBridge.h"
#include "UI/WebUISubsystem.h"
#include "Audio/AudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include <Ultralight/Ultralight.h>
#include <JavaScriptCore/JavaScript.h>
#include <atomic>

#ifdef GetBytesPerPixel
#undef GetBytesPerPixel
#endif

// Ponteiro global atômico para a instância ativa do Bridge — necessário para os callbacks JSC
// que são funções file-scope estáticas (não membros de classe)
static std::atomic<UWebUIBridge*> GActiveBridge { nullptr };

// Helper: converte JSValueRef para int32 (-1 se não for número válido)
static int32 JSValToInt32(JSContextRef Ctx, JSValueRef Value)
{
	if (!Value) return -1;
	const double Num = JSValueToNumber(Ctx, Value, nullptr);
	if (FMath::IsNaN(Num)) return -1;
	return static_cast<int32>(Num);
}

// Helper: converte JSValueRef para FString
static FString JSValToFString(JSContextRef Ctx, JSValueRef Value)
{
	if (!Value) return FString();
	JSStringRef JSStr = JSValueToStringCopy(Ctx, Value, nullptr);
	if (!JSStr) return FString();
	size_t Length = JSStringGetLength(JSStr);
	const JSChar* Chars = JSStringGetCharactersPtr(JSStr);
	FString Result(Length, reinterpret_cast<const UTF16CHAR*>(Chars));
	JSStringRelease(JSStr);
	return Result;
}

// Helper para encaminhar a execução para a Game Thread de forma segura e thread-safe
template<typename FuncType>
static void ExecuteOnGameThread(FuncType&& Func)
{
	if (IsEngineExitRequested()) return;

	UWebUIBridge* ActiveBridge = GActiveBridge.load(std::memory_order_acquire);
	if (ActiveBridge)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakBridge = TWeakObjectPtr<UWebUIBridge>(ActiveBridge), Func = MoveTemp(Func)]()
		{
			if (WeakBridge.IsValid())
			{
				Func(WeakBridge.Get());
			}
		});
	}
}

// Callbacks JSC como funções file-scope estáticas (sem expor tipos JSC no .h)
static JSValueRef JSC_Echo(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		// Monta a resposta localmente, sem chamar métodos de UObject na UL thread
		// (chamar Bridge->Echo() aqui corria contra o GC da Game Thread).
		// Mesmo texto de retorno de UWebUIBridge::Echo — comportamento preservado.
		FString Msg = JSValToFString(ctx, argv[0]);
		UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Recebido Echo do JS: '%s'"), *Msg);
		FString Ret = FString::Printf(TEXT("%s (retorno do Unreal C++)"), *Msg);
		JSStringRef RetStr = JSStringCreateWithCharacters(reinterpret_cast<const JSChar*>(*Ret), Ret.Len());
		JSValueRef RetVal = JSValueMakeString(ctx, RetStr);
		JSStringRelease(RetStr);
		return RetVal;
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnUIReady(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*)
{
	ExecuteOnGameThread([](UWebUIBridge* Bridge)
	{
		Bridge->OnUIReady();
	});
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnMenuOptionSelected(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		FString Option = JSValToFString(ctx, argv[0]);
		ExecuteOnGameThread([Option](UWebUIBridge* Bridge)
		{
			Bridge->OnMenuOptionSelected(Option);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_CloseMenu(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*)
{
	ExecuteOnGameThread([](UWebUIBridge* Bridge)
	{
		Bridge->CloseMenu();
	});
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnSaveSlot(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		// Só extrai o int e despacha — nunca tocar UObject na UL thread
		const int32 SlotIndex = JSValToInt32(ctx, argv[0]);
		ExecuteOnGameThread([SlotIndex](UWebUIBridge* Bridge)
		{
			Bridge->OnSaveSlotSelected(SlotIndex);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnLoadSlot(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		const int32 SlotIndex = JSValToInt32(ctx, argv[0]);
		ExecuteOnGameThread([SlotIndex](UWebUIBridge* Bridge)
		{
			Bridge->OnLoadSlotSelected(SlotIndex);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnShopBuy(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 1)
	{
		// Só extrai os dados e despacha — nunca tocar UObject na UL thread
		FString ItemID = JSValToFString(ctx, argv[0]);
		const int32 Quantity = JSValToInt32(ctx, argv[1]);
		ExecuteOnGameThread([ItemID, Quantity](UWebUIBridge* Bridge)
		{
			Bridge->OnShopBuy(ItemID, Quantity);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnShopSell(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 1)
	{
		FString ItemID = JSValToFString(ctx, argv[0]);
		const int32 Quantity = JSValToInt32(ctx, argv[1]);
		ExecuteOnGameThread([ItemID, Quantity](UWebUIBridge* Bridge)
		{
			Bridge->OnShopSell(ItemID, Quantity);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnPartyToggle(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 1)
	{
		FString Who = JSValToFString(ctx, argv[0]);
		const bool bAtivar = JSValueToBoolean(ctx, argv[1]);
		ExecuteOnGameThread([Who, bAtivar](UWebUIBridge* Bridge)
		{
			Bridge->OnPartyToggle(Who, bAtivar);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnItemDiscard(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 1)
	{
		FString Id = JSValToFString(ctx, argv[0]);
		const int32 Qtd = static_cast<int32>(JSValueToNumber(ctx, argv[1], nullptr));
		ExecuteOnGameThread([Id, Qtd](UWebUIBridge* Bridge)
		{
			Bridge->OnItemDiscard(Id, Qtd);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnDevCommand(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		FString Command = JSValToFString(ctx, argv[0]);
		ExecuteOnGameThread([Command](UWebUIBridge* Bridge)
		{
			Bridge->OnDevCommand(Command);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnMainMenuAction(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		FString Action = JSValToFString(ctx, argv[0]);
		ExecuteOnGameThread([Action](UWebUIBridge* Bridge)
		{
			Bridge->OnMainMenuAction(Action);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnUIStateChanged(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		FString State = JSValToFString(ctx, argv[0]);
		ExecuteOnGameThread([State](UWebUIBridge* Bridge)
		{
			Bridge->OnUIStateChanged(State);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OnSetDifficulty(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		const int32 Index = JSValToInt32(ctx, argv[0]);
		ExecuteOnGameThread([Index](UWebUIBridge* Bridge)
		{
			Bridge->OnSetDifficulty(Index);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_SetCursorVisible(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		const bool bVisible = JSValueToBoolean(ctx, argv[0]);
		ExecuteOnGameThread([bVisible](UWebUIBridge* Bridge)
		{
			Bridge->SetCursorVisible(bVisible);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_OpenURL(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		FString URL = JSValToFString(ctx, argv[0]);
		ExecuteOnGameThread([URL](UWebUIBridge* Bridge)
		{
			Bridge->OpenURL(URL);
		});
	}
	return JSValueMakeUndefined(ctx);
}

static JSValueRef JSC_PlaySFX(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef*)
{
	if (argc > 0)
	{
		FString SoundName = JSValToFString(ctx, argv[0]);
		ExecuteOnGameThread([SoundName](UWebUIBridge* Bridge)
		{
			Bridge->PlaySFX(SoundName);
		});
	}
	return JSValueMakeUndefined(ctx);
}

UWebUIBridge::UWebUIBridge()
	: WebUISubsystem(nullptr)
{
}

void UWebUIBridge::Initialize(UWebUISubsystem* InSubsystem)
{
	WebUISubsystem = InSubsystem;
	GActiveBridge.store(this, std::memory_order_release);
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Ponte de comunicação C++ inicializada com sucesso."));
}

void UWebUIBridge::ClearActiveBridge()
{
	GActiveBridge.store(nullptr, std::memory_order_release);
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Ponte global limpa (ClearActiveBridge)."));
}

void UWebUIBridge::BeginDestroy()
{
	UWebUIBridge* Expected = this;
	GActiveBridge.compare_exchange_strong(Expected, nullptr, std::memory_order_acq_rel);
	Super::BeginDestroy();
}

void UWebUIBridge::BindNativeFunctions(void* RawView)
{
	if (!RawView) return;
	GActiveBridge.store(this, std::memory_order_release);

	ultralight::View* ULView = static_cast<ultralight::View*>(RawView);
	ultralight::RefPtr<ultralight::JSContext> Context = ULView->LockJSContext();
	JSContextRef Ctx = Context->ctx();
	JSObjectRef GlobalObject = JSContextGetGlobalObject(Ctx);

	// Cria um objeto JS simples para "uebridge" e vincula as funções estáticas
	JSClassDefinition ClassDef;
	FMemory::Memzero(&ClassDef, sizeof(JSClassDefinition));
	ClassDef.className = "UEBridgeClass";
	JSClassRef BridgeClass = JSClassCreate(&ClassDef);
	JSObjectRef BridgeObj = JSObjectMake(Ctx, BridgeClass, nullptr);
	JSClassRelease(BridgeClass);

	auto AttachFn = [&](const char* Name, JSObjectCallAsFunctionCallback Fn)
	{
		JSStringRef NameStr = JSStringCreateWithUTF8CString(Name);
		JSObjectRef FnObj = JSObjectMakeFunctionWithCallback(Ctx, NameStr, Fn);
		JSObjectSetProperty(Ctx, BridgeObj, NameStr, FnObj, kJSPropertyAttributeNone, nullptr);
		JSStringRelease(NameStr);
	};

	AttachFn("echo",                 JSC_Echo);
	AttachFn("onuiready",            JSC_OnUIReady);
	AttachFn("onmenuoptionselected", JSC_OnMenuOptionSelected);
	AttachFn("closemenu",            JSC_CloseMenu);
	AttachFn("playsfx",              JSC_PlaySFX);
	AttachFn("onsaveslot",           JSC_OnSaveSlot);
	AttachFn("onloadslot",           JSC_OnLoadSlot);
	AttachFn("onshopbuy",            JSC_OnShopBuy);
	AttachFn("onshopsell",           JSC_OnShopSell);
	AttachFn("onmainmenuaction",     JSC_OnMainMenuAction);
	AttachFn("onuistatechanged",     JSC_OnUIStateChanged);
	AttachFn("setcursorvisible",     JSC_SetCursorVisible);
	AttachFn("onsetdifficulty",      JSC_OnSetDifficulty);
	AttachFn("openurl",              JSC_OpenURL);
	AttachFn("ondevcommand",         JSC_OnDevCommand);
	AttachFn("onpartytoggle",        JSC_OnPartyToggle);
	AttachFn("onitemdiscard",        JSC_OnItemDiscard);

	// 1. Cria o objeto "ue" (window.ue) para compatibilidade nativa com scripts CEF antigos
	JSObjectRef UeObj = JSObjectMake(Ctx, nullptr, nullptr);

	// 2. Associa "uebridge" como propriedade de "ue" (window.ue.uebridge)
	JSStringRef BridgePropName = JSStringCreateWithUTF8CString("uebridge");
	JSObjectSetProperty(Ctx, UeObj, BridgePropName, BridgeObj, kJSPropertyAttributeNone, nullptr);
	JSStringRelease(BridgePropName);

	// 3. Expõe "ue" no escopo global (window.ue)
	JSStringRef UeGlobalName = JSStringCreateWithUTF8CString("ue");
	JSObjectSetProperty(Ctx, GlobalObject, UeGlobalName, UeObj, kJSPropertyAttributeNone, nullptr);
	JSStringRelease(UeGlobalName);

	// 4. Também expõe diretamente no escopo global como window.uebridge
	JSStringRef BridgeName = JSStringCreateWithUTF8CString("uebridge");
	JSObjectSetProperty(Ctx, GlobalObject, BridgeName, BridgeObj, kJSPropertyAttributeNone, nullptr);
	JSStringRelease(BridgeName);

	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: 'window.ue.uebridge' e 'window.uebridge' registrados com sucesso no JS."));
}

FString UWebUIBridge::Echo(const FString& Message)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Recebido Echo do JS: '%s'"), *Message);
	return FString::Printf(TEXT("%s (retorno do Unreal C++)"), *Message);
}

void UWebUIBridge::OnUIReady()
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: O JS notificou que a interface está pronta."));
	if (WebUISubsystem)
	{
		// Marca o shell como pronto e flusha os comandos JS enfileirados
		WebUISubsystem->HandleShellReady();
	}
}

void UWebUIBridge::OnMenuOptionSelected(const FString& OptionName)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Opção do menu selecionada no JS: '%s'"), *OptionName);

	if (!WebUISubsystem)
	{
		return;
	}

	// Options, Save, Load e Quit seguem o mesmo padrão: o JS já escondeu os
	// cards do menu (o estado continua MenuOpen) e o C++ abre a tela por cima,
	// de forma que cancelar devolva o jogador ao menu de pausa.
	if (OptionName == TEXT("options"))
	{
		WebUISubsystem->OpenOptions();
		return;
	}

	if (OptionName == TEXT("party"))
	{
		WebUISubsystem->OpenPartyScreen();
		return;
	}

	if (OptionName == TEXT("items"))
	{
		WebUISubsystem->OpenItemsScreen();
		return;
	}

	if (OptionName == TEXT("save"))
	{
		WebUISubsystem->OpenSaveLoadFromMenu(/*bLoadTab=*/false);
		return;
	}

	if (OptionName == TEXT("load"))
	{
		WebUISubsystem->OpenSaveLoadFromMenu(/*bLoadTab=*/true);
		return;
	}

	// Quit: o JS já confirmou com o jogador (overlay Yes/No) antes de chegar aqui.
	if (OptionName == TEXT("quit"))
	{
		WebUISubsystem->QuitToMainMenu();
	}
}

void UWebUIBridge::OnPartyToggle(const FString& Character, bool bActivate)
{
	if (WebUISubsystem)
	{
		WebUISubsystem->HandlePartyToggle(FName(*Character), bActivate);
	}
}

void UWebUIBridge::OnItemDiscard(const FString& ItemID, int32 Quantity)
{
	if (WebUISubsystem)
	{
		WebUISubsystem->HandleItemDiscard(FName(*ItemID), Quantity);
	}
}

void UWebUIBridge::OnDevCommand(const FString& Command)
{
	if (WebUISubsystem)
	{
		WebUISubsystem->RunDevCommand(Command);
	}
}

void UWebUIBridge::OnSetDifficulty(int32 DifficultyIndex)
{
	if (WebUISubsystem)
	{
		WebUISubsystem->SetDifficultyFromUI(DifficultyIndex);
	}
}

void UWebUIBridge::SetCursorVisible(bool bVisible)
{
	if (WebUISubsystem)
	{
		WebUISubsystem->SetUICursorVisible(bVisible);
	}
}

void UWebUIBridge::CloseMenu()
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Fechamento do menu solicitado pelo JS."));
	if (WebUISubsystem)
	{
		// Arquitetura shell: o documento NUNCA descarrega — o JS já escondeu a
		// seção; aqui só restauramos o input mode/foco do jogo.
		WebUISubsystem->HandleCloseRequestFromJS();
	}
}

void UWebUIBridge::OnMainMenuAction(const FString& Action)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Ação do main menu solicitada pelo JS: '%s'."), *Action);
	if (WebUISubsystem)
	{
		WebUISubsystem->HandleMainMenuActionFromJS(Action);
	}
}

void UWebUIBridge::OnUIStateChanged(const FString& State)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: O JS trocou de tela sozinho: '%s'."), *State);
	if (WebUISubsystem)
	{
		WebUISubsystem->HandleUIStateChangedFromJS(State);
	}
}

void UWebUIBridge::OpenURL(const FString& URL)
{
	// Só http(s): a URL vem do JS e vira uma chamada ao shell do sistema
	if (!URL.StartsWith(TEXT("http://")) && !URL.StartsWith(TEXT("https://")))
	{
		UE_LOG(LogTemp, Warning, TEXT("WebUIBridge: openurl('%s') recusado — só http/https são aceitos."), *URL);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Abrindo '%s' no navegador do sistema."), *URL);
	FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
}

void UWebUIBridge::OnSaveSlotSelected(int32 SlotIndex)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Save no slot %d solicitado pelo JS."), SlotIndex);
	if (WebUISubsystem)
	{
		WebUISubsystem->HandleSaveSlotFromJS(SlotIndex);
	}
}

void UWebUIBridge::OnLoadSlotSelected(int32 SlotIndex)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Load do slot %d solicitado pelo JS."), SlotIndex);
	if (WebUISubsystem)
	{
		WebUISubsystem->HandleLoadSlotFromJS(SlotIndex);
	}
}

void UWebUIBridge::OnShopBuy(const FString& ItemID, int32 Quantity)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Compra solicitada pelo JS: %dx '%s'."), Quantity, *ItemID);
	if (WebUISubsystem)
	{
		WebUISubsystem->HandleShopBuyFromJS(ItemID, Quantity);
	}
}

void UWebUIBridge::OnShopSell(const FString& ItemID, int32 Quantity)
{
	UE_LOG(LogTemp, Log, TEXT("WebUIBridge: Venda solicitada pelo JS: %dx '%s'."), Quantity, *ItemID);
	if (WebUISubsystem)
	{
		WebUISubsystem->HandleShopSellFromJS(ItemID, Quantity);
	}
}

void UWebUIBridge::PlaySFX(const FString& SoundName)
{
	// Traduz nomes do JS para os arquivos reais de SFX do projeto
	FString RealSoundName = SoundName;
	if (SoundName.Equals(TEXT("Open"), ESearchCase::IgnoreCase))       RealSoundName = TEXT("SFX_Open");
	else if (SoundName.Equals(TEXT("Close"), ESearchCase::IgnoreCase))  RealSoundName = TEXT("SFX_Close");
	else if (SoundName.Equals(TEXT("Next"), ESearchCase::IgnoreCase))   RealSoundName = TEXT("SFX_Next");
	else if (SoundName.Equals(TEXT("Cancel"), ESearchCase::IgnoreCase)) RealSoundName = TEXT("SFX_Cancel");
	else if (SoundName.Equals(TEXT("Select"), ESearchCase::IgnoreCase)) RealSoundName = TEXT("SFX_Select");
	else if (SoundName.Equals(TEXT("Item"), ESearchCase::IgnoreCase))   RealSoundName = TEXT("SFX_Item");

	USoundBase* Sound = nullptr;
	
	// Busca rápida no cache em memória antes de tentar ler do disco
	USoundBase** CachedSoundPtr = SoundCache.Find(RealSoundName);
	if (CachedSoundPtr)
	{
		Sound = *CachedSoundPtr;
	}
	else
	{
		// O editor cria automaticamente assets uasset para os .wav importados no Content/UI/SFX
		FString AssetPath = FString::Printf(TEXT("/JRPGFramework/UI/SFX/%s.%s"), *RealSoundName, *RealSoundName);
		Sound = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr, *AssetPath));
		if (Sound)
		{
			SoundCache.Add(RealSoundName, Sound);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("WebUIBridge: Falha ao carregar efeito sonoro nativo em '%s'"), *AssetPath);
		}
	}

	if (Sound)
	{
		// Sons da UI obedecem os sliders de volume (Master × SFX) automaticamente
		float VolumeMultiplier = 1.0f;
		if (WebUISubsystem)
		{
			if (UAudioSubsystem* Audio = WebUISubsystem->GetGameInstance()->GetSubsystem<UAudioSubsystem>())
			{
				VolumeMultiplier = Audio->GetEffectiveSFXVolume();
			}
		}
		UGameplayStatics::PlaySound2D(this, Sound, VolumeMultiplier);
	}
}
