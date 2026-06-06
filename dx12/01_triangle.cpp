#include <core/DXBaseApp.h>
#include <d3dcompiler.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

// =============================================================================
// HLSL 셰이더 (런타임 D3DCompile, vs_5_0 / ps_5_0)
// =============================================================================
// 실제 프로젝트에선 DXC로 사전 컴파일된 DXIL을 쓰는 게 일반적.
// POSITION/COLOR semantic 이름은 InputLayout의 SemanticName과 매칭되어야 한다.
static const char* vsHlsl = R"(
struct VSIn  { float3 pos : POSITION; float3 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float3 col : COLOR; };
VSOut main(VSIn v) {
    VSOut o;
    o.pos = float4(v.pos, 1.0);
    o.col = v.col;
    return o;
}
)";

static const char* psHlsl = R"(
struct PSIn { float4 pos : SV_POSITION; float3 col : COLOR; };
float4 main(PSIn p) : SV_TARGET {
    return float4(p.col, 1.0);
}
)";

static Microsoft::WRL::ComPtr<ID3DBlob> compileShader(const char* src, const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                            "main", target, D3DCOMPILE_DEBUG, 0, &blob, &err);
    if (FAILED(hr)) {
        const char* message = err ? static_cast<char*>(err->GetBufferPointer())
                                  : "no compiler error message";
        spdlog::error("HLSL compile failed for {}: {}", target, message);
        throw std::runtime_error(std::string("HLSL compile: ") + message);
    }
    spdlog::info("HLSL shader compiled: target={}, bytes={}", target, blob->GetBufferSize());
    return blob;
}

// 더블 버퍼링: 백버퍼 2개를 번갈아 그린다. (트리플은 3)
static const UINT FRAME_COUNT = 2;

class TriangleApp : public DXBaseApp {
public:
    TriangleApp() { m_title = "dx12-triangle"; }

private:
    Microsoft::WRL::ComPtr<IDXGIFactory4>           m_factory;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_renderTargets[FRAME_COUNT];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>  m_cmdAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_pso;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW                        m_vbView{};
    Microsoft::WRL::ComPtr<ID3D12Fence>             m_fence;
    UINT64                                          m_fenceValue = 0;
    HANDLE                                          m_fenceEvent = nullptr;
    UINT                                            m_rtvSize = 0;   // RTV descriptor 크기 (GPU별로 다름)
    UINT                                            m_frameIdx = 0;  // 현재 백버퍼 인덱스
    UINT                                            m_loggedFrames = 0;

    void OnInit() override {
        CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory));
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
        spdlog::info("DX12 device created: feature level >= 11_0");

        D3D12_COMMAND_QUEUE_DESC qDesc = {};
        qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_cmdQueue));
        spdlog::info("DX12 direct command queue created");

        // ─── Swap Chain ──────────────────────────────────────────────────────
        // FLIP_DISCARD: 모던 DXGI 권장. 이전 BLT 모델보다 효율적.
        // SampleDesc{1,0}: MSAA off. MSAA를 쓰려면 백버퍼는 1샘플로 두고
        // 별도 multisampled RT를 만들어 resolve 하는 패턴이 일반적이다.
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.BufferCount = FRAME_COUNT;
        scDesc.Width       = m_width;
        scDesc.Height      = m_height;
        scDesc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc  = {1, 0};
        Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
        m_factory->CreateSwapChainForHwnd(m_cmdQueue.Get(), m_hwnd, &scDesc, nullptr, nullptr, &sc1);
        sc1.As(&m_swapChain);
        m_frameIdx = m_swapChain->GetCurrentBackBufferIndex();
        spdlog::info("DX12 swapchain created: buffers={}, size={}x{}, format=R8G8B8A8_UNORM",
                     FRAME_COUNT, m_width, m_height);

        // ─── RTV Descriptor Heap ─────────────────────────────────────────────
        // RTV는 RENDER_TARGET 전용 descriptor. CBV/SRV/UAV용 heap과 분리됨.
        // GetDescriptorHandleIncrementSize: descriptor 1개 크기(바이트). GPU별로 다르므로
        // 하드코딩 금지.
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = FRAME_COUNT;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap));
        m_rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        spdlog::info("DX12 RTV heap created: descriptors={}, descriptorSize={}",
                     FRAME_COUNT, m_rtvSize);

        // 각 백버퍼에 대해 RTV 생성
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < FRAME_COUNT; i++) {
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += m_rtvSize;
        }

        // Command Allocator: command list의 backing 메모리. 큐가 GPU에서 다 쓰기 전엔 Reset 금지.
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAlloc));

        // ─── Root Signature ──────────────────────────────────────────────────
        // 셰이더가 사용할 리소스 바인딩 레이아웃 정의.
        // 여기선 상수/텍스처 없음 → 비어 있는 root signature.
        // ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT 플래그 필요 (Input layout을 쓰므로).
        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> sig, err;
        D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                     IID_PPV_ARGS(&m_rootSig));

        // Shaders
        auto vs = compileShader(vsHlsl, "vs_5_0");
        auto ps = compileShader(psHlsl, "ps_5_0");

        // ─── Input Layout ────────────────────────────────────────────────────
        // SemanticName/Index/Format/InputSlot/AlignedByteOffset/Class/InstanceStep.
        // POSITION: float3, offset 0 / COLOR: float3, offset 12.
        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        // Rasterizer: solid fill, back-face cull, CW front, depth clip on.
        // 주의: D3D는 기본 front-face가 시계방향(CW). Vulkan은 CCW가 기본이라 좌표계 차이 유의.
        D3D12_RASTERIZER_DESC rast = {};
        rast.FillMode              = D3D12_FILL_MODE_SOLID;
        rast.CullMode              = D3D12_CULL_MODE_BACK;
        rast.FrontCounterClockwise = FALSE;
        rast.DepthClipEnable       = TRUE;
        rast.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        // Blend off (opaque), write all 4 channels.
        D3D12_BLEND_DESC blend = {};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // ─── PSO (Pipeline State Object) ─────────────────────────────────────
        // DX12에선 셰이더/라스터라이저/블렌드/RT format 등을 PSO 하나에 묶는다.
        // 한 번 만들면 immutable. 상태 조합마다 PSO 하나가 필요해 캐싱 전략이 중요.
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout           = {inputLayout, 2};
        psoDesc.pRootSignature        = m_rootSig.Get();
        psoDesc.VS                    = {vs->GetBufferPointer(), vs->GetBufferSize()};
        psoDesc.PS                    = {ps->GetBufferPointer(), ps->GetBufferSize()};
        psoDesc.RasterizerState       = rast;
        psoDesc.BlendState            = blend;
        psoDesc.DepthStencilState     = {};  // depth/stencil 비활성
        psoDesc.SampleMask            = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;  // 스왑체인 포맷과 동일
        psoDesc.SampleDesc            = {1, 0};
        m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
        spdlog::info("DX12 graphics PSO created");

        // Command List: 생성 직후 recording 상태 → 바로 Close해서 onRender에서 Reset부터 시작.
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_cmdAlloc.Get(), m_pso.Get(), IID_PPV_ARGS(&m_cmdList));
        m_cmdList->Close();

        // ─── Vertex Buffer (UPLOAD heap) ─────────────────────────────────────
        // UPLOAD heap = CPU writable + GPU readable. 작은/일회성 데이터에 적합.
        // 대용량/정적 메시는 UPLOAD → DEFAULT heap으로 복사하는 패턴이 표준이다.
        struct Vertex { float pos[3]; float col[3]; };
        Vertex verts[] = {
            {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},  // top   - red
            {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // right - green
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},  // left  - blue
        };
        UINT vbSize = sizeof(verts);

        D3D12_HEAP_PROPERTIES uploadHeap = {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width            = vbSize;
        bufDesc.Height           = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels        = 1;
        bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc       = {1, 0};
        bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        // UPLOAD heap의 초기 상태는 반드시 GENERIC_READ.
        m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                         D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(&m_vertexBuffer));
        void* mapped;
        m_vertexBuffer->Map(0, nullptr, &mapped);
        memcpy(mapped, verts, vbSize);
        m_vertexBuffer->Unmap(0, nullptr);

        // VBV: GPU virtual address + 전체 크기 + stride.
        m_vbView = {m_vertexBuffer->GetGPUVirtualAddress(), vbSize, sizeof(Vertex)};
        spdlog::info("DX12 vertex buffer uploaded: vertices=3, bytes={}", vbSize);

        // ─── Fence (CPU ↔ GPU 동기화) ────────────────────────────────────────
        // GPU는 비동기. 백버퍼를 다시 쓰기 전에 GPU가 끝났는지 fence로 확인해야 한다.
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        spdlog::info("DX12 triangle initialized: frameIndex={}", m_frameIdx);
    }

    void OnRender() override {
        // ─── 1) Command List 기록 시작 ───────────────────────────────────────
        // Allocator는 GPU가 다 쓴 뒤에만 Reset. 여기선 매 프레임 wait해서 단순화.
        m_cmdAlloc->Reset();
        m_cmdList->Reset(m_cmdAlloc.Get(), m_pso.Get());

        m_cmdList->SetGraphicsRootSignature(m_rootSig.Get());

        // Viewport/Scissor: PSO에 포함되지 않는 dynamic 상태.
        D3D12_VIEWPORT vp = {0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f};
        D3D12_RECT     scissor = {0, 0, (LONG)m_width, (LONG)m_height};
        m_cmdList->RSSetViewports(1, &vp);
        m_cmdList->RSSetScissorRects(1, &scissor);

        // ─── 2) Resource Barrier: PRESENT → RENDER_TARGET ────────────────────
        // DX12는 명시적 state transition 필수. 빠뜨리면 디버그 레이어가 경고.
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = m_renderTargets[m_frameIdx].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_cmdList->ResourceBarrier(1, &barrier);

        // 현재 백버퍼의 RTV handle 계산
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (SIZE_T)m_frameIdx * m_rtvSize;

        // ─── 3) Draw ─────────────────────────────────────────────────────────
        m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        float clear[] = {0.0f, 127.0f / 255.0f, 1.0f, 1.0f};
        m_cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);
        m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
        m_cmdList->DrawInstanced(3, 1, 0, 0);  // vertexCount=3, instanceCount=1

        // ─── 4) Barrier: RENDER_TARGET → PRESENT ─────────────────────────────
        // Present 전에 반드시 PRESENT 상태로 되돌려야 한다.
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        m_cmdList->ResourceBarrier(1, &barrier);
        m_cmdList->Close();

        // ─── 5) 제출 & Present ───────────────────────────────────────────────
        ID3D12CommandList* lists[] = {m_cmdList.Get()};
        m_cmdQueue->ExecuteCommandLists(1, lists);
        m_swapChain->Present(1, 0);  // SyncInterval=1 → vsync
        if (m_loggedFrames < 8) {
            spdlog::info("DX12 frame {} presented backbuffer {}", m_loggedFrames, m_frameIdx);
            ++m_loggedFrames;
        }

        // ─── 6) GPU 대기 (단순 single-frame sync) ────────────────────────────
        // 실전에선 백버퍼마다 fence value를 관리해 frame N-2 정도만 기다리는 게 효율적.
        // 여기선 학습용으로 매 프레임 풀 동기화.
        m_cmdQueue->Signal(m_fence.Get(), ++m_fenceValue);
        if (m_fence->GetCompletedValue() < m_fenceValue) {
            m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        m_frameIdx = m_swapChain->GetCurrentBackBufferIndex();
    }

    void OnCleanup() override {
        // GPU가 모든 작업을 끝낼 때까지 대기 (in-flight resource 해제 안전성 확보)
        m_cmdQueue->Signal(m_fence.Get(), ++m_fenceValue);
        if (m_fence->GetCompletedValue() < m_fenceValue) {
            m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        CloseHandle(m_fenceEvent);
    }
};

int main() {
    TriangleApp app;
    app.Run();
}
