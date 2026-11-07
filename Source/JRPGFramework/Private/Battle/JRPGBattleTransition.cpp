#include "JRPGBattleTransition.h"

#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UnrealClient.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	constexpr int32 PsxW = 320;
	constexpr int32 PsxH = 240;
	/** A página da captura do original tem 256 linhas para as 240 do quadro (o resto é preto). */
	constexpr int32 PageH = 256;
	/** A captura mais larga que isto é reduzida (o custo do ReadPixels e da textura num monitor 4K). */
	constexpr int32 MaxCaptureWidth = 1920;
	/** O GPU do PS1 pula o triângulo mais largo que isto (1023 em x, 511 em y). */
	constexpr float MaxSpanX = 1023.0f;
	constexpr float MaxSpanY = 511.0f;

	uint8 ToByte(float Value)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255));
	}

	/** O overlay: só pede ao dono para desenhar; não pega mouse nem foco. */
	class SJRPGBattleTransitionOverlay : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SJRPGBattleTransitionOverlay) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UJRPGBattleTransition* InOwner)
		{
			Owner = InOwner;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const UJRPGBattleTransition* Transition = Owner.Get();
			return Transition ? Transition->Paint(AllottedGeometry, OutDrawElements, LayerId) : LayerId;
		}

	protected:
		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
		{
			return FVector2D(PsxW, PsxH);
		}

	private:
		TWeakObjectPtr<UJRPGBattleTransition> Owner;
	};
}

// =================================================================================================
// Começo e fim
// =================================================================================================

bool UJRPGBattleTransition::Start(UWorld* World, int32 Style, int32 Sub, int32 CurtainScale, const FJRPGBattleTransitionTiming& InTiming,
	TFunction<void(bool)> OnFinished, TFunction<void()> OnReveal)
{
	Stop();
	UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
	if (!Client || !Client->Viewport || !FSlateApplication::IsInitialized() || !CaptureViewport(Client, CurtainScale))
	{
		return false;
	}

	Effect.Begin(Style, Sub, static_cast<uint32>(FMath::Rand()) * 2654435761u + 0x1234u);
	TotalFrames = FJRPGBattleIntroEffect::DurationFrames(Effect.GetStyle());
	if (Effect.IsCurtain())
	{
		// A cortina trabalha na captura reduzida (a escala de trabalho); a textura dela nasce da imagem do primeiro quadro
		Effect.Step(true);
		CurtainTexture = MakeTexture(Effect.GetCurtainWidth(), Effect.GetCurtainHeight(), Effect.GetCurtainImage());
		CurtainBrush = FSlateBrush();
		CurtainBrush.SetResourceObject(CurtainTexture);
		CurtainBrush.ImageSize = FVector2D(Effect.GetCurtainWidth(), Effect.GetCurtainHeight());
	}
	else
	{
		// O primeiro quadro já na tela: o estilo em repouso refaz o Field exatamente
		Effect.Step(true);
	}

	FinishedCallback = MoveTemp(OnFinished);
	RevealCallback = MoveTemp(OnReveal);
	Timing = InTiming;
	Timing.PlaybackRate = FMath::Max(Timing.PlaybackRate, 0.05f);
	Timing.HoldSeconds = FMath::Max(Timing.HoldSeconds, 0.0f);
	Timing.RevealSeconds = FMath::Max(Timing.RevealSeconds, 0.0f);
	ViewportClient = Client;
	Accumulator = 0.0;
	bFinished = false;
	bEndWhite = false;
	AfterSeconds = 0.0f;
	bRevealed = false;
	bRunning = true;

	// Por cima de tudo (a WebUI fica no 100): o original é dono da tela inteira enquanto a transição roda
	Overlay = SNew(SJRPGBattleTransitionOverlay, this).Visibility(EVisibility::HitTestInvisible);
	Client->AddViewportWidgetContent(Overlay.ToSharedRef(), 200);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UJRPGBattleTransition::TickTransition));
	return true;
}

void UJRPGBattleTransition::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (Overlay.IsValid())
	{
		if (UGameViewportClient* Client = ViewportClient.Get())
		{
			Client->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
		Overlay.Reset();
	}
	ViewportClient = nullptr;
	FinishedCallback = nullptr;
	RevealCallback = nullptr;
	bRunning = false;
	Capture = nullptr;
	CurtainTexture = nullptr;
	CaptureBrush = FSlateBrush();
	CurtainBrush = FSlateBrush();
}

void UJRPGBattleTransition::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

bool UJRPGBattleTransition::TickTransition(float DeltaSeconds)
{
	if (!bRunning)
	{
		return false;
	}
	const float Delta = FMath::Min(DeltaSeconds, 0.25f);
	if (!bFinished)
	{
		// O relógio do efeito é o de tela do port: 60 quadros por segundo (vezes PlaybackRate), em tempo real (a luta e o Field estão
		// parados)
		Accumulator += Delta * Timing.PlaybackRate;
		constexpr double StepSeconds = 1.0 / 60.0;
		int32 Steps = 0;
		while (Accumulator >= StepSeconds && Steps < 4 && Effect.GetFrame() < TotalFrames)
		{
			Accumulator -= StepSeconds;
			++Steps;
			Effect.Step(true);
			if (Effect.IsCurtain())
			{
				UploadCurtain();
			}
		}
		if (Effect.GetFrame() < TotalFrames)
		{
			return true;
		}
		// A tela inteira na cor final: a luta monta agora, debaixo do overlay
		bFinished = true;
		AfterSeconds = 0.0f;
		int32 Level = 0;
		FJRPGBattleIntroEffect::FadeAt(Effect.GetStyle(), Effect.GetSub(), TotalFrames, Level, bEndWhite);
		TFunction<void(bool)> Callback = MoveTemp(FinishedCallback);
		FinishedCallback = nullptr;
		if (Callback)
		{
			Callback(bEndWhite);
		}
		return bRunning;
	}

	// A espera na cor final (a carga do original) e o fade que abre a luta
	AfterSeconds += Delta;
	if (!bRevealed && AfterSeconds >= Timing.HoldSeconds)
	{
		bRevealed = true;
		TFunction<void()> Callback = MoveTemp(RevealCallback);
		RevealCallback = nullptr;
		if (Callback)
		{
			Callback();
		}
		if (!bRunning)
		{
			return false;
		}
	}
	if (AfterSeconds >= Timing.HoldSeconds + Timing.RevealSeconds)
	{
		Stop();
		return false;
	}
	return true;
}

// =================================================================================================
// A captura
// =================================================================================================

UTexture2D* UJRPGBattleTransition::MakeTexture(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
	{
		return nullptr;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None,
		TConstArrayView64<uint8>(reinterpret_cast<const uint8*>(Pixels.GetData()), static_cast<int64>(Pixels.Num()) * sizeof(FColor)));
	if (!Texture)
	{
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->UpdateResource();
	return Texture;
}

bool UJRPGBattleTransition::CaptureViewport(UGameViewportClient* Client, int32 CurtainScale)
{
	// O original copia o framebuffer mostrado (DrawSync + MoveImage + StoreImage): aqui o último quadro do viewport
	FViewport* Viewport = Client->Viewport;
	TArray<FColor> Pixels;
	const FIntPoint Size = Viewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0 || !Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleTransition: a captura do viewport falhou — a luta entra pelo fade"));
		return false;
	}

	// Uma tela grande demais é reduzida (de amostra em amostra; a transição se mexe rápido demais para precisar de filtro)
	const int32 Skip = FMath::Max(1, FMath::DivideAndRoundUp(Size.X, MaxCaptureWidth));
	const int32 W = Size.X / Skip;
	const int32 H = Size.Y / Skip;
	// A página: as linhas do quadro, as que faltam para 256 / 240 em preto (as UVs das últimas fileiras caem nelas, como no PS1) e
	// quatro brancas no fim (o texel das primitivas sem textura)
	const int32 PageRows = FMath::RoundToInt(H * static_cast<float>(PageH) / PsxH);
	const int32 TexH = PageRows + 4;
	TArray<FColor> Page;
	Page.Init(FColor(0, 0, 0, 255), W * TexH);
	for (int32 y = 0; y < H; ++y)
	{
		const FColor* Src = &Pixels[(y * Skip) * Size.X];
		FColor* Dst = &Page[y * W];
		for (int32 x = 0; x < W; ++x)
		{
			FColor C = Src[x * Skip];
			C.A = 255;
			Dst[x] = C;
		}
	}
	for (int32 i = PageRows * W; i < Page.Num(); ++i)
	{
		Page[i] = FColor::White;
	}
	Capture = MakeTexture(W, TexH, Page);
	if (!Capture)
	{
		return false;
	}
	CaptureBrush = FSlateBrush();
	CaptureBrush.SetResourceObject(Capture);
	CaptureBrush.ImageSize = FVector2D(W, TexH);
	CaptureRowsV = static_cast<float>(PageRows) / TexH;
	WhiteV = (PageRows + 2.0f) / TexH;

	// A cortina: a mesma captura reduzida para 320 x 240 vezes a escala de trabalho
	const int32 Scale = FMath::Clamp(CurtainScale, 1, 6);
	const int32 CW = PsxW * Scale;
	const int32 CH = PsxH * Scale;
	TArray<FColor> Curtain;
	Curtain.SetNumUninitialized(CW * CH);
	for (int32 y = 0; y < CH; ++y)
	{
		const int32 Sy = FMath::Min(y * H / CH, H - 1);
		for (int32 x = 0; x < CW; ++x)
		{
			const int32 Sx = FMath::Min(x * W / CW, W - 1);
			Curtain[y * CW + x] = Page[Sy * W + Sx];
		}
	}
	Effect.SetCurtainSource(Curtain, CW, CH);
	return true;
}

void UJRPGBattleTransition::UploadCurtain()
{
	const TArray<FColor>& Image = Effect.GetCurtainImage();
	const int32 W = Effect.GetCurtainWidth();
	const int32 H = Effect.GetCurtainHeight();
	if (!CurtainTexture || Image.Num() != W * H)
	{
		return;
	}
	const int64 Bytes = static_cast<int64>(Image.Num()) * sizeof(FColor);
	uint8* Data = static_cast<uint8*>(FMemory::Malloc(Bytes));
	FMemory::Memcpy(Data, Image.GetData(), Bytes);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, W, H);
	CurtainTexture->UpdateTextureRegions(0, 1, Region, W * sizeof(FColor), sizeof(FColor), Data,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SrcData);
			delete Regions;
		});
}

// =================================================================================================
// O desenho
// =================================================================================================

int32 UJRPGBattleTransition::Paint(const FGeometry& Geometry, FSlateWindowElementList& OutElements, int32 LayerId) const
{
	if (!bRunning || !Capture || !FSlateApplication::IsInitialized())
	{
		return LayerId;
	}
	FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
	if (!Renderer)
	{
		return LayerId;
	}
	const FVector2f Size(Geometry.GetLocalSize());
	const FSlateRenderTransform& Xf = Geometry.GetAccumulatedRenderTransform();
	const FVector2f WhiteUV(0.5f, WhiteV);

	TArray<FSlateVertex> Verts;
	TArray<SlateIndex> Indices;
	auto ToLocal = [&Size](const FVector2f& P) { return FVector2f(P.X / PsxW * Size.X, P.Y / PsxH * Size.Y); };
	auto AddVert = [&](const FVector2f& Local, const FVector2f& UV, const FColor& Color)
	{
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Xf, Local, UV, Color));
		return static_cast<SlateIndex>(Verts.Num() - 1);
	};
	// Um retângulo da tela inteira (o fundo preto, o fade)
	auto AddScreenQuad = [&](const FVector2f& UV, const FColor& Color)
	{
		const SlateIndex A = AddVert(FVector2f(0, 0), UV, Color);
		const SlateIndex B = AddVert(FVector2f(Size.X, 0), UV, Color);
		const SlateIndex C = AddVert(FVector2f(0, Size.Y), UV, Color);
		const SlateIndex D = AddVert(FVector2f(Size.X, Size.Y), UV, Color);
		Indices.Append({ A, B, C, B, C, D });
	};
	auto Flush = [&](const FSlateBrush& Brush, int32 Layer)
	{
		if (Indices.Num() > 0)
		{
			FSlateDrawElement::MakeCustomVerts(OutElements, Layer, Renderer->GetResourceHandle(Brush), Verts, Indices, nullptr, 0, 0,
				ESlateDrawEffect::PreMultipliedAlpha);
		}
		Verts.Reset();
		Indices.Reset();
	};

	// Depois do efeito: só a cor final, cheia na espera e abrindo no fade (a luta embaixo)
	int32 Layer = LayerId;
	if (bFinished)
	{
		float Opacity = 1.0f;
		if (AfterSeconds > Timing.HoldSeconds)
		{
			Opacity = Timing.RevealSeconds > 0.0f ? 1.0f - FMath::Clamp((AfterSeconds - Timing.HoldSeconds) / Timing.RevealSeconds, 0.0f, 1.0f) : 0.0f;
		}
		const uint8 A = ToByte(Opacity);
		// Pré-multiplicado: a cor já vezes o alpha
		const uint8 C = bEndWhite ? A : 0;
		AddScreenQuad(WhiteUV, FColor(C, C, C, A));
		Flush(CaptureBrush, Layer++);
		return Layer;
	}

	// O original desenha sobre preto: a transição é dona do quadro inteiro (o Field vivo embaixo não aparece)
	AddScreenQuad(WhiteUV, FColor(0, 0, 0, 255));
	if (Effect.IsCurtain())
	{
		Flush(CaptureBrush, Layer++);
		if (CurtainTexture)
		{
			const SlateIndex A = AddVert(FVector2f(0, 0), FVector2f(0, 0), FColor::White);
			const SlateIndex B = AddVert(FVector2f(Size.X, 0), FVector2f(1, 0), FColor::White);
			const SlateIndex C = AddVert(FVector2f(0, Size.Y), FVector2f(0, 1), FColor::White);
			const SlateIndex D = AddVert(FVector2f(Size.X, Size.Y), FVector2f(1, 1), FColor::White);
			Indices.Append({ A, B, C, B, C, D });
			Flush(CurtainBrush, Layer++);
		}
	}
	else
	{
		for (const FJRPGIntroPrim& Prim : Effect.GetPrims())
		{
			// Os três blends pelo alpha do modo pré-multiplicado: 1 opaco, 0 aditivo, 0,5 a meia mistura (a cor já vezes 0,5)
			FColor Colors[4];
			FVector2f UV[4];
			for (int32 k = 0; k < 4; ++k)
			{
				const FVector3f& C = Prim.Color[k];
				switch (Prim.Mode)
				{
				case FJRPGIntroPrim::EMode::Opaque:
					Colors[k] = FColor(ToByte(C.X), ToByte(C.Y), ToByte(C.Z), 255);
					break;
				case FJRPGIntroPrim::EMode::AddTextured:
				case FJRPGIntroPrim::EMode::AddShade:
					Colors[k] = FColor(ToByte(C.X), ToByte(C.Y), ToByte(C.Z), 0);
					break;
				case FJRPGIntroPrim::EMode::Half:
				default:
					Colors[k] = FColor(ToByte(C.X * 0.5f), ToByte(C.Y * 0.5f), ToByte(C.Z * 0.5f), 128);
					break;
				}
				const bool bTextured = Prim.Mode == FJRPGIntroPrim::EMode::Opaque || Prim.Mode == FJRPGIntroPrim::EMode::AddTextured;
				UV[k] = bTextured ? FVector2f(Prim.UV[k].X / PsxW, Prim.UV[k].Y / PageH * CaptureRowsV) : WhiteUV;
			}
			// O GPU desenha o quad como (0, 1, 2) e (1, 2, 3) e pula o triângulo largo demais
			const int32 Tris[2][3] = { { 0, 1, 2 }, { 1, 2, 3 } };
			for (const auto& Tri : Tris)
			{
				float MinX = TNumericLimits<float>::Max(), MaxX = TNumericLimits<float>::Lowest();
				float MinY = TNumericLimits<float>::Max(), MaxY = TNumericLimits<float>::Lowest();
				for (const int32 i : Tri)
				{
					MinX = FMath::Min(MinX, Prim.XY[i].X);
					MaxX = FMath::Max(MaxX, Prim.XY[i].X);
					MinY = FMath::Min(MinY, Prim.XY[i].Y);
					MaxY = FMath::Max(MaxY, Prim.XY[i].Y);
				}
				if (MaxX - MinX > MaxSpanX || MaxY - MinY > MaxSpanY)
				{
					continue;
				}
				for (const int32 i : Tri)
				{
					Indices.Add(AddVert(ToLocal(Prim.XY[i]), UV[i], Colors[i]));
				}
			}
		}
	}

	// O fade do fim: o branco aditivo (ABR 1) é exato pelo alpha 0; o preto subtrativo (ABR 2) vira um escurecer pelo alpha
	int32 Level = 0;
	bool bWhite = false;
	if (FJRPGBattleIntroEffect::FadeAt(Effect.GetStyle(), Effect.GetSub(), Effect.GetFrame(), Level, bWhite))
	{
		const uint8 L = static_cast<uint8>(Level);
		AddScreenQuad(WhiteUV, bWhite ? FColor(L, L, L, 0) : FColor(0, 0, 0, L));
	}
	Flush(CaptureBrush, Layer++);
	return Layer;
}
