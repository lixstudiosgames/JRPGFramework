#pragma once

// ============================================================================
// REGRA: Este header pode ser incluído APENAS em .cpp privados do plugin.
// Não expor para headers públicos — contém tipos D3D11 e do Ultralight SDK.
// Este header gerencia seus próprios guards de plataforma Windows internamente.
// ============================================================================

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START

// D3D11 / DXGI / Ultralight GPUDriver — requerem tipos nativos Windows
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <atomic>

// Ultralight GPUDriver interface
#include <Ultralight/platform/GPUDriver.h>
#ifdef GetBytesPerPixel
#undef GetBytesPerPixel
#endif

THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

// UE types (TMap, TArray, uint32) — incluídos APÓS restauração dos tipos Windows
#include "CoreMinimal.h"

// ============================================================================
// Tipos de suporte
// ============================================================================

struct FULGeometryEntry
{
    Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> IndexBuffer;
    ultralight::VertexBufferFormat       VertexFormat;
    uint32                               VBSizeBytes;  // Tamanho real do VB (bytes)
    uint32                               IBSizeBytes;  // Tamanho real do IB (bytes)
};

struct FULTextureEntry
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          Texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SRV;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex>          KeyedMutex; // válido apenas em RTTs shared
    bool                                             bIsRTT;
    uint32                                           Width;
    uint32                                           Height;
    HANDLE                                           SharedHandle = nullptr;
};

struct FULRenderBufferEntry
{
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> RTV;
    uint32                                         TextureId;
    uint32                                         Width;
    uint32                                         Height;
};

// Layout exato do cbuffer b0 que os shaders HLSL do Ultralight esperam.
// O padding entre ClipSize e Clip é necessário pois HLSL alinha matrizes a 16 bytes.
// Tamanho total: 16 + 64 + 32 + 128 + 16(ClipSize+pad) + 512 = 768 bytes.
#pragma pack(push, 1)
struct FULUniforms
{
    float    State[4];       // [time, screenW, screenH, 0]
    float    Transform[16];  // matrix 4x4 (row-major, conforme emitido pelo Ultralight)
    float    Scalar4[8];     // uniform_scalar[8] (dois float4)
    float    Vector[32];     // uniform_vector[8] (oito float4)
    uint32_t ClipSize;
    uint32_t _pad[3];        // padding HLSL: uint + 12 bytes = 16 bytes antes das matrizes
    float    Clip[8][16];    // clip[8] matrizes 4x4
};
#pragma pack(pop)

// ============================================================================
// FUltralightGPUDriverD3D11
//
// Implementa ultralight::GPUDriver usando um ID3D11Device próprio (criado
// com D3D11CreateDevice, completamente independente do device da Unreal).
//
// THREAD SAFETY:
//   - Funções virtuais (BeginSynchronize...UpdateCommandList) → UL Thread apenas.
//   - DrawCommandList() → UL Thread apenas (após Renderer::Render()).
//   - IsRenderTargetReady() / GetSharedTextureHandle() → leitura atômica, safe
//     para qualquer thread APÓS IsRenderTargetReady() retornar true.
//   - GetKeyedMutex() → deve ser chamado com IsRenderTargetReady() == true.
//     O IDXGIKeyedMutex é thread-safe para uso concurrent por design DXGI.
// ============================================================================
class FUltralightGPUDriverD3D11 : public ultralight::GPUDriver
{
public:
    FUltralightGPUDriverD3D11();
    virtual ~FUltralightGPUDriverD3D11();

    // ---------------------------------------------------------------
    // Ciclo de vida — chamar na UL Thread
    // ---------------------------------------------------------------
    // AdapterLuid (opcional): LUID do adapter usado pelo device da UE.
    // Necessário em máquinas com múltiplas GPUs (iGPU + dGPU) — texturas
    // compartilhadas via NT handle só funcionam entre devices do MESMO adapter.
    bool Initialize(const LUID* AdapterLuid = nullptr);
    void Shutdown();

    // ---------------------------------------------------------------
    // ultralight::GPUDriver interface
    // ---------------------------------------------------------------
    virtual void BeginSynchronize() override;
    virtual void EndSynchronize()   override;

    virtual uint32_t NextTextureId() override;
    virtual void CreateTexture(uint32_t texture_id,
                               ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
    virtual void UpdateTexture(uint32_t texture_id,
                               ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
    virtual void DestroyTexture(uint32_t texture_id) override;

    virtual uint32_t NextRenderBufferId() override;
    virtual void CreateRenderBuffer(uint32_t render_buffer_id,
                                    const ultralight::RenderBuffer& buffer) override;
    virtual void DestroyRenderBuffer(uint32_t render_buffer_id) override;

    virtual uint32_t NextGeometryId() override;
    virtual void CreateGeometry(uint32_t geometry_id,
                                const ultralight::VertexBuffer& vertices,
                                const ultralight::IndexBuffer& indices) override;
    virtual void UpdateGeometry(uint32_t geometry_id,
                                const ultralight::VertexBuffer& vertices,
                                const ultralight::IndexBuffer& indices) override;
    virtual void DestroyGeometry(uint32_t geometry_id) override;

    virtual void UpdateCommandList(const ultralight::CommandList& list) override;

    // ---------------------------------------------------------------
    // API pública — UL Thread (chamar após Renderer::Render())
    // ---------------------------------------------------------------

    // Executa todos os comandos D3D11 pendentes.
    // Chamar com o KeyedMutex já adquirido (key=0).
    // Retorna true se algum comando foi de fato executado (frame novo produzido).
    bool DrawCommandList();

    // True se há comandos pendentes para DrawCommandList (UL Thread apenas).
    // Usado para NÃO adquirir o KeyedMutex quando não há nada a desenhar —
    // adquirir/liberar sem desenhar deixa o mutex preso na key 1 (só a UE pode
    // retomá-la, e ela só o faz quando há frame novo), travando o próximo paint.
    bool HasPendingCommands() const { return PendingCommands.Num() > 0; }

    // ---------------------------------------------------------------
    // API pública — leitura atômica (qualquer thread)
    // ---------------------------------------------------------------

    bool     IsRenderTargetReady()       const { return bRenderTargetCreated.load(std::memory_order_acquire); }
    uint32_t GetMainRTTextureId()        const { return MainRTTextureId.load(std::memory_order_acquire); }
    HANDLE   GetSharedTextureHandle()    const { return SharedTextureHandle.load(std::memory_order_acquire); }
    uint32   GetRTWidth()                const { return RTWidth; }
    uint32   GetRTHeight()               const { return RTHeight; }

    // Retorna o IDXGIKeyedMutex do render target principal.
    // Válido apenas quando IsRenderTargetReady() == true.
    // O ponteiro permanece vivo enquanto este driver existir.
    IDXGIKeyedMutex* GetKeyedMutex() const;

    void SetMainRTSize(uint32 Width, uint32 Height) { TargetWidth = Width; TargetHeight = Height; }

    // Limpa recursos de sessao (textures, buffers, geometrias) sem destruir o device D3D11.
    // Usado entre sessoes PIE para reutilizar o mesmo device e shaders.
    void Reset();

private:
    // ---------------------------------------------------------------
    // Setup helpers (UL Thread)
    // ---------------------------------------------------------------
    bool CreateShaders();
    bool CreateInputLayouts();
    bool CreateStates();
    bool CreateConstantBuffer();

    void ExecuteClearRenderBuffer(const ultralight::Command& Cmd);
    void ExecuteDrawGeometry(const ultralight::Command& Cmd);
    void UploadBitmapToTexture(ID3D11Texture2D* Tex,
                               ultralight::RefPtr<ultralight::Bitmap> Bitmap);

    // ---------------------------------------------------------------
    // D3D11 resources (owned, UL Thread only)
    // ---------------------------------------------------------------
    Microsoft::WRL::ComPtr<ID3D11Device>        Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;

    Microsoft::WRL::ComPtr<ID3D11VertexShader>  VSPath;      // v2f_c4f_t2f
    Microsoft::WRL::ComPtr<ID3D11VertexShader>  VSFill;      // v2f_c4f_t2f_t2f_d28f
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   PSFillPath;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>   PSFill;

    Microsoft::WRL::ComPtr<ID3D11InputLayout>   ILPath;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>   ILFill;

    Microsoft::WRL::ComPtr<ID3D11BlendState>        BlendEnabled;
    Microsoft::WRL::ComPtr<ID3D11BlendState>        BlendDisabled;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   RasterizerNormal;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   RasterizerScissor;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>      SamplerLinear;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DepthStencilOff;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> FallbackSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          FallbackTex;

    Microsoft::WRL::ComPtr<ID3D11Buffer> ConstantBuffer;

    // ---------------------------------------------------------------
    // Resource maps (UL Thread only)
    // ---------------------------------------------------------------
    TMap<uint32, FULTextureEntry>      Textures;
    TMap<uint32, FULRenderBufferEntry> RenderBuffers;
    TMap<uint32, FULGeometryEntry>     Geometries;

    TArray<ultralight::Command> PendingCommands;

    // ---------------------------------------------------------------
    // ID counters
    // ---------------------------------------------------------------
    uint32 TextureIdCounter      = 0;
    uint32 RenderBufferIdCounter = 0;
    uint32 GeometryIdCounter     = 0;

    // ---------------------------------------------------------------
    // Shared render target — escrito pela UL Thread, lido de qualquer thread
    // ---------------------------------------------------------------
    std::atomic<bool>     bRenderTargetCreated { false };
    std::atomic<uint32_t> MainRTTextureId      { 0 };
    std::atomic<HANDLE>   SharedTextureHandle  { nullptr };

    // Dimensões do RT — escritas uma vez, depois read-only
    uint32 RTWidth  = 0;
    uint32 RTHeight = 0;

    uint32 TargetWidth  = 1920;
    uint32 TargetHeight = 1080;
};
