#include "UltralightRenderThread.h"

// UltralightGPUDriverD3D11.h gerencia seus proprios guards de plataforma internamente
#include "UltralightGPUDriverD3D11.h"

// AppCore/Platform.h pode precisar de tipos Windows
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <AppCore/Platform.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

#include "UI/WebUIBridge.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Logging/LogMacros.h"

static TSharedPtr<FUltralightGPUDriverD3D11> GGPUDriver = nullptr;
static ultralight::RefPtr<ultralight::Renderer> GULRenderer = nullptr;

// FontLoader e FileSystem — criados uma unica vez (stateless, seguros de reusar)
static ultralight::FontLoader* GFontLoader = nullptr;
static ultralight::FileSystem* GFileSystem = nullptr;

// View — mantida viva entre sessoes PIE para evitar corrupcao de estado interno do WebCore
static ultralight::RefPtr<ultralight::View> GView = nullptr;

// IDXGIKeyedMutex::AcquireSync devolve WAIT_TIMEOUT (0x00000102) e WAIT_ABANDONED
// (0x00000080) — ambos POSITIVOS, ou seja, SUCCEEDED() é true neles. Testar com
// SUCCEEDED() faria a thread desenhar sem posse do mutex e chamar ReleaseSync(1)
// sobre uma chave que nunca adquiriu, corrompendo o handshake com a UE.
// Só S_OK e WAIT_ABANDONED concedem posse de fato.
static FORCEINLINE bool ULKeyedMutexAcquired(HRESULT Hr)
{
	return Hr == S_OK || Hr == static_cast<HRESULT>(0x00000080L); // WAIT_ABANDONED
}

// ============================================================================
// FULThreadLoadListener
//
// CONTRATO DE LIFETIME (thread-safety com o GC):
// Este listener roda na UL thread e toca um UObject (Bridge). Isso só é seguro
// porque: (1) o Bridge é rooteado pelo UWebUISubsystem (UPROPERTY) durante toda
// a sessão; e (2) UWebUISubsystem::Deinitialize enfileira BindBridge(nullptr) e
// aguarda via FlushCommands() — o listener é destruído na UL thread ANTES do
// Bridge poder ser coletado pelo GC. Não usar este padrão com UObjects que não
// tenham esse contrato de root garantido.
// ============================================================================

class FULThreadLoadListener : public ultralight::LoadListener
{
public:
	FULThreadLoadListener(UWebUIBridge* InBridge) : Bridge(InBridge) {}

	virtual void OnWindowObjectReady(ultralight::View* caller,
	                                 uint64_t frame_id,
	                                 bool is_main_frame,
	                                 const ultralight::String& url) override
	{
		if (is_main_frame && Bridge.IsValid())
		{
			Bridge->BindNativeFunctions(static_cast<void*>(caller));
		}
	}

	virtual void OnDOMReady(ultralight::View* caller,
	                        uint64_t frame_id,
	                        bool is_main_frame,
	                        const ultralight::String& url) override
	{
		if (is_main_frame && Bridge.IsValid())
		{
			Bridge->BindNativeFunctions(static_cast<void*>(caller));
		}
	}

private:
	TWeakObjectPtr<UWebUIBridge> Bridge;
};

// ============================================================================
// FUltralightRenderThread Construtor / Destrutor
// ============================================================================

FUltralightRenderThread::FUltralightRenderThread()
	: WakeEvent(nullptr)
	, bRequestStop(false)
	, SharedTextureHandle(nullptr)
	, RenderTargetTextureId(0)
	, FrameSerial(0)
	, LoadListener(nullptr)
{
	WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FUltralightRenderThread::~FUltralightRenderThread()
{
	if (WakeEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		WakeEvent = nullptr;
	}
	if (LoadListener)
	{
		delete LoadListener;
		LoadListener = nullptr;
	}
}

// ============================================================================
// FRunnable Lifecycle
// ============================================================================

bool FUltralightRenderThread::Init()
{
	return true;
}

uint32 FUltralightRenderThread::Run()
{
	UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: Iniciando thread dedicada de render."));

	// 1. Criar GPUDriver e Renderer na primeira sessao; resetar nas sessoes seguintes
	if (!GGPUDriver.IsValid())
	{
		GGPUDriver = MakeShared<FUltralightGPUDriverD3D11>();

		// LUID do adapter da UE (setado pelo subsystem) — cria o device na mesma GPU
		LUID AdapterLuid = {};
		AdapterLuid.HighPart = AdapterLuidHigh;
		AdapterLuid.LowPart = AdapterLuidLow;

		if (!GGPUDriver->Initialize(bHasAdapterLuid ? &AdapterLuid : nullptr))
		{
			UE_LOG(LogTemp, Error, TEXT("UltralightRenderThread: Falha ao inicializar o GPUDriver D3D11 global."));
			GGPUDriver = nullptr;
			return 0;
		}

		ultralight::Config Config;
		FString ResourcesPath = FPaths::Combine(PluginDir, TEXT("Content"), TEXT("ThirdParty"), TEXT("Ultralight"), TEXT("resources"));
		ResourcesPath = FPaths::ConvertRelativePathToFull(ResourcesPath);
		ResourcesPath += TEXT("/");
		Config.resource_path_prefix = TCHAR_TO_UTF8(*ResourcesPath);

		ultralight::Platform::instance().set_config(Config);

		// FontLoader/FileSystem — criar apenas 1 vez (stateless, nao precisam ser recriados)
		if (!GFontLoader)
		{
			GFontLoader = ultralight::GetPlatformFontLoader();
			ultralight::Platform::instance().set_font_loader(GFontLoader);
		}
		if (!GFileSystem)
		{
			GFileSystem = ultralight::GetPlatformFileSystem(TCHAR_TO_UTF8(*PluginDir));
			ultralight::Platform::instance().set_file_system(GFileSystem);
		}

		ultralight::Platform::instance().set_gpu_driver(GGPUDriver.Get());

		GULRenderer = ultralight::Renderer::Create();
		if (!GULRenderer)
		{
			UE_LOG(LogTemp, Error, TEXT("UltralightRenderThread: Falha ao criar o Renderer do Ultralight global."));
			GGPUDriver->Shutdown();
			GGPUDriver = nullptr;
			return 0;
		}

		UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: Renderer e GPUDriver criados com sucesso (Inicializacao Unica de Processo)."));
	}
	else
	{
		// Sessoes PIE seguintes: reusar device/Renderer (acumular recursos e seguro)
		// Re-registrar no Platform (raw pointer — inofensivo re-setar mesmo valor)
		ultralight::Platform::instance().set_gpu_driver(GGPUDriver.Get());

		// Reaplicar config (pode ter mudado entre sessoes)
		ultralight::Config Config;
		FString ResourcesPath = FPaths::Combine(PluginDir, TEXT("Content"), TEXT("ThirdParty"), TEXT("Ultralight"), TEXT("resources"));
		ResourcesPath = FPaths::ConvertRelativePathToFull(ResourcesPath);
		ResourcesPath += TEXT("/");
		Config.resource_path_prefix = TCHAR_TO_UTF8(*ResourcesPath);
		ultralight::Platform::instance().set_config(Config);

		UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: Reaproveitando Renderer e GPUDriver existentes."));
	}

	GPUDriver = GGPUDriver;
	Renderer = GULRenderer;

	const bool bSizeChanged = (ViewWidth != CurrentViewWidth || ViewHeight != CurrentViewHeight);

	// Configurar tamanho alvo do RT principal apenas se necessario (evita recriar RT desnecessariamente)
	if (!GView || bSizeChanged)
	{
		GPUDriver->SetMainRTSize(ViewWidth, ViewHeight);
		CurrentViewWidth = ViewWidth;
		CurrentViewHeight = ViewHeight;
	}

	// 2. View global — criar na 1a sessao ou redimensionar apenas se o tamanho mudou
	ultralight::ViewConfig ViewConfig;
	ViewConfig.is_transparent = bTransparent;
	ViewConfig.is_accelerated = true;

	const bool bViewExisted = (GView.get() != nullptr);

	if (!GView)
	{
		GView = Renderer->CreateView(ViewWidth, ViewHeight, ViewConfig, nullptr);
		if (!GView)
		{
			UE_LOG(LogTemp, Error, TEXT("UltralightRenderThread: Falha ao criar a View do Ultralight."));
			return 0;
		}
		UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: View global criada."));
	}
	else if (bSizeChanged)
	{
		GView->Resize(ViewWidth, ViewHeight);
		UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: View global redimensionada para %dx%d."), ViewWidth, ViewHeight);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: View global reaproveitada sem redimensionar (tamanho inalterado)."));
	}

	// PIE 2+: View ja foi resetada no cleanup do PIE anterior (about:blank antes do break)
	if (bViewExisted)
	{
		UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: View global reaproveitada (ja resetada no cleanup anterior)."));
	}

	// Limpar listener anterior (sera redefinido via comando BindBridge quando o menu abrir)
	GView->set_load_listener(nullptr);

	// 4. Carregar URL inicial se fornecida
	if (!InitialURL.IsEmpty())
	{
		GView->LoadURL(TCHAR_TO_UTF8(*InitialURL));
	}

	double LastRenderTime = 0.0;
	const double TargetDelta = 1.0 / 60.0; // 60fps limit (~16.6ms)

	// Rate-limit dos avisos de contenção/estado inválido: sem isso, um mutex
	// preso encheria o log com uma linha a cada 16ms.
	double LastWarnTime = 0.0;
	const double WarnInterval = 1.0;

	// 5. Loop principal de renderização
	while (!bRequestStop.load(std::memory_order_relaxed))
	{
		// Drenar fila de comandos da Game Thread
		uint64 NumDequeued = 0;
		FULThreadCommand Cmd;
		while (CommandQueue.Dequeue(Cmd))
		{
			++NumDequeued;
			switch (Cmd.Type)
			{
				case EULCommandType::MouseDown:
				{
					ultralight::MouseEvent Evt;
					Evt.type = ultralight::MouseEvent::kType_MouseDown;
					Evt.x = Cmd.X;
					Evt.y = Cmd.Y;
					Evt.button = static_cast<ultralight::MouseEvent::Button>(Cmd.Button);
					if (GView) GView->FireMouseEvent(Evt);
					break;
				}
				case EULCommandType::MouseUp:
				{
					ultralight::MouseEvent Evt;
					Evt.type = ultralight::MouseEvent::kType_MouseUp;
					Evt.x = Cmd.X;
					Evt.y = Cmd.Y;
					Evt.button = static_cast<ultralight::MouseEvent::Button>(Cmd.Button);
					if (GView) GView->FireMouseEvent(Evt);
					break;
				}
				case EULCommandType::MouseMove:
				{
					ultralight::MouseEvent Evt;
					Evt.type = ultralight::MouseEvent::kType_MouseMoved;
					Evt.x = Cmd.X;
					Evt.y = Cmd.Y;
					Evt.button = ultralight::MouseEvent::kButton_None;
					if (GView) GView->FireMouseEvent(Evt);
					break;
				}
				case EULCommandType::KeyDown:
				{
					ultralight::KeyEvent Evt;
					Evt.type = ultralight::KeyEvent::kType_RawKeyDown;
					Evt.virtual_key_code = Cmd.KeyCode;
					Evt.native_key_code = Cmd.KeyCode;
					Evt.modifiers = Cmd.Modifiers;
					if (GView) GView->FireKeyEvent(Evt);
					break;
				}
				case EULCommandType::KeyUp:
				{
					ultralight::KeyEvent Evt;
					Evt.type = ultralight::KeyEvent::kType_KeyUp;
					Evt.virtual_key_code = Cmd.KeyCode;
					Evt.native_key_code = Cmd.KeyCode;
					Evt.modifiers = Cmd.Modifiers;
					if (GView) GView->FireKeyEvent(Evt);
					break;
				}
				case EULCommandType::KeyChar:
				{
					ultralight::KeyEvent Evt;
					Evt.type = ultralight::KeyEvent::kType_Char;
					Evt.text = TCHAR_TO_UTF8(*Cmd.Text);
					Evt.unmodified_text = TCHAR_TO_UTF8(*Cmd.Text);
					if (GView) GView->FireKeyEvent(Evt);
					break;
				}
				case EULCommandType::ScrollEvent:
				{
					ultralight::ScrollEvent Evt;
					Evt.type = ultralight::ScrollEvent::kType_ScrollByPixel;
					Evt.delta_x = Cmd.X;
					Evt.delta_y = Cmd.Y;
					if (GView) GView->FireScrollEvent(Evt);
					break;
				}
				case EULCommandType::LoadURL:
				{
					if (GView) GView->LoadURL(TCHAR_TO_UTF8(*Cmd.Text));
					break;
				}
				case EULCommandType::LoadHTML:
				{
					if (GView) GView->LoadHTML(TCHAR_TO_UTF8(*Cmd.Text));
					break;
				}
				case EULCommandType::ExecuteJS:
				{
					if (GView) GView->EvaluateScript(TCHAR_TO_UTF8(*Cmd.Text));
					break;
				}
				case EULCommandType::BindBridge:
				{
					if (GView)
					{
						if (Cmd.Bridge.IsValid())
						{
							if (LoadListener)
							{
								delete LoadListener;
							}
							LoadListener = new FULThreadLoadListener(Cmd.Bridge.Get());
							GView->set_load_listener(LoadListener);
							Cmd.Bridge->BindNativeFunctions(GView.get());
						}
						else
						{
							GView->set_load_listener(nullptr);
							if (LoadListener)
							{
								delete LoadListener;
								LoadListener = nullptr;
							}
						}
					}
					break;
				}
				case EULCommandType::Resize:
				{
					// Dedup contra o tamanho efetivo atual (fonte única — mesma usada na criação da View)
					if (Cmd.X > 0 && Cmd.Y > 0 && (Cmd.X != CurrentViewWidth || Cmd.Y != CurrentViewHeight))
					{
						if (GPUDriver.IsValid())
						{
							GPUDriver->SetMainRTSize(Cmd.X, Cmd.Y);
						}
						if (GView)
						{
							GView->Resize(Cmd.X, Cmd.Y);
						}
						CurrentViewWidth = Cmd.X;
						CurrentViewHeight = Cmd.Y;
					}
					break;
				}
				case EULCommandType::Shutdown:
				{
					bRequestStop.store(true, std::memory_order_relaxed);
					break;
				}
			}
		}

		// Publicar contagem de comandos processados (consumidor único desta fila)
		if (NumDequeued > 0)
		{
			ProcessedCommandCount.fetch_add(NumDequeued, std::memory_order_release);
		}

		if (bRequestStop.load(std::memory_order_relaxed))
		{
			break;
		}

		// Sem nenhum SUltralightBrowser vivo (UI fechada), não há consumidor para o
		// frame: pula Render/apresentação e o handoff do KeyedMutex. Renderer::Update()
		// continua rodando para manter timers/JS/carregamentos vivos (estado preservado).
		const bool bHasSurfaces = ActiveSurfaceCount.load(std::memory_order_relaxed) > 0;

		// Cadência de render limitada a 60fps INDEPENDENTE da chegada de comandos.
		// O WakeEvent é auto-reset e todo EnqueueCommand o dispara, então com o mouse
		// a ~1000Hz o Wait() abaixo retornava imediatamente todo ciclo e a UL thread
		// free-runava junto com o jogo. Separar "drenar comandos" (sempre, para input
		// responsivo) de "renderizar" (só a cada TargetDelta) é o que segura o teto.
		const double Now = FPlatformTime::Seconds();
		const bool bShouldRender = bHasSurfaces && (Now - LastRenderTime >= TargetDelta);

		// Atualizar o Renderer. Update() roda SEMPRE (timers/JS/rAF/carregamentos).
		if (Renderer)
		{
			Renderer->Update();

			// BACK-PRESSURE: não gerar uma CommandList nova enquanto a anterior não
			// foi desenhada. UpdateCommandList() sobrescreve PendingCommands, e o
			// Ultralight considera aquele damage consumido e não o reemite — sem
			// esta guarda, todo Render() cuja DrawCommandList não rodou (timeout do
			// mutex, RT inválido, Render Thread travada compilando shaders) perdia
			// o repaint PARA SEMPRE, deixando a RTT em branco.
			if (bShouldRender && !(GPUDriver && GPUDriver->HasPendingCommands()))
			{
				Renderer->RefreshDisplay(0);
				Renderer->Render();
				LastRenderTime = Now;
			}
		}

		// Desenhar a CommandList do Driver D3D11 utilizando o KeyedMutex
		if (bHasSurfaces && GPUDriver && GPUDriver->IsRenderTargetReady())
		{
			// Publicar o handle compartilhado assim que mudar (inicialização/resize) —
			// leitura/escrita atômica, independente do estado do mutex
			void* CurrentHandle = GPUDriver->GetSharedTextureHandle();
			if (SharedTextureHandle.load(std::memory_order_relaxed) != CurrentHandle)
			{
				SharedTextureHandle.store(CurrentHandle, std::memory_order_release);
				RenderTargetTextureId.store(GPUDriver->GetMainRTTextureId(), std::memory_order_release);
			}

			// PROTOCOLO DO KEYEDMUTEX — só tocar no mutex quando HÁ algo a desenhar.
			// O ciclo válido é: UL adquire(0) → desenha → libera(1) → ++FrameSerial →
			// UE adquire(1) → copia → libera(0). Se a UL adquirisse/liberasse SEM
			// desenhar, o mutex ficaria preso na key 1 (a UE só o retoma quando o
			// serial avança) e o próximo paint real estouraria timeout a cada iteração.
			//
			// O incremento do serial vem DEPOIS do ReleaseSync(1): quando o consumidor
			// enxerga um serial novo, a key 1 já está disponível — sem essa ordem, ele
			// tentaria adquirir uma chave que a UL ainda segura.
			if (GPUDriver->HasPendingCommands())
			{
				IDXGIKeyedMutex* KeyedMutex = GPUDriver->GetKeyedMutex();
				if (KeyedMutex)
				{
					// Adquire o mutex sob a chave 0 (a ser liberado com a chave 1 para a UE Render Thread)
					// Timeout generoso: o consumidor da UE agora faz poll não-bloqueante
					// (AcquireSync(1, 0)), então uma Render Thread congestionada só atrasa
					// o handoff — nunca o impede. Esperar aqui é barato (thread dedicada)
					// e evita descartar o batch por contenção momentânea.
					const HRESULT hr = KeyedMutex->AcquireSync(0, 100);
					if (ULKeyedMutexAcquired(hr))
					{
						const bool bDidDraw = GPUDriver->DrawCommandList();
						KeyedMutex->ReleaseSync(1);

						// Só publica um serial novo quando o Ultralight de fato pintou algo.
						// Evita blit fullscreen por frame na Render Thread da UE com página estática.
						if (bDidDraw)
						{
							FrameSerial.fetch_add(1, std::memory_order_release);
						}
					}
					else if (Now - LastWarnTime >= WarnInterval)
					{
						LastWarnTime = Now;
						UE_LOG(LogTemp, Warning,
							TEXT("UltralightRenderThread: Falha ao adquirir o KeyedMutex (key=0) na UL Thread (HRESULT=0x%08X). Frame de UI adiado."),
							(unsigned)hr);
					}
				}
				else if (Now - LastWarnTime >= WarnInterval)
				{
					// RT principal registrado mas sem KeyedMutex: bookkeeping obsoleto.
					// Antes este caminho era um skip 100% silencioso e a UI congelava
					// sem deixar rastro nenhum no log.
					LastWarnTime = Now;
					UE_LOG(LogTemp, Warning,
						TEXT("UltralightRenderThread: KeyedMutex indisponivel para o RT principal (id=%u) — comandos pendentes nao serao desenhados."),
						GPUDriver->GetMainRTTextureId());
				}
			}
		}

		// Controlar cadência. Comandos que chegaram durante este ciclo têm prioridade:
		// volta ao topo para drená-los imediatamente (o branch antigo fazia o oposto —
		// dormia justamente quando a fila NÃO estava vazia, atrasando o input).
		if (!CommandQueue.IsEmpty())
		{
			continue;
		}

		// Ocioso (sem consumidor): 50ms. Com consumidor: dorme só até o próximo tick
		// de render. Comandos pendentes (batch não desenhado) não podem esperar o
		// TargetDelta inteiro — o handoff acima precisa re-tentar o mutex logo.
		double WaitSeconds = 0.05;
		if (bHasSurfaces)
		{
			const bool bDrainPending = (GPUDriver && GPUDriver->HasPendingCommands());
			WaitSeconds = bDrainPending
				? 0.001
				: FMath::Clamp(LastRenderTime + TargetDelta - FPlatformTime::Seconds(), 0.0, TargetDelta);
		}

		// Sempre esperar pelo menos 1ms quando há algo a aguardar. Um Sleep(0.0) aqui
		// viraria busy-spin a 100% de CPU no caso em que o batch pendente não consegue
		// drenar (mutex em contenção) — exatamente o cenário que queremos sobreviver.
		if (WaitSeconds > 0.0)
		{
			if (WakeEvent)
			{
				WakeEvent->Wait(FMath::Max(1u, (uint32)(WaitSeconds * 1000.0)));
			}
			else
			{
				FPlatformProcess::Sleep((float)WaitSeconds);
			}
		}
		else
		{
			FPlatformProcess::Sleep(0.0f); // Cede o restante do time slice
		}
	}

	// 6. Cleanup — desvincular listener e limpar locais (View global intacta)
	if (GView)
	{
		GView->set_load_listener(nullptr);
	}
	if (LoadListener)
	{
		delete LoadListener;
		LoadListener = nullptr;
	}

	// Limpar ponteiros locais da thread (preservar globais GULRenderer, GGPUDriver, GFontLoader, GFileSystem, GView)
	Renderer = nullptr;
	GPUDriver = nullptr;

	// 7. Shutdown definitivo (exit do engine): destruir os singletons NA UL THREAD,
	// na ordem correta (View → Renderer → GPUDriver), antes das DLLs do Ultralight
	// poderem ser descarregadas no teardown do processo. Entre sessões PIE este
	// bloco NÃO executa (bFinalShutdown só é setado pelo OnEnginePreExit).
	if (bFinalShutdown.load(std::memory_order_acquire))
	{
		GView = nullptr;
		GULRenderer = nullptr;
		ultralight::Platform::instance().set_gpu_driver(nullptr);
		if (GGPUDriver.IsValid())
		{
			GGPUDriver->Shutdown();
		}
		GGPUDriver = nullptr;
		UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: Shutdown definitivo — View, Renderer e GPUDriver destruidos."));
	}

	UE_LOG(LogTemp, Log, TEXT("UltralightRenderThread: Finalizando e limpando recursos da thread dedicada."));
	return 0;
}

void FUltralightRenderThread::Stop()
{
	bRequestStop.store(true, std::memory_order_release);
	if (WakeEvent)
	{
		WakeEvent->Trigger();
	}
}

void FUltralightRenderThread::Exit()
{
}

// ============================================================================
// Public APIs
// ============================================================================

void FUltralightRenderThread::EnqueueCommand(FULThreadCommand&& Cmd)
{
	EnqueuedCommandCount.fetch_add(1, std::memory_order_release);
	CommandQueue.Enqueue(MoveTemp(Cmd));
	if (WakeEvent)
	{
		WakeEvent->Trigger();
	}
}

bool FUltralightRenderThread::FlushCommands(float TimeoutSeconds)
{
	const uint64 Target = EnqueuedCommandCount.load(std::memory_order_acquire);
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;

	if (WakeEvent)
	{
		WakeEvent->Trigger();
	}

	while (ProcessedCommandCount.load(std::memory_order_acquire) < Target)
	{
		if (FPlatformTime::Seconds() >= Deadline)
		{
			UE_LOG(LogTemp, Warning, TEXT("UltralightRenderThread: FlushCommands expirou aguardando a UL thread drenar a fila."));
			return false;
		}
		FPlatformProcess::Sleep(0.001f);
	}
	return true;
}

void FUltralightRenderThread::AddSurface()
{
	ActiveSurfaceCount.fetch_add(1, std::memory_order_relaxed);
	if (WakeEvent)
	{
		WakeEvent->Trigger();
	}
}

void FUltralightRenderThread::RemoveSurface()
{
	ActiveSurfaceCount.fetch_sub(1, std::memory_order_relaxed);
}

void FUltralightRenderThread::RequestFinalShutdown()
{
	bFinalShutdown.store(true, std::memory_order_release);
	Stop();
}

void* FUltralightRenderThread::GetSharedTextureHandle() const
{
	return SharedTextureHandle.load(std::memory_order_acquire);
}

uint64 FUltralightRenderThread::GetFrameSerial() const
{
	return FrameSerial.load(std::memory_order_acquire);
}

uint32 FUltralightRenderThread::GetRenderTargetTextureId() const
{
	return RenderTargetTextureId.load(std::memory_order_acquire);
}
