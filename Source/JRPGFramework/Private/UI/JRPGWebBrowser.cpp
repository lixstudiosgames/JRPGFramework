#include "UI/JRPGWebBrowser.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SLeafWidget.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Brushes/SlateImageBrush.h"
#include "Rendering/DrawElements.h"
#include "UI/WebUIBridge.h"
#include "UI/WebUISubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

// AllowWindowsPlatformTypes wraps DXGI and D3D11 types
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <Ultralight/Ultralight.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <atomic>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

#include "ID3D11DynamicRHI.h"
#include "RenderingThread.h"
#include "Async/Async.h"
#include "UI/UltralightRenderThread.h"

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "RHIStaticStates.h"

class FUltralightBlitVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FUltralightBlitVS);

	FUltralightBlitVS() {}
	FUltralightBlitVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FUltralightBlitPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FUltralightBlitPS);

	FUltralightBlitPS() {}
	FUltralightBlitPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InputTexture.Bind(Initializer.ParameterMap, TEXT("InputTexture"));
		InputSampler.Bind(Initializer.ParameterMap, TEXT("InputSampler"));
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* InTexture)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetTextureParameter(RHICmdList, ShaderRHI, InputTexture, InputSampler, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InTexture);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, InputTexture);
	LAYOUT_FIELD(FShaderResourceParameter, InputSampler);
};

IMPLEMENT_GLOBAL_SHADER(FUltralightBlitVS, "/Plugin/JRPGFramework/UltralightBlitShader.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FUltralightBlitPS, "/Plugin/JRPGFramework/UltralightBlitShader.usf", "MainPS", SF_Pixel);

// ============================================================================
// FULFrameSync
//
// Estado de consumo de frames compartilhado entre a Game Thread (que agenda a
// cópia) e a RHI/Render Thread (que sabe se ela deu certo). Vive num TSharedPtr
// capturado por valor nos render commands, então sobrevive à destruição do widget
// com comandos em voo.
//
// ConsumedSerial só avança quando o AcquireSync(1) REALMENTE teve sucesso. É essa
// propriedade que elimina o livelock antigo: o código anterior consumia um flag
// booleano na Game Thread e o re-marcava em caso de timeout, o que fabricava
// frames inexistentes e prendia a Render Thread em 8ms por frame de jogo.
// ============================================================================
struct FULFrameSync
{
	std::atomic<uint64> ConsumedSerial { 0 };
};

// ============================================================================
// IDXGIKeyedMutex::AcquireSync devolve WAIT_TIMEOUT (0x00000102) e WAIT_ABANDONED
// (0x00000080) — ambos POSITIVOS, ou seja, SUCCEEDED() é true neles. Testar com
// SUCCEEDED() faria o código copiar a textura sem posse do mutex e, pior, chamar
// ReleaseSync() sobre uma chave que nunca adquiriu, deixando o handshake num
// estado inválido. Só S_OK e WAIT_ABANDONED concedem posse de fato.
// ============================================================================
static FORCEINLINE bool ULKeyedMutexAcquired(HRESULT Hr)
{
	return Hr == S_OK || Hr == static_cast<HRESULT>(0x00000080L); // WAIT_ABANDONED
}

// ============================================================================
// SUltralightBrowser (Widget Slate Interno)
// ============================================================================

class SUltralightBrowser : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SUltralightBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWebUISubsystem* InSubsystem, UJRPGWebBrowser* InOwnerWidget, int32 InWidth, int32 InHeight)
	{
		Subsystem = InSubsystem;
		OwnerWidget = InOwnerWidget;
		Width = InWidth;
		Height = InHeight;
		bSupportsTransparency = true;
		RenderTargetTexture = nullptr;
		CachedSharedHandle = nullptr;
		PendingWidth = 0;
		PendingHeight = 0;
		PendingSizeStableTicks = 0;
		bRecreateInFlight = false;
		LastEnqueuedSerial = 0;
		CopyRetryCooldown = 0;
		FrameSync = MakeShared<FULFrameSync, ESPMode::ThreadSafe>();
		RenderThread = Subsystem ? Subsystem->GetRenderThread() : nullptr;

		if (RenderThread)
		{
			// Registra este widget como consumidor ativo — a UL thread só renderiza
			// e faz o handoff do KeyedMutex enquanto houver pelo menos um consumidor
			RenderThread->AddSurface();

			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::Resize;
			Cmd.X = Width;
			Cmd.Y = Height;
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
		}
	}

	virtual ~SUltralightBrowser()
	{
		// Liberar texturas e recursos de RHI na Render Thread de forma assíncrona
		ENQUEUE_RENDER_COMMAND(CleanupULTextures)([
			SharedTex = MoveTemp(WrappedTextureRHI),
			IntermediateTex = MoveTemp(IntermediateTextureRHI),
			CopyTex = MoveTemp(CopyTextureRHI),
			SharedRes = MoveTemp(D3D11SharedResource),
			Mutex = MoveTemp(KeyedMutex)
		](FRHICommandListImmediate& RHICmdList)
		{
			// Recursos serão deletados ao saírem de escopo no render command
		});

		if (RenderThread)
		{
			// Desconectar o load listener e limpar a ponte
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::BindBridge;
			Cmd.Bridge = nullptr;
			RenderThread->EnqueueCommand(MoveTemp(Cmd));

			// Remove este consumidor — sem consumidores, a UL thread entra em modo ocioso
			RenderThread->RemoveSurface();
		}
	}

	void LoadURL(const FString& InUrl)
	{
		if (RenderThread)
		{
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::LoadURL;
			Cmd.Text = InUrl;
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
		}
	}

	void LoadString(const FString& Contents)
	{
		if (RenderThread)
		{
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::LoadHTML;
			Cmd.Text = Contents;
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
		}
	}

	void ExecuteJS(const FString& JSCode)
	{
		if (RenderThread)
		{
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::ExecuteJS;
			Cmd.Text = JSCode;
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
		}
	}

	void SetSupportsTransparency(bool bInSupportsTransparency)
	{
		bSupportsTransparency = bInSupportsTransparency;
	}

	void BindBridge(UWebUIBridge* InBridge)
	{
		if (RenderThread)
		{
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::BindBridge;
			Cmd.Bridge = InBridge;
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
		}
	}

	// Slate overrides
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		if (!RenderThread)
		{
			return;
		}

		// Detecção dinâmica de redimensionamento de janela/resolução.
		// GetAbsoluteSize() = pixels reais da tela. GetLocalSize() é em Slate units e
		// diverge do viewport quando há DPI curve em UserInterfaceSettings — a View do
		// Ultralight precisa casar com os PIXELS para o texto não sair reescalado.
		const FVector2D AbsSize = AllottedGeometry.GetAbsoluteSize();
		const int32 NewWidth = FMath::RoundToInt(AbsSize.X);
		const int32 NewHeight = FMath::RoundToInt(AbsSize.Y);

		if (NewWidth > 0 && NewHeight > 0 && (NewWidth != Width || NewHeight != Height))
		{
			// Debounce: um resize destrói e recria o RT principal, o que suspende o
			// handoff por alguns frames. Só aceita o tamanho novo depois de ele se
			// repetir, para não disparar um resize por frame durante um drag de janela.
			if (NewWidth == PendingWidth && NewHeight == PendingHeight)
			{
				if (++PendingSizeStableTicks >= 2)
				{
					Width = NewWidth;
					Height = NewHeight;
					PendingSizeStableTicks = 0;

					FULThreadCommand Cmd;
					Cmd.Type = EULCommandType::Resize;
					Cmd.X = Width;
					Cmd.Y = Height;
					RenderThread->EnqueueCommand(MoveTemp(Cmd));
				}
			}
			else
			{
				PendingWidth = NewWidth;
				PendingHeight = NewHeight;
				PendingSizeStableTicks = 0;
			}
		}
		else
		{
			PendingSizeStableTicks = 0;
		}

		{
			// 1. Abrir a shared texture caso o handle tenha mudado (inicialização ou resize)
			void* SharedHandle = RenderThread->GetSharedTextureHandle();
			if (SharedHandle != CachedSharedHandle)
			{
				if (SharedHandle)
				{
					// CachedSharedHandle NÃO é atualizado aqui: ele só avança quando o
					// OpenSharedResource1 confirma sucesso (dentro de RecreateSharedTexture).
					// Antes ele era gravado antes da tentativa, então uma falha de abertura
					// era definitiva — o widget nunca mais tentava e a UI ficava em branco.
					RecreateSharedTexture(SharedHandle);
				}
				else
				{
					CachedSharedHandle = nullptr;

					// RT principal destruído (todo View::Resize faz isso). Solta apenas o
					// que pertence ao Ultralight; a CopyTexture é da UE e guarda o último
					// frame completo, então mantê-la evita a UI piscar até o RT novo ficar
					// pronto. A guarda de WrappedTextureRHI abaixo impede copiar nesse meio.
					ENQUEUE_RENDER_COMMAND(ReleaseULSharedTexture)([
						SharedTex = MoveTemp(WrappedTextureRHI),
						SharedRes = MoveTemp(D3D11SharedResource),
						Mutex = MoveTemp(KeyedMutex)
					](FRHICommandListImmediate& RHICmdList)
					{
						// Recursos liberados ao sair de escopo na Render Thread
					});
				}
			}

			// 2. Se o serial avançou, copia os dados da textura sob mutex e invalida para repaint.
			const uint64 Serial = RenderThread->GetFrameSerial();
			const bool bNeedsCopy = (Serial != FrameSync->ConsumedSerial.load(std::memory_order_acquire));

			bool bEnqueueCopy = false;
			if (bNeedsCopy && WrappedTextureRHI.IsValid() && IntermediateTextureRHI.IsValid() && CopyTextureRHI.IsValid())
			{
				if (Serial != LastEnqueuedSerial)
				{
					// Frame novo: copia imediatamente.
					LastEnqueuedSerial = Serial;
					CopyRetryCooldown = 0;
					bEnqueueCopy = true;
				}
				else if (--CopyRetryCooldown <= 0)
				{
					// Mesmo serial ainda não confirmado: a cópia anterior falhou o poll do
					// mutex ou ainda está em voo. Re-tenta espaçado para não empilhar blits
					// fullscreen com o jogo a centenas de fps. Todo este estado vive na Game
					// Thread — não há contador cross-thread que possa ficar preso e travar
					// o widget, que é justamente a classe de bug que estamos removendo.
					CopyRetryCooldown = 4;
					bEnqueueCopy = true;
				}
			}

			if (bEnqueueCopy)
			{
				ENQUEUE_RENDER_COMMAND(CopyULTexture)([
					SharedTex = WrappedTextureRHI,
					IntermediateTex = IntermediateTextureRHI,
					CopyTex = CopyTextureRHI,
					Mutex = KeyedMutex,
					Sync = FrameSync,
					Serial
				](FRHICommandListImmediate& RHICmdList)
				{
					// Etapa 1 (na RHI Thread, em ordem de stream): shared → intermediária,
					// com CopyResource raw D3D11 CONDICIONADO ao sucesso do Acquire. Sem o
					// mutex, a shared texture pode estar no meio de um redesenho da UL thread
					// (que limpa o RT para transparente antes dos draws) — copiá-la produziria
					// um frame vazio/parcial na tela (flicker sob GPU carregada).
					//
					// Timeout ZERO (poll): em D3D11 não há RHI thread paralela, então este
					// lambda roda na PRÓPRIA Render Thread — bloquear aqui trava o jogo
					// inteiro. Falhou? Nada acontece: ConsumedSerial não avança, a
					// intermediária mantém o último frame completo e o próximo Tick tenta
					// de novo. É esse par (poll + serial) que torna o livelock impossível.
					RHICmdList.EnqueueLambda([Mutex, SharedTex, IntermediateTex, Sync, Serial](FRHICommandListBase&)
					{
						if (Mutex.Get())
						{
							const HRESULT hr = Mutex->AcquireSync(1, 0);
							if (ULKeyedMutexAcquired(hr))
							{
								// Formatos idênticos (B8G8R8A8_TYPELESS) e mesmas dimensões —
								// requisitos do CopyResource
								ID3D11DynamicRHI* D3D11RHI = GetID3D11DynamicRHI();
								D3D11RHI->RHIGetDeviceContext()->CopyResource(
									D3D11RHI->RHIGetResource(IntermediateTex.GetReference()),
									D3D11RHI->RHIGetResource(SharedTex.GetReference()));
								Mutex->ReleaseSync(0);

								// Só aqui o frame conta como consumido. Se o Acquire falhou,
								// ConsumedSerial não avança e o Tick re-agenda a cópia.
								Sync->ConsumedSerial.store(Serial, std::memory_order_release);
							}
						}
					});

					// Etapa 2: blit com dithering intermediária → textura do Slate. Roda mesmo
					// quando o Acquire acima falhou: nesse caso re-blita o último frame completo
					// (custo ínfimo, sem efeito visual) — nunca um frame vazio.
					const uint32 TexWidth = CopyTex->GetSizeX();
					const uint32 TexHeight = CopyTex->GetSizeY();

					FRHIRenderPassInfo RenderPassInfo(CopyTex, ERenderTargetActions::DontLoad_Store);
					RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("UltralightBlit"));
					{
						RHICmdList.SetViewport(0, 0, 0.0f, TexWidth, TexHeight, 1.0f);

						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

						auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
						TShaderMapRef<FUltralightBlitVS> VertexShader(ShaderMap);
						TShaderMapRef<FUltralightBlitPS> PixelShader(ShaderMap);

						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						GraphicsPSOInit.PrimitiveType = PT_TriangleList;

						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

						PixelShader->SetParameters(RHICmdList, IntermediateTex);

						RHICmdList.DrawPrimitive(0, 1, 1);
					}
					RHICmdList.EndRenderPass();
				});

				Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (Brush.IsValid() && RenderTargetTexture)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				Brush.Get(),
				ESlateDrawEffect::PreMultipliedAlpha,
				bSupportsTransparency ? FLinearColor::White : FLinearColor::Black
			);
		}

		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		// Camada de fundo: desenha o que lhe for dado e não impõe tamanho ao pai.
		// Width/Height agora são PIXELS de tela (GetAbsoluteSize), então devolvê-los
		// aqui — onde Slate espera unidades locais — inflaria o layout sob DPI scale.
		return FVector2D::ZeroVector;
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	// --- MAPEAMENTO DE INPUTS SLATE -> FILA RENDERTHREAD ---

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (RenderThread)
		{
			const FVector2D ViewPos = LocalToView(MyGeometry, MouseEvent.GetScreenSpacePosition());
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::MouseDown;
			Cmd.X = FMath::RoundToInt(ViewPos.X);
			Cmd.Y = FMath::RoundToInt(ViewPos.Y);
			Cmd.Button = static_cast<uint32>(TranslateMouseButton(MouseEvent));
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (RenderThread)
		{
			const FVector2D ViewPos = LocalToView(MyGeometry, MouseEvent.GetScreenSpacePosition());
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::MouseUp;
			Cmd.X = FMath::RoundToInt(ViewPos.X);
			Cmd.Y = FMath::RoundToInt(ViewPos.Y);
			Cmd.Button = static_cast<uint32>(TranslateMouseButton(MouseEvent));
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (RenderThread)
		{
			const FVector2D ViewPos = LocalToView(MyGeometry, MouseEvent.GetScreenSpacePosition());
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::MouseMove;
			Cmd.X = FMath::RoundToInt(ViewPos.X);
			Cmd.Y = FMath::RoundToInt(ViewPos.Y);
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (RenderThread)
		{
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::ScrollEvent;
			Cmd.X = 0;
			// GetWheelDelta() = 1.0 por "notch"; ~53px por notch (padrão de 3 linhas do Windows)
			Cmd.Y = FMath::RoundToInt(MouseEvent.GetWheelDelta() * 53.0f);
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (RenderThread)
		{
			if (InKeyEvent.GetKey() == EKeys::Escape)
			{
				FULThreadCommand Cmd;
				Cmd.Type = EULCommandType::ExecuteJS;
				Cmd.Text = TEXT("triggerAnimateOutAndClose();");
				RenderThread->EnqueueCommand(MoveTemp(Cmd));
				return FReply::Handled();
			}

			// GAMEPAD -> ação semântica da UI. O WebCore não tem conceito de
			// gamepad, então o mapeamento acontece aqui e o JS recebe a mesma
			// ação que o teclado gera localmente (handleUIInput). Estes eventos
			// só chegam com o widget focado (menu aberto, FInputModeUIOnly);
			// eventos de repeat passam de propósito (segurar o direcional navega).
			const FKey Key = InKeyEvent.GetKey();
			const TCHAR* UIAction = nullptr;
			if (Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)            UIAction = TEXT("up");
			else if (Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)   UIAction = TEXT("down");
			else if (Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left)   UIAction = TEXT("left");
			else if (Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right) UIAction = TEXT("right");
			else if (Key == EKeys::Gamepad_FaceButton_Bottom)                                   UIAction = TEXT("confirm");
			else if (Key == EKeys::Gamepad_FaceButton_Right)                                    UIAction = TEXT("cancel");
			// LB/RB = trocar de aba (tela de opções). O teclado usa Q/E, tratado
			// localmente no JS (as duas rotas caem no mesmo handleUIInput).
			else if (Key == EKeys::Gamepad_LeftShoulder)                                        UIAction = TEXT("tab_prev");
			else if (Key == EKeys::Gamepad_RightShoulder)                                       UIAction = TEXT("tab_next");

			if (UIAction)
			{
				FULThreadCommand Cmd;
				Cmd.Type = EULCommandType::ExecuteJS;
				Cmd.Text = FString::Printf(TEXT("handleUIInput('%s');"), UIAction);
				RenderThread->EnqueueCommand(MoveTemp(Cmd));
				return FReply::Handled();
			}

			// Demais teclas de gamepad não mapeadas: não fazem sentido como
			// KeyEvent de teclado para o WebCore — consome sem encaminhar
			if (Key.IsGamepadKey())
			{
				return FReply::Handled();
			}

			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::KeyDown;
			Cmd.KeyCode = InKeyEvent.GetKeyCode();
			Cmd.Modifiers = 0;
			if (InKeyEvent.IsShiftDown()) Cmd.Modifiers |= ultralight::KeyEvent::kMod_ShiftKey;
			if (InKeyEvent.IsControlDown()) Cmd.Modifiers |= ultralight::KeyEvent::kMod_CtrlKey;
			if (InKeyEvent.IsAltDown()) Cmd.Modifiers |= ultralight::KeyEvent::kMod_AltKey;

			RenderThread->EnqueueCommand(MoveTemp(Cmd));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (RenderThread)
		{
			// Gamepad é tratado como ação semântica no OnKeyDown — o KeyUp
			// correspondente não deve virar KeyEvent de teclado no WebCore
			if (InKeyEvent.GetKey().IsGamepadKey())
			{
				return FReply::Handled();
			}

			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::KeyUp;
			Cmd.KeyCode = InKeyEvent.GetKeyCode();
			Cmd.Modifiers = 0;
			if (InKeyEvent.IsShiftDown()) Cmd.Modifiers |= ultralight::KeyEvent::kMod_ShiftKey;
			if (InKeyEvent.IsControlDown()) Cmd.Modifiers |= ultralight::KeyEvent::kMod_CtrlKey;
			if (InKeyEvent.IsAltDown()) Cmd.Modifiers |= ultralight::KeyEvent::kMod_AltKey;

			RenderThread->EnqueueCommand(MoveTemp(Cmd));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override
	{
		if (RenderThread)
		{
			FULThreadCommand Cmd;
			Cmd.Type = EULCommandType::KeyChar;
			TCHAR Char = InCharacterEvent.GetCharacter();
			Cmd.Text = FString(1, &Char);
			RenderThread->EnqueueCommand(MoveTemp(Cmd));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	// Converte uma posição de tela para o espaço da View do Ultralight.
	// A View é dimensionada em PIXELS (GetAbsoluteSize), enquanto AbsoluteToLocal
	// devolve unidades locais do Slate — sob DPI scale os dois divergem e o clique
	// cairia deslocado do elemento HTML. Este é o único ponto que faz a ponte.
	FVector2D LocalToView(const FGeometry& Geometry, const FVector2D& ScreenPos) const
	{
		const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPos);
		const FVector2D LocalSize = Geometry.GetLocalSize();
		const FVector2D AbsSize = Geometry.GetAbsoluteSize();

		if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
		{
			return Local;
		}
		return FVector2D(Local.X * (AbsSize.X / LocalSize.X), Local.Y * (AbsSize.Y / LocalSize.Y));
	}

	void RecreateSharedTexture(void* Handle)
	{
		// Um recreate leva um round-trip Game → Render → Game. Sem esta guarda, cada
		// Tick nesse intervalo dispararia outro OpenSharedResource1 para o mesmo handle
		// (a 999fps, dezenas deles) — CachedSharedHandle só avança no fim do caminho.
		if (bRecreateInFlight)
		{
			return;
		}
		bRecreateInFlight = true;

		// Captura FRACA do widget: se ele for destruído (fim da sessão PIE) com este comando
		// em voo, os lambdas abortam em vez de acessar memória liberada
		TWeakPtr<SUltralightBrowser> WeakSelf = SharedThis(this);

		// Captura as texturas atuais (lidas aqui na Game Thread, dona dos membros) para
		// permitir o reuso quando as dimensões não mudaram — uma textura recém-criada é
		// vazia até o primeiro blit e o OnPaint a amostraria nesse intervalo (frame em branco)
		ENQUEUE_RENDER_COMMAND(OpenSharedULTexture)([Handle, WeakSelf,
			ExistingIntermediateTex = IntermediateTextureRHI,
			ExistingCopyTex = CopyTextureRHI](FRHICommandListImmediate& RHICmdList)
		{
			// Qualquer saída sem sucesso precisa devolver o guard na Game Thread —
			// senão bRecreateInFlight ficaria travado e o widget nunca mais tentaria
			// abrir a shared texture, ficando em branco para sempre.
			auto AbortRecreate = [WeakSelf]()
			{
				AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
				{
					if (TSharedPtr<SUltralightBrowser> Self = WeakSelf.Pin())
					{
						Self->bRecreateInFlight = false;
					}
				});
			};

			if (!GDynamicRHI)
			{
				AbortRecreate();
				return;
			}

			const FString RHIName = GDynamicRHI->GetName();
			if (!RHIName.Equals(TEXT("D3D11"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogTemp, Warning, TEXT("SUltralightBrowser: O RHI ativo (%s) nao suporta texturas compartilhadas. Requer D3D11."), *RHIName);
				AbortRecreate();
				return;
			}

			ID3D11Device* UEDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
			if (!UEDevice)
			{
				AbortRecreate();
				return;
			}

			Microsoft::WRL::ComPtr<ID3D11Device1> UEDevice1;
			HRESULT hr = UEDevice->QueryInterface(IID_PPV_ARGS(&UEDevice1));
			if (FAILED(hr))
			{
				AbortRecreate();
				return;
			}

			Microsoft::WRL::ComPtr<ID3D11Texture2D> D3DTexture;
			hr = UEDevice1->OpenSharedResource1(Handle, IID_PPV_ARGS(&D3DTexture));
			if (FAILED(hr))
			{
				// Handle já fechado (RT recriado no meio do caminho) ou inválido.
				// Não é fatal: o guard volta e o próximo Tick tenta com o handle novo.
				UE_LOG(LogTemp, Warning, TEXT("SUltralightBrowser: Falha ao abrir recurso compartilhado (HRESULT=0x%08X) — nova tentativa no proximo Tick."), (unsigned)hr);
				AbortRecreate();
				return;
			}

			Microsoft::WRL::ComPtr<IDXGIKeyedMutex> TempKeyedMutex;
			D3DTexture.As(&TempKeyedMutex);

			ID3D11DynamicRHI* D3D11RHI = GetID3D11DynamicRHI();

			// Obter dimensões reais da textura D3DTexture compartilhada
			D3D11_TEXTURE2D_DESC TexDesc = {};
			D3DTexture->GetDesc(&TexDesc);
			const int32 TexWidth = (int32)TexDesc.Width;
			const int32 TexHeight = (int32)TexDesc.Height;

			// 1. Envolver a textura compartilhada temporariamente como recurso RHI
			FTextureRHIRef TempWrappedTextureRHI = D3D11RHI->RHICreateTexture2DFromResource(
				PF_B8G8R8A8,
				TexCreate_ShaderResource | TexCreate_SRGB,
				FClearValueBinding::None,
				D3DTexture.Get()
			);

			// 2. Criar (ou reutilizar) as texturas do lado UE:
			//    - intermediária: recebe o CopyResource da shared sob o KeyedMutex
			//      (sempre um frame completo) e é a fonte do blit
			//    - cópia: destino do blit com dithering, amostrada pelo Slate
			//    Reutilizar quando as dimensões não mudaram preserva o conteúdo
			//    e evita frames em branco em recreates de handle sem resize
			auto CreateOrReuseTexture = [TexWidth, TexHeight, &RHICmdList](
				const FTextureRHIRef& Existing, const TCHAR* DebugName, ETextureCreateFlags Flags) -> FTextureRHIRef
			{
				if (Existing.IsValid()
					&& Existing->GetSizeX() == (uint32)TexWidth
					&& Existing->GetSizeY() == (uint32)TexHeight)
				{
					return Existing;
				}
				FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(
					DebugName,
					TexWidth, TexHeight,
					PF_B8G8R8A8
				);
				Desc.SetFlags(Flags);
				Desc.SetInitialState(ERHIAccess::SRVGraphics);
				// UE 5.8: FDynamicRHI::RHICreateTexture foi removido e o helper global
				// RHICreateTexture(Desc) ficou deprecado — a criacao passa pela
				// command list, que ja e a immediate deste render command.
				return RHICmdList.CreateTexture(Desc);
			};

			FTextureRHIRef TempIntermediateTextureRHI = CreateOrReuseTexture(
				ExistingIntermediateTex,
				TEXT("UltralightIntermediateTexture"),
				TexCreate_ShaderResource | TexCreate_SRGB);
			FTextureRHIRef TempCopyTextureRHI = CreateOrReuseTexture(
				ExistingCopyTex,
				TEXT("UltralightCopyTexture"),
				TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_SRGB);

			// 3. Vincular de volta para a Game Thread de forma assíncrona para atualizar membros com segurança
			AsyncTask(ENamedThreads::GameThread, [
				WeakSelf,
				Handle,
				D3DTexture,
				TempKeyedMutex,
				TempWrappedTextureRHI,
				TempIntermediateTextureRHI,
				TempCopyTextureRHI,
				TexWidth,
				TexHeight
			]()
			{
				// Pinar o widget: aborta se ele foi destruído enquanto o comando estava em voo
				TSharedPtr<SUltralightBrowser> Self = WeakSelf.Pin();
				if (!Self.IsValid())
				{
					return;
				}

				// Atualizar membros na Game Thread para evitar data races com Tick()
				Self->D3D11SharedResource = D3DTexture;
				Self->KeyedMutex = TempKeyedMutex;
				Self->WrappedTextureRHI = TempWrappedTextureRHI;
				Self->IntermediateTextureRHI = TempIntermediateTextureRHI;
				Self->CopyTextureRHI = TempCopyTextureRHI;

				// Só agora o handle conta como aberto com sucesso.
				Self->CachedSharedHandle = Handle;
				Self->bRecreateInFlight = false;

				// Reutiliza o UTexture2D existente quando as dimensões não mudaram —
				// evita acumular texturas transientes órfãs a cada recreate do handle
				UTexture2D* DynamicTexture = Self->RenderTargetTexture;
				const bool bReuseTexture = DynamicTexture
					&& DynamicTexture->GetSizeX() == TexWidth
					&& DynamicTexture->GetSizeY() == TexHeight;

				if (!bReuseTexture)
				{
					DynamicTexture = UTexture2D::CreateTransient(TexWidth, TexHeight, PF_B8G8R8A8);
					if (DynamicTexture)
					{
						DynamicTexture->SRGB = false;
						DynamicTexture->UpdateResource();
					}
				}

				if (DynamicTexture)
				{
					FTextureResource* Resource = DynamicTexture->GetResource();
					if (Resource)
					{
						ENQUEUE_RENDER_COMMAND(LinkSharedTextureToResource)([Resource, CopyTex = Self->CopyTextureRHI](FRHICommandListImmediate& RHICmdList)
						{
							Resource->TextureRHI = CopyTex;
						});
					}

					Self->RenderTargetTexture = DynamicTexture;
					if (Self->OwnerWidget.IsValid())
					{
						Self->OwnerWidget->SetRenderTargetTexture(DynamicTexture);
					}
					if (!bReuseTexture || !Self->Brush.IsValid())
					{
						Self->Brush = MakeShareable(new FSlateImageBrush(DynamicTexture, FVector2D(TexWidth, TexHeight)));
					}
				}
			});
		});
	}

	static ultralight::MouseEvent::Button TranslateMouseButton(const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) return ultralight::MouseEvent::kButton_Left;
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton) return ultralight::MouseEvent::kButton_Right;
		if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton) return ultralight::MouseEvent::kButton_Middle;
		return ultralight::MouseEvent::kButton_None;
	}

	FUltralightRenderThread* RenderThread;
	UWebUISubsystem* Subsystem;
	TWeakObjectPtr<UJRPGWebBrowser> OwnerWidget;

	void* CachedSharedHandle;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11SharedResource;
	Microsoft::WRL::ComPtr<IDXGIKeyedMutex> KeyedMutex;
	FTextureRHIRef WrappedTextureRHI;
	// Último frame COMPLETO copiado da shared texture sob o KeyedMutex; é a única
	// fonte que o blit amostra — nunca a shared diretamente (que pode estar no meio
	// de um redesenho da UL thread)
	FTextureRHIRef IntermediateTextureRHI;
	FTextureRHIRef CopyTextureRHI;

	mutable UTexture2D* RenderTargetTexture;
	mutable TSharedPtr<FSlateBrush> Brush;

	// Tamanho EFETIVO da View, em pixels de tela (GetAbsoluteSize) — não em Slate units.
	int32 Width;
	int32 Height;

	// Debounce do resize (ver Tick): candidato + quantos ticks seguidos ele se repetiu.
	int32 PendingWidth;
	int32 PendingHeight;
	int32 PendingSizeStableTicks;

	// Impede reentrada em RecreateSharedTexture enquanto o round-trip está em voo.
	bool bRecreateInFlight;

	// Estado de consumo compartilhado com os render commands (sobrevive ao widget).
	TSharedPtr<FULFrameSync, ESPMode::ThreadSafe> FrameSync;

	// Throttle de re-tentativa da cópia — só Game Thread (ver Tick).
	uint64 LastEnqueuedSerial;
	int32 CopyRetryCooldown;

	bool bSupportsTransparency;
};

// ============================================================================
// UJRPGWebBrowser Wrapper UMG
// ============================================================================

UJRPGWebBrowser::UJRPGWebBrowser()
	: RenderTargetTexture(nullptr)
	, UltralightBrowserWidget(nullptr)
	, InitialURL(TEXT("about:blank"))
	, bSupportsTransparency(true)
{
}

void UJRPGWebBrowser::BindUObject(const FString& Name, UObject* Object, bool bRecurse)
{
	if (UltralightBrowserWidget.IsValid())
	{
		UWebUIBridge* Bridge = Cast<UWebUIBridge>(Object);
		if (Bridge)
		{
			UltralightBrowserWidget->BindBridge(Bridge);
		}
	}
}

void UJRPGWebBrowser::SetSupportsTransparency(bool bInSupportsTransparency)
{
	bSupportsTransparency = bInSupportsTransparency;
	if (UltralightBrowserWidget.IsValid())
	{
		UltralightBrowserWidget->SetSupportsTransparency(bInSupportsTransparency);
	}
}

void UJRPGWebBrowser::LoadURL(const FString& URL)
{
	InitialURL = URL;
	if (UltralightBrowserWidget.IsValid())
	{
		UltralightBrowserWidget->LoadURL(URL);
	}
}

void UJRPGWebBrowser::LoadString(const FString& Contents, const FString& DummyURL)
{
	if (UltralightBrowserWidget.IsValid())
	{
		UltralightBrowserWidget->LoadString(Contents);
	}
}

void UJRPGWebBrowser::ExecuteJavascript(const FString& Script)
{
	if (UltralightBrowserWidget.IsValid())
	{
		UltralightBrowserWidget->ExecuteJS(Script);
	}
}

TSharedPtr<SWidget> UJRPGWebBrowser::GetSlateWidget() const
{
	return UltralightBrowserWidget;
}

TSharedRef<SWidget> UJRPGWebBrowser::RebuildWidget()
{
	if (IsDesignTime())
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Ultralight Browser (Design Time)")))
			];
	}
	else
	{
		UGameInstance* GI = GetGameInstance();
		UWebUISubsystem* WebSubsystem = GI ? GI->GetSubsystem<UWebUISubsystem>() : nullptr;
		if (WebSubsystem)
		{
			if (UltralightBrowserWidget.IsValid())
			{
				// A árvore Slate está sendo reconstruída com um SUltralightBrowser já vivo.
				// Isso cria uma segunda superfície consumindo a mesma shared texture e é
				// sintoma de alguém chamando TakeWidget() sobre um UserWidget cujo Slate
				// foi liberado. Não é fatal, mas precisa ficar visível no log.
				UE_LOG(LogTemp, Warning, TEXT("UJRPGWebBrowser: RebuildWidget com SUltralightBrowser ja existente — arvore Slate reconstruida inesperadamente."));
			}

			// Tamanho apenas de BOOTSTRAP. Em BeginPlay o viewport ainda devolve (0,0),
			// e antes esse fallback 1920x1080 era congelado num SBox dentro de um
			// SScaleBox(ScaleToFit) — o que letterboxava a UI inteira num ultrawide.
			// Agora o widget é devolvido cru: ele recebe a geometria completa do
			// viewport e o Tick corrige o tamanho real (em pixels) no primeiro frame.
			FVector2D ViewportSize = FVector2D(1920, 1080);
			if (GEngine && GEngine->GameViewport)
			{
				GEngine->GameViewport->GetViewportSize(ViewportSize);
			}

			int32 Width = FMath::RoundToInt(ViewportSize.X);
			int32 Height = FMath::RoundToInt(ViewportSize.Y);

			if (Width <= 0) Width = 1920;
			if (Height <= 0) Height = 1080;

			UltralightBrowserWidget = SNew(SUltralightBrowser, WebSubsystem, this, Width, Height);
			UltralightBrowserWidget->SetSupportsTransparency(bSupportsTransparency);

			if (!InitialURL.IsEmpty() && InitialURL != TEXT("about:blank"))
			{
				UltralightBrowserWidget->LoadURL(InitialURL);
			}

			return UltralightBrowserWidget.ToSharedRef();
		}

		return SNew(SBox);
	}
}

void UJRPGWebBrowser::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	UltralightBrowserWidget.Reset();
}
