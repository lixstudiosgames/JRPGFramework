#include "UltralightGPUDriverD3D11.h"

// Shaders pré-compilados (arrays de bytecode gerados pelo FXC)
#include "../../../../Content/ThirdParty/Ultralight/shaders/hlsl/bin/v2f_c4f_t2f_fxc.h"
#include "../../../../Content/ThirdParty/Ultralight/shaders/hlsl/bin/v2f_c4f_t2f_t2f_d28f_fxc.h"
#include "../../../../Content/ThirdParty/Ultralight/shaders/hlsl/bin/fill_fxc.h"
#include "../../../../Content/ThirdParty/Ultralight/shaders/hlsl/bin/fill_path_fxc.h"
// Ultralight types completos necessários no .cpp (Bitmap, Renderer, etc.)
#include <Ultralight/Ultralight.h>

#include "Logging/LogMacros.h"

// ============================================================================
// Helpers internos
// ============================================================================

static bool CheckHR(HRESULT hr, const TCHAR* Context)
{
    if (SUCCEEDED(hr)) return true;
    UE_LOG(LogTemp, Error, TEXT("UltralightGPUDriver: falha HRESULT em '%s' (0x%08X)"), Context, (unsigned)hr);
    return false;
}

#define UL_HR(expr) CheckHR((expr), TEXT(#expr))

// ============================================================================
// Construtor / Destrutor
// ============================================================================

FUltralightGPUDriverD3D11::FUltralightGPUDriverD3D11() {}

FUltralightGPUDriverD3D11::~FUltralightGPUDriverD3D11()
{
    Shutdown();
}

// ============================================================================
// Initialize
// ============================================================================

bool FUltralightGPUDriverD3D11::Initialize(const LUID* AdapterLuid)
{
    D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL ActualLevel     = {};
    UINT Flags = 0;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    // Debug layer ativa em Dev/Debug. Se a D3D debug layer não estiver instalada
    // (SDK opcional), a criação falhará — fazemos fallback sem debug nesse caso.
    Flags = D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Em máquinas multi-GPU (notebook iGPU + dGPU), o device precisa ser criado
    // no MESMO adapter da UE — shared handles não abrem entre adapters diferentes.
    Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
    if (AdapterLuid)
    {
        Microsoft::WRL::ComPtr<IDXGIFactory1> Factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&Factory))))
        {
            for (UINT i = 0; Factory->EnumAdapters1(i, Adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i)
            {
                DXGI_ADAPTER_DESC1 Desc = {};
                if (SUCCEEDED(Adapter->GetDesc1(&Desc)) &&
                    Desc.AdapterLuid.LowPart  == AdapterLuid->LowPart &&
                    Desc.AdapterLuid.HighPart == AdapterLuid->HighPart)
                {
                    UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Adapter da UE encontrado por LUID: %s"), Desc.Description);
                    break;
                }
                Adapter.Reset();
            }
        }
        if (!Adapter)
        {
            UE_LOG(LogTemp, Warning, TEXT("UltralightGPUDriver: Adapter com LUID da UE nao encontrado — usando adapter padrao (pode falhar em maquinas multi-GPU)."));
        }
    }

    // Com adapter explícito, o driver type deve ser UNKNOWN (regra da API D3D11)
    const D3D_DRIVER_TYPE DriverType = Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    HRESULT hr = D3D11CreateDevice(
        Adapter.Get(),
        DriverType,
        nullptr,
        Flags,
        FeatureLevels, 1,
        D3D11_SDK_VERSION,
        Device.GetAddressOf(),
        &ActualLevel,
        Context.GetAddressOf()
    );

    // Fallback: se debug layer não instalada, tenta sem ela
    if (FAILED(hr) && (Flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        UE_LOG(LogTemp, Warning, TEXT("UltralightGPUDriver: Debug layer indisponível — recriando sem ela."));
        Device.Reset();
        Context.Reset();
        Flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            Adapter.Get(), DriverType, nullptr,
            Flags, FeatureLevels, 1, D3D11_SDK_VERSION,
            Device.GetAddressOf(), &ActualLevel, Context.GetAddressOf()
        );
    }

    if (!CheckHR(hr, TEXT("D3D11CreateDevice"))) return false;

    UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Device D3D11 criado (FL 11_0)."));

    if (!CreateShaders())        return false;
    if (!CreateInputLayouts())   return false;
    if (!CreateStates())         return false;
    if (!CreateConstantBuffer()) return false;

    // Textura fallback 1x1 branca opaca — evita null SRV em draws sem textura
    {
        D3D11_TEXTURE2D_DESC FallbackDesc = {};
        FallbackDesc.Width     = 1;
        FallbackDesc.Height    = 1;
        FallbackDesc.MipLevels = 1;
        FallbackDesc.ArraySize = 1;
        FallbackDesc.Format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        FallbackDesc.SampleDesc.Count = 1;
        FallbackDesc.Usage     = D3D11_USAGE_IMMUTABLE;

        uint8 WhitePixel[4] = { 255, 255, 255, 255 };
        D3D11_SUBRESOURCE_DATA InitData = {};
        InitData.pSysMem          = WhitePixel;
        InitData.SysMemPitch      = 4;
        InitData.SysMemSlicePitch = 4;

        if (SUCCEEDED(Device->CreateTexture2D(&FallbackDesc, &InitData, FallbackTex.GetAddressOf())))
        {
            Device->CreateShaderResourceView(FallbackTex.Get(), nullptr, FallbackSRV.GetAddressOf());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Inicializado com sucesso."));
    return true;
}

// ============================================================================
// Shutdown
// ============================================================================

void FUltralightGPUDriverD3D11::Shutdown()
{
    PendingCommands.Empty();
    Geometries.Empty();
    RenderBuffers.Empty();

    // Fecha handles compartilhados antes de limpar o mapa de texturas
    for (auto& Pair : Textures)
    {
        if (Pair.Value.SharedHandle)
        {
            CloseHandle(Pair.Value.SharedHandle);
            Pair.Value.SharedHandle = nullptr;
        }
    }
    Textures.Empty();

    ConstantBuffer.Reset();
    DepthStencilOff.Reset();
    SamplerLinear.Reset();
    RasterizerScissor.Reset();
    RasterizerNormal.Reset();
    BlendDisabled.Reset();
    BlendEnabled.Reset();
    ILFill.Reset();
    ILPath.Reset();
    PSFill.Reset();
    PSFillPath.Reset();
    VSFill.Reset();
    VSPath.Reset();

    if (Context) { Context->ClearState(); Context->Flush(); }
    Context.Reset();
    Device.Reset();

	UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Shutdown completo."));
}

// ============================================================================
// Reset — limpa recursos de sessão, preserva device D3D11 e shaders
// ============================================================================

void FUltralightGPUDriverD3D11::Reset()
{
	PendingCommands.Empty();

	// Fecha handles compartilhados antes de limpar texturas
	for (auto& Pair : Textures)
	{
		if (Pair.Value.SharedHandle)
		{
			CloseHandle(Pair.Value.SharedHandle);
			Pair.Value.SharedHandle = nullptr;
		}
	}
	Textures.Empty();
	RenderBuffers.Empty();
	Geometries.Empty();

	// Resetar contadores de ID
	TextureIdCounter = 0;
	RenderBufferIdCounter = 0;
	GeometryIdCounter = 0;

	// Resetar estado do RT principal
	bRenderTargetCreated.store(false, std::memory_order_release);
	MainRTTextureId.store(0, std::memory_order_release);
	SharedTextureHandle.store(nullptr, std::memory_order_release);
	RTWidth = 0;
	RTHeight = 0;

	UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Reset de recursos de sessao concluido (Device D3D11 preservado)."));
}

// ============================================================================
// Shaders
// ============================================================================

bool FUltralightGPUDriverD3D11::CreateShaders()
{
    if (!UL_HR(Device->CreateVertexShader(
            v2f_c4f_t2f_fxc, v2f_c4f_t2f_fxc_len,
            nullptr, VSPath.GetAddressOf()))) return false;

    if (!UL_HR(Device->CreateVertexShader(
            v2f_c4f_t2f_t2f_d28f_fxc, v2f_c4f_t2f_t2f_d28f_fxc_len,
            nullptr, VSFill.GetAddressOf()))) return false;

    if (!UL_HR(Device->CreatePixelShader(
            fill_path_fxc, fill_path_fxc_len,
            nullptr, PSFillPath.GetAddressOf()))) return false;

    if (!UL_HR(Device->CreatePixelShader(
            fill_fxc, fill_fxc_len,
            nullptr, PSFill.GetAddressOf()))) return false;

    UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Shaders criados."));
    return true;
}

// ============================================================================
// Input Layouts
//
// Os offsets espelham exatamente os structs do Ultralight SDK:
//   Vertex_2f_4ub_2f             = { pos[2], color[4ub], obj[2] }        → 20 bytes
//   Vertex_2f_4ub_2f_2f_28f     = { pos[2], color[4ub], tex[2], obj[2],
//                                    data0..6[4f each] }                  → 140 bytes
// ============================================================================

bool FUltralightGPUDriverD3D11::CreateInputLayouts()
{
    // --- Path geometry (Vertex_2f_4ub_2f, 20 bytes) ---
    {
        D3D11_INPUT_ELEMENT_DESC Elems[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,  0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UINT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (!UL_HR(Device->CreateInputLayout(
                Elems, 3,
                v2f_c4f_t2f_fxc, v2f_c4f_t2f_fxc_len,
                ILPath.GetAddressOf()))) return false;
    }

    // --- Quad/fill geometry (Vertex_2f_4ub_2f_2f_28f, 140 bytes) ---
    {
        D3D11_INPUT_ELEMENT_DESC Elems[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,   0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UINT,      0,   8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0,  20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  60, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  76, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  92, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 108, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    7, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 124, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (!UL_HR(Device->CreateInputLayout(
                Elems, 11,
                v2f_c4f_t2f_t2f_d28f_fxc, v2f_c4f_t2f_t2f_d28f_fxc_len,
                ILFill.GetAddressOf()))) return false;
    }

    UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Input layouts criados."));
    return true;
}

// ============================================================================
// Render States
// ============================================================================

bool FUltralightGPUDriverD3D11::CreateStates()
{
    // Blend com alpha premultiplicado (formato nativo do Ultralight)
    {
        D3D11_BLEND_DESC D = {};
        D.RenderTarget[0].BlendEnable           = true;
        D.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE; 
        D.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
        D.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        D.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        D.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
        D.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        D.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (!UL_HR(Device->CreateBlendState(&D, BlendEnabled.GetAddressOf()))) return false;
    }

    // Blend desabilitado (overwrite direto, usado nos clears de scissor)
    {
        D3D11_BLEND_DESC D = {};
        D.RenderTarget[0].BlendEnable           = false;
        D.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (!UL_HR(Device->CreateBlendState(&D, BlendDisabled.GetAddressOf()))) return false;
    }

    // Rasterizer sem scissor
    {
        D3D11_RASTERIZER_DESC D = {};
        D.FillMode        = D3D11_FILL_SOLID;
        D.CullMode        = D3D11_CULL_NONE;
        D.DepthClipEnable = false;
        D.ScissorEnable   = false;
        if (!UL_HR(Device->CreateRasterizerState(&D, RasterizerNormal.GetAddressOf()))) return false;
    }

    // Rasterizer com scissor
    {
        D3D11_RASTERIZER_DESC D = {};
        D.FillMode        = D3D11_FILL_SOLID;
        D.CullMode        = D3D11_CULL_NONE;
        D.DepthClipEnable = false;
        D.ScissorEnable   = true;
        if (!UL_HR(Device->CreateRasterizerState(&D, RasterizerScissor.GetAddressOf()))) return false;
    }

    // Sampler linear clamp
    {
        D3D11_SAMPLER_DESC D = {};
        D.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        D.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        D.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        D.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        D.ComparisonFunc = D3D11_COMPARISON_NEVER;
        D.MaxLOD         = D3D11_FLOAT32_MAX;
        if (!UL_HR(Device->CreateSamplerState(&D, SamplerLinear.GetAddressOf()))) return false;
    }

    // Depth-stencil completamente desabilitado (Ultralight não usa depth)
    {
        D3D11_DEPTH_STENCIL_DESC D = {};
        D.DepthEnable   = false;
        D.StencilEnable = false;
        if (!UL_HR(Device->CreateDepthStencilState(&D, DepthStencilOff.GetAddressOf()))) return false;
    }

    UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Render states criados."));
    return true;
}

// ============================================================================
// Constant Buffer
// ============================================================================

bool FUltralightGPUDriverD3D11::CreateConstantBuffer()
{
    // D3D11 exige que o tamanho de cbuffers seja múltiplo de 16 bytes.
    // sizeof(FULUniforms) = 768 bytes (já é múltiplo de 16).
    static_assert(sizeof(FULUniforms) % 16 == 0,
        "FULUniforms deve ser múltiplo de 16 bytes para D3D11 cbuffer");

    D3D11_BUFFER_DESC D = {};
    D.ByteWidth      = sizeof(FULUniforms);
    D.Usage          = D3D11_USAGE_DYNAMIC;
    D.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    D.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (!UL_HR(Device->CreateBuffer(&D, nullptr, ConstantBuffer.GetAddressOf())))
        return false;

    UE_LOG(LogTemp, Log, TEXT("UltralightGPUDriver: Constant buffer criado (%zu bytes)."),
           sizeof(FULUniforms));
    return true;
}

// ============================================================================
// Synchronize — no-op (device exclusivo da UL Thread)
// ============================================================================

void FUltralightGPUDriverD3D11::BeginSynchronize() {}
void FUltralightGPUDriverD3D11::EndSynchronize()   {}

// ============================================================================
// Textures
// ============================================================================

uint32_t FUltralightGPUDriverD3D11::NextTextureId()
{
    return ++TextureIdCounter;
}

void FUltralightGPUDriverD3D11::CreateTexture(uint32_t texture_id,
                                               ultralight::RefPtr<ultralight::Bitmap> bitmap)
{
    if (!bitmap)
    {
        UE_LOG(LogTemp, Error, TEXT("UltralightGPUDriver: CreateTexture chamado com bitmap nulo."));
        return;
    }

    FULTextureEntry Entry = {};
    Entry.bIsRTT  = bitmap->IsEmpty();
    Entry.Width   = bitmap->width();
    Entry.Height  = bitmap->height();

    if (Entry.bIsRTT)
    {
        const bool bIsMainRT = (Entry.Width == TargetWidth && Entry.Height == TargetHeight);

        // RTT criada com SHARED_NTHANDLE + KEYEDMUTEX para zero-copy handoff
        // entre o device do Ultralight e o device da Unreal Engine, apenas se for o RT principal.
        D3D11_TEXTURE2D_DESC Desc = {};
        Desc.Width              = Entry.Width;
        Desc.Height             = Entry.Height;
        Desc.MipLevels          = 1;
        Desc.ArraySize          = 1;
        if (bIsMainRT)
        {
            Desc.Format         = DXGI_FORMAT_B8G8R8A8_TYPELESS; // Permite criar views sRGB
            Desc.MiscFlags      = D3D11_RESOURCE_MISC_SHARED_NTHANDLE
                                | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        }
        else
        {
            Desc.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
            Desc.MiscFlags      = 0;
        }
        Desc.SampleDesc.Count   = 1;
        Desc.Usage              = D3D11_USAGE_DEFAULT;
        Desc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if (!UL_HR(Device->CreateTexture2D(&Desc, nullptr, Entry.Texture.GetAddressOf())))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels = 1;
        if (!UL_HR(Device->CreateShaderResourceView(
                Entry.Texture.Get(), &SRVDesc, Entry.SRV.GetAddressOf())))
            return;

        if (bIsMainRT)
        {
            // KeyedMutex — compartilhado entre UL Thread e Render Thread UE
            if (!UL_HR(Entry.Texture.As(&Entry.KeyedMutex)))
                return;

            // Exporta o NT handle para a UE abrir no seu próprio device
            Microsoft::WRL::ComPtr<IDXGIResource1> Res1;
            if (!UL_HR(Entry.Texture.As(&Res1)))
                return;

            HANDLE Handle = nullptr;
            if (!UL_HR(Res1->CreateSharedHandle(
                    nullptr,
                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                    nullptr,
                    &Handle)))
                return;

            Entry.SharedHandle = Handle;

            RTWidth  = Entry.Width;
            RTHeight = Entry.Height;
            SharedTextureHandle.store(Handle, std::memory_order_release);
            MainRTTextureId.store(texture_id, std::memory_order_release);
            bRenderTargetCreated.store(true, std::memory_order_release);

            UE_LOG(LogTemp, Log,
                TEXT("UltralightGPUDriver: RTT Principal registrada (id=%u, %ux%u, handle=%p)."),
                texture_id, Entry.Width, Entry.Height, Handle);
        }
        else
        {
            UE_LOG(LogTemp, Log,
                TEXT("UltralightGPUDriver: RTT secundaria/sub-elemento criada (id=%u, %ux%u)."),
                texture_id, Entry.Width, Entry.Height);
        }
    }
    else
    {
        // Textura de dados (atlas de fontes, imagens web, etc.)
        // A8_UNORM (glyph atlas) é usado nativamente: o sampling retorna (0,0,0,a),
        // idêntico ao resultado da antiga expansão CPU A8→BGRA8, com 1/4 da memória
        // e sem custo de conversão por upload (mesmo formato do driver D3D11 de
        // referência do Ultralight/AppCore).
        const bool bIsA8 = (bitmap->format() == ultralight::BitmapFormat::A8_UNORM);

        D3D11_TEXTURE2D_DESC Desc = {};
        Desc.Width              = Entry.Width;
        Desc.Height             = Entry.Height;
        Desc.MipLevels          = 1;
        Desc.ArraySize          = 1;
        Desc.Format             = bIsA8 ? DXGI_FORMAT_A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count   = 1;
        Desc.Usage              = D3D11_USAGE_DEFAULT;
        Desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;

        if (!UL_HR(Device->CreateTexture2D(&Desc, nullptr, Entry.Texture.GetAddressOf())))
            return;

        if (!UL_HR(Device->CreateShaderResourceView(
                Entry.Texture.Get(), nullptr, Entry.SRV.GetAddressOf())))
            return;

        UploadBitmapToTexture(Entry.Texture.Get(), bitmap);
    }

    Textures.Add(texture_id, MoveTemp(Entry));
}

void FUltralightGPUDriverD3D11::UpdateTexture(uint32_t texture_id,
                                               ultralight::RefPtr<ultralight::Bitmap> bitmap)
{
    FULTextureEntry* Entry = Textures.Find(texture_id);
    // RTTs não têm dados de bitmap — skip silencioso
    if (!Entry || Entry->bIsRTT) return;

    // Formato da textura (A8 ou BGRA8) já casa com o formato do bitmap — upload direto
    UploadBitmapToTexture(Entry->Texture.Get(), bitmap);
}

void FUltralightGPUDriverD3D11::DestroyTexture(uint32_t texture_id)
{
    // Se o RT principal está sendo destruído (acontece em todo View::Resize), o
    // bookkeeping publicado precisa ser invalidado ANTES do CloseHandle. Sem isto:
    //   - GetKeyedMutex() passava a devolver nullptr (Textures.Find falha) enquanto
    //     IsRenderTargetReady() continuava true → a UL thread pulava o handoff em
    //     SILÊNCIO e a UI congelava sem deixar rastro no log;
    //   - GetSharedTextureHandle() continuava devolvendo um HANDLE já fechado →
    //     OpenSharedResource1 falhava no widget, que nunca mais tentava.
    // Zerando aqui, o SUltralightBrowser::Tick vê SharedHandle == nullptr e limpa
    // suas texturas pelo caminho já existente, reabrindo tudo no RT novo.
    if (MainRTTextureId.load(std::memory_order_acquire) == texture_id)
    {
        bRenderTargetCreated.store(false, std::memory_order_release);
        SharedTextureHandle.store(nullptr, std::memory_order_release);
        MainRTTextureId.store(0, std::memory_order_release);
        RTWidth  = 0;
        RTHeight = 0;

        UE_LOG(LogTemp, Warning,
            TEXT("UltralightGPUDriver: RTT Principal destruida (id=%u) — handoff suspenso ate um RT novo ser registrado."),
            texture_id);
    }

    if (FULTextureEntry* Entry = Textures.Find(texture_id))
    {
        if (Entry->SharedHandle)
        {
            CloseHandle(Entry->SharedHandle);
            Entry->SharedHandle = nullptr;
        }
    }
    Textures.Remove(texture_id);
}

void FUltralightGPUDriverD3D11::UploadBitmapToTexture(ID3D11Texture2D* Tex,
                                                       ultralight::RefPtr<ultralight::Bitmap> Bitmap)
{
    // LockPixels() retorna void* (não const) — sem necessidade de const_cast
    void* Pixels = Bitmap->LockPixels();
    if (!Pixels) return;

    Context->UpdateSubresource(Tex, 0, nullptr, Pixels, Bitmap->row_bytes(), 0);
    Bitmap->UnlockPixels();
}

// ============================================================================
// Render Buffers
// ============================================================================

uint32_t FUltralightGPUDriverD3D11::NextRenderBufferId()
{
    return ++RenderBufferIdCounter;
}

void FUltralightGPUDriverD3D11::CreateRenderBuffer(uint32_t render_buffer_id,
                                                    const ultralight::RenderBuffer& buffer)
{
    FULTextureEntry* TexEntry = Textures.Find(buffer.texture_id);
    if (!TexEntry)
    {
        UE_LOG(LogTemp, Error,
            TEXT("UltralightGPUDriver: CreateRenderBuffer — textura %u não encontrada."),
            buffer.texture_id);
        return;
    }

    FULRenderBufferEntry RBEntry = {};
    RBEntry.TextureId = buffer.texture_id;
    RBEntry.Width     = buffer.width;
    RBEntry.Height    = buffer.height;

    D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
    RTVDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    RTVDesc.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
    RTVDesc.Texture2D.MipSlice = 0;

    if (!UL_HR(Device->CreateRenderTargetView(
            TexEntry->Texture.Get(), &RTVDesc, RBEntry.RTV.GetAddressOf())))
        return;

    UE_LOG(LogTemp, Log,
        TEXT("UltralightGPUDriver: RenderBuffer criado (id=%u, tex=%u, %ux%u)."),
        render_buffer_id, buffer.texture_id, buffer.width, buffer.height);

    RenderBuffers.Add(render_buffer_id, MoveTemp(RBEntry));
}

void FUltralightGPUDriverD3D11::DestroyRenderBuffer(uint32_t render_buffer_id)
{
    RenderBuffers.Remove(render_buffer_id);
}

// ============================================================================
// Geometry
// ============================================================================

uint32_t FUltralightGPUDriverD3D11::NextGeometryId()
{
    return ++GeometryIdCounter;
}

static Microsoft::WRL::ComPtr<ID3D11Buffer> MakeDynamicBuffer(
    ID3D11Device* Dev, UINT BindFlags, const void* Data, uint32 SizeBytes)
{
    if (SizeBytes == 0) return {};

    D3D11_BUFFER_DESC Desc = {};
    Desc.ByteWidth      = SizeBytes;
    Desc.Usage          = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags      = BindFlags;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA Init = {};
    Init.pSysMem = Data;

    Microsoft::WRL::ComPtr<ID3D11Buffer> Buf;
    Dev->CreateBuffer(&Desc, Data ? &Init : nullptr, Buf.GetAddressOf());
    return Buf;
}

void FUltralightGPUDriverD3D11::CreateGeometry(uint32_t geometry_id,
                                                const ultralight::VertexBuffer& vertices,
                                                const ultralight::IndexBuffer& indices)
{
    FULGeometryEntry Entry = {};
    Entry.VertexFormat = vertices.format;
    Entry.VBSizeBytes  = vertices.size;
    Entry.IBSizeBytes  = indices.size;

    Entry.VertexBuffer = MakeDynamicBuffer(
        Device.Get(), D3D11_BIND_VERTEX_BUFFER, vertices.data, vertices.size);
    Entry.IndexBuffer  = MakeDynamicBuffer(
        Device.Get(), D3D11_BIND_INDEX_BUFFER,  indices.data,  indices.size);

    if (!Entry.VertexBuffer || !Entry.IndexBuffer)
    {
        UE_LOG(LogTemp, Error,
            TEXT("UltralightGPUDriver: CreateGeometry %u — falha ao criar buffers."),
            geometry_id);
        return;
    }

    Geometries.Add(geometry_id, MoveTemp(Entry));
}

void FUltralightGPUDriverD3D11::UpdateGeometry(uint32_t geometry_id,
                                                const ultralight::VertexBuffer& vertices,
                                                const ultralight::IndexBuffer& indices)
{
    FULGeometryEntry* Entry = Geometries.Find(geometry_id);
    if (!Entry) return;

    // Se o novo dado cabe nos buffers existentes, usa MAP_WRITE_DISCARD (rápido).
    // Se o tamanho mudou, recria os buffers — evita overflow silencioso.
    auto UpdateBuf = [&](Microsoft::WRL::ComPtr<ID3D11Buffer>& Buf,
                         uint32& StoredSize, UINT BindFlags,
                         const void* Data, uint32 NewSize)
    {
        if (NewSize > StoredSize)
        {
            Buf = MakeDynamicBuffer(Device.Get(), BindFlags, Data, NewSize);
            StoredSize = NewSize;
        }
        else if (Buf)
        {
            D3D11_MAPPED_SUBRESOURCE Mapped = {};
            if (SUCCEEDED(Context->Map(Buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
            {
                FMemory::Memcpy(Mapped.pData, Data, NewSize);
                Context->Unmap(Buf.Get(), 0);
            }
        }
    };

    UpdateBuf(Entry->VertexBuffer, Entry->VBSizeBytes,
              D3D11_BIND_VERTEX_BUFFER, vertices.data, vertices.size);
    UpdateBuf(Entry->IndexBuffer, Entry->IBSizeBytes,
              D3D11_BIND_INDEX_BUFFER,  indices.data,  indices.size);
}

void FUltralightGPUDriverD3D11::DestroyGeometry(uint32_t geometry_id)
{
    Geometries.Remove(geometry_id);
}

// ============================================================================
// Command List
// ============================================================================

void FUltralightGPUDriverD3D11::UpdateCommandList(const ultralight::CommandList& list)
{
    // Deep copy dos comandos — o ponteiro list.commands não persiste além desta chamada
    PendingCommands.Reset(list.size);
    for (uint32_t i = 0; i < list.size; ++i)
    {
        PendingCommands.Add(list.commands[i]);
    }
}

// ============================================================================
// DrawCommandList — executa os comandos D3D11 pendentes
// Deve ser chamado pela UL Thread com o KeyedMutex adquirido (key=0).
// ============================================================================

bool FUltralightGPUDriverD3D11::DrawCommandList()
{
    if (PendingCommands.Num() == 0) return false;

    // Estado fixo para toda a sequência de draw calls
    Context->OMSetDepthStencilState(DepthStencilOff.Get(), 0);
    ID3D11SamplerState* Samplers[] = { SamplerLinear.Get() };
    Context->PSSetSamplers(0, 1, Samplers);

    for (const ultralight::Command& Cmd : PendingCommands)
    {
        switch (Cmd.command_type)
        {
            case ultralight::CommandType::ClearRenderBuffer:
                ExecuteClearRenderBuffer(Cmd);
                break;
            case ultralight::CommandType::DrawGeometry:
                ExecuteDrawGeometry(Cmd);
                break;
        }
    }

    PendingCommands.Reset();
    // Flush necessário antes do ReleaseSync: garante que o trabalho GPU deste device
    // esteja submetido antes do handoff do KeyedMutex para o device da UE
    Context->Flush();
    return true;
}

void FUltralightGPUDriverD3D11::ExecuteClearRenderBuffer(const ultralight::Command& Cmd)
{
    FULRenderBufferEntry* RB = RenderBuffers.Find(Cmd.gpu_state.render_buffer_id);
    if (!RB) return;

    const float Zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Context->ClearRenderTargetView(RB->RTV.Get(), Zero);
}

void FUltralightGPUDriverD3D11::ExecuteDrawGeometry(const ultralight::Command& Cmd)
{
    const ultralight::GPUState& S = Cmd.gpu_state;

    // --- Render target ---
    FULRenderBufferEntry* RB = RenderBuffers.Find(S.render_buffer_id);
    if (!RB) return;

    ID3D11RenderTargetView* RTVs[] = { RB->RTV.Get() };
    Context->OMSetRenderTargets(1, RTVs, nullptr);

    // --- Viewport ---
    D3D11_VIEWPORT VP = {};
    VP.Width    = (float)S.viewport_width;
    VP.Height   = (float)S.viewport_height;
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    Context->RSSetViewports(1, &VP);

    // --- Scissor ---
    if (S.enable_scissor)
    {
        D3D11_RECT Rect = { S.scissor_rect.left, S.scissor_rect.top,
                            S.scissor_rect.right, S.scissor_rect.bottom };
        Context->RSSetScissorRects(1, &Rect);
        Context->RSSetState(RasterizerScissor.Get());
    }
	else
	{
		Context->RSSetState(RasterizerNormal.Get());
	}

	// --- Blend ---
    const float BF[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    Context->OMSetBlendState(
        S.enable_blend ? BlendEnabled.Get() : BlendDisabled.Get(),
        BF, 0xFFFFFFFF);

    // --- Constant buffer (uniforms) ---
    {
        D3D11_MAPPED_SUBRESOURCE M = {};
        if (SUCCEEDED(Context->Map(ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &M)))
        {
            FULUniforms* U = static_cast<FULUniforms*>(M.pData);
            FMemory::Memzero(U, sizeof(FULUniforms));

            U->State[0] = 0.0f;
            U->State[1] = (float)S.viewport_width;
            U->State[2] = (float)S.viewport_height;
            U->State[3] = 0.0f;

            // Projecao ortografica × SDK transform (o SDK pode mandar identidade)
            float Proj[16] = {
                2.0f/S.viewport_width, 0.0f,                  0.0f, -1.0f,
                0.0f,                 -2.0f/S.viewport_height, 0.0f,  1.0f,
                0.0f,                  0.0f,                  1.0f,  0.0f,
                0.0f,                  0.0f,                  0.0f,  1.0f
            };
            const float* SdkTr = S.transform.data;
            float* T = U->Transform;
            for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                T[c*4+r] = Proj[r*4+0]*SdkTr[c*4+0] + Proj[r*4+1]*SdkTr[c*4+1] + Proj[r*4+2]*SdkTr[c*4+2] + Proj[r*4+3]*SdkTr[c*4+3];

            // Scalars (8 floats → 2 float4 no shader)
            FMemory::Memcpy(U->Scalar4, S.uniform_scalar, sizeof(S.uniform_scalar));

            // Vectors (8 × float4)
            for (int i = 0; i < 8; ++i)
            {
                U->Vector[i * 4 + 0] = S.uniform_vector[i].x;
                U->Vector[i * 4 + 1] = S.uniform_vector[i].y;
                U->Vector[i * 4 + 2] = S.uniform_vector[i].z;
                U->Vector[i * 4 + 3] = S.uniform_vector[i].w;
            }

            U->ClipSize = S.clip_size;
            const int ClipCount = FMath::Min((int)S.clip_size, 8);
            for (int i = 0; i < ClipCount; ++i)
            {
                static_assert(sizeof(U->Clip[0]) == sizeof(S.clip[0].data),
                    "Mismatch no tamanho da matrix Clip");
                FMemory::Memcpy(U->Clip[i], S.clip[i].data, sizeof(U->Clip[i]));
            }

            Context->Unmap(ConstantBuffer.Get(), 0);
        }

        ID3D11Buffer* CB[] = { ConstantBuffer.Get() };
        Context->VSSetConstantBuffers(0, 1, CB);
        Context->PSSetConstantBuffers(0, 1, CB);
    }

    // --- Texturas ---
    {
        ID3D11ShaderResourceView* SRVs[3] = {};
        const uint32_t TexIds[3] = { S.texture_1_id, S.texture_2_id, S.texture_3_id };
        for (int i = 0; i < 3; ++i)
        {
            if (TexIds[i] != 0)
            {
                FULTextureEntry* TE = Textures.Find(TexIds[i]);
                if (TE) SRVs[i] = TE->SRV.Get();
            }
        }

        // Fallback: textura branca 1x1 para slots sem textura
        if (!SRVs[0]) SRVs[0] = FallbackSRV.Get();
        if (!SRVs[1]) SRVs[1] = FallbackSRV.Get();
        if (!SRVs[2]) SRVs[2] = FallbackSRV.Get();

        Context->PSSetShaderResources(0, 3, SRVs);
    }

    // --- Geometry + Shaders ---
    FULGeometryEntry* Geo = Geometries.Find(Cmd.geometry_id);
    if (!Geo || !Geo->VertexBuffer || !Geo->IndexBuffer) return;

    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->IASetIndexBuffer(Geo->IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    if (S.shader_type == ultralight::ShaderType::FillPath)
    {
        Context->IASetInputLayout(ILPath.Get());
        Context->VSSetShader(VSPath.Get(), nullptr, 0);
        Context->PSSetShader(PSFillPath.Get(), nullptr, 0);
        UINT Stride = (UINT)sizeof(ultralight::Vertex_2f_4ub_2f);
        UINT Offset = 0;
        ID3D11Buffer* VB[] = { Geo->VertexBuffer.Get() };
        Context->IASetVertexBuffers(0, 1, VB, &Stride, &Offset);
    }
    else // ShaderType::Fill
    {
        Context->IASetInputLayout(ILFill.Get());
        Context->VSSetShader(VSFill.Get(), nullptr, 0);
        Context->PSSetShader(PSFill.Get(), nullptr, 0);
        UINT Stride = (UINT)sizeof(ultralight::Vertex_2f_4ub_2f_2f_28f);
        UINT Offset = 0;
        ID3D11Buffer* VB[] = { Geo->VertexBuffer.Get() };
        Context->IASetVertexBuffers(0, 1, VB, &Stride, &Offset);
    }

    Context->DrawIndexed(Cmd.indices_count, Cmd.indices_offset, 0);
}

// ============================================================================
// GetKeyedMutex
// ============================================================================

IDXGIKeyedMutex* FUltralightGPUDriverD3D11::GetKeyedMutex() const
{
    const uint32 TexId = MainRTTextureId.load(std::memory_order_acquire);
    const FULTextureEntry* Entry = Textures.Find(TexId);
    if (!Entry) return nullptr;
    // ComPtr::Get() em const ComPtr retorna T* (não const T*) — correto
    return Entry->KeyedMutex.Get();
}
