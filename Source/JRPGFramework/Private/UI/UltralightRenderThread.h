#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"

// Re-enable Win32 types for Ultralight/AppCore
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <Ultralight/Ultralight.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

class FUltralightGPUDriverD3D11;
class UWebUIBridge;

enum class EULCommandType : uint8
{
	MouseDown,
	MouseUp,
	MouseMove,
	KeyDown,
	KeyUp,
	KeyChar,
	ScrollEvent,
	LoadURL,
	LoadHTML,
	ExecuteJS,
	BindBridge,
	Resize,
	Shutdown
};

struct FULThreadCommand
{
	EULCommandType Type;
	int32 X = 0;
	int32 Y = 0;
	uint32 Button = 0;
	uint32 KeyCode = 0;
	uint32 Modifiers = 0;
	FString Text;
	TWeakObjectPtr<UWebUIBridge> Bridge;
};

class FUltralightRenderThread : public FRunnable
{
public:
	FUltralightRenderThread();
	virtual ~FUltralightRenderThread();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	// Public API called from Game Thread
	void EnqueueCommand(FULThreadCommand&& Cmd);

	// Bloqueia (com timeout) até a UL thread drenar todos os comandos já enfileirados.
	// Usado no Deinitialize para garantir que BindBridge(nullptr) foi processado
	// antes do Bridge (UObject) poder ser coletado pelo GC.
	bool FlushCommands(float TimeoutSeconds = 0.1f);

	// Contagem de superfícies consumidoras (SUltralightBrowser vivos).
	// Sem consumidor, a UL thread pausa Render/apresentação (economia de CPU/GPU)
	// mantendo apenas Renderer::Update() (timers/JS continuam vivos).
	void AddSurface();
	void RemoveSurface();

	// Shutdown definitivo (exit do engine): além de parar a thread, destrói
	// View/Renderer/GPUDriver globais dentro da própria UL thread.
	void RequestFinalShutdown();

	// Outputs called from Game/Render thread
	void* GetSharedTextureHandle() const;

	// Serial monotônico de frames publicados: incrementado pela UL thread APÓS
	// cada ReleaseSync(1) bem-sucedido. O consumidor guarda o último serial que
	// conseguiu copiar e só tenta de novo quando este valor diverge.
	//
	// Substituiu o antigo par ConsumeNewFrame()/MarkFrameDirty(): aquele flag
	// booleano era consumido ANTES do AcquireSync e re-marcado em timeout, o que
	// (a) perdia o frame quando o consumidor abortava sem re-marcar e (b) fabricava
	// um frame inexistente quando o Acquire falhava por não haver nada a consumir —
	// gerando um livelock que bloqueava a Render Thread 8ms por frame de jogo.
	// Com o serial, o retry é implícito (o valor simplesmente não é confirmado) e
	// múltiplos consumidores não roubam frames uns dos outros.
	uint64 GetFrameSerial() const;

	uint32 GetRenderTargetTextureId() const;

	// Configuration
	FString PluginDir;
	int32 ViewWidth = 1920;
	int32 ViewHeight = 1080;
	bool bTransparent = true;
	FString InitialURL;

	// LUID do adapter do device da UE (setado pelo WebUISubsystem antes do start).
	// Garante que o device D3D11 do Ultralight seja criado na mesma GPU da UE.
	bool bHasAdapterLuid = false;
	int32 AdapterLuidHigh = 0;
	uint32 AdapterLuidLow = 0;

private:
	TQueue<FULThreadCommand, EQueueMode::Mpsc> CommandQueue;

	FEvent* WakeEvent;
	std::atomic<bool> bRequestStop;
	std::atomic<bool> bFinalShutdown { false };

	std::atomic<void*> SharedTextureHandle;
	std::atomic<uint32_t> RenderTargetTextureId;
	std::atomic<uint64> FrameSerial;

	std::atomic<int32> ActiveSurfaceCount { 0 };

	// Contadores para FlushCommands (produtor incrementa Enqueued; consumidor, Processed)
	std::atomic<uint64> EnqueuedCommandCount { 0 };
	std::atomic<uint64> ProcessedCommandCount { 0 };

	// Tamanho atual efetivo da View/RT — fonte única de verdade para dedup de resize
	// (substitui os antigos statics de função, que compartilhavam estado entre caminhos)
	int32 CurrentViewWidth = 0;
	int32 CurrentViewHeight = 0;

	ultralight::RefPtr<ultralight::Renderer> Renderer;
	TSharedPtr<FUltralightGPUDriverD3D11> GPUDriver;
	class FULThreadLoadListener* LoadListener;
};
