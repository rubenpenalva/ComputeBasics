#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>

#include <iostream>
#include <cassert>

#include "utils.h"
#include "cmdqueuesyncer.h"

#define ENABLE_D3D12_DEBUG_LAYER            ( 1 )
#define ENABLE_D3D12_DEBUG_GPU_VALIDATION   ( 1 )

#if ENABLE_D3D12_DEBUG_LAYER
#include <Initguid.h>
#include <dxgidebug.h>
#endif

// Note actually comptr is not a smart ptr but a raii class using
// IUnknown AddRef and Release functions
using IDXGIAdapter1ComPtr                   = Microsoft::WRL::ComPtr<IDXGIAdapter1>;
using IDXGIFactory6ComPtr                   = Microsoft::WRL::ComPtr<IDXGIFactory6>;
using ID3D12DeviceComPtr                    = Microsoft::WRL::ComPtr<ID3D12Device>;
using ID3D12CommandQueueComPtr              = Microsoft::WRL::ComPtr<ID3D12CommandQueue>;
using ID3D12ResourceComPtr                  = Microsoft::WRL::ComPtr<ID3D12Resource>;
using ID3D12GraphicsCommandListComPtr       = Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>;
using ID3D12CommandAllocatorComPtr          = Microsoft::WRL::ComPtr<ID3D12CommandAllocator>;

struct CommandQueue
{
    ID3D12CommandQueueComPtr    m_cmdQueue;
    uint64_t                    m_timestampFrequency;
};

struct BufferAllocation
{
    ID3D12ResourceComPtr    m_resource;
    uint64_t                m_alignedSize;
};

struct CommandList
{
    ID3D12CommandAllocatorComPtr    m_allocator;
    ID3D12GraphicsCommandListComPtr m_cmdList;
};

IDXGIAdapter1ComPtr CreateDXGIAdapter()
{
    IDXGIFactory6ComPtr factory;
    Utils::AssertIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));
    assert(factory);

    IDXGIAdapter1ComPtr adapter;
    for (unsigned int adapterIndex = 0; ; ++adapterIndex)
    {
        HRESULT result = factory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
        if (result != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC1 desc;
            Utils::AssertIfFailed(adapter->GetDesc1(&desc));
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                std::wcout << "[Hardware init] Using adapter " << desc.Description << std::endl;
                break;
            }
        }
    }
    assert(adapter);

    return adapter;
}

ID3D12DeviceComPtr CreateD3D12Device(IDXGIAdapter1ComPtr adapter)
{
#if ENABLE_D3D12_DEBUG_LAYER
    // Note this needs to be called before creating the d3d12 device
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController0;
    Utils::AssertIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController0)));
    debugController0->EnableDebugLayer();
#if ENABLE_D3D12_DEBUG_GPU_VALIDATION
    Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
    Utils::AssertIfFailed(debugController0->QueryInterface(IID_PPV_ARGS(&debugController1)));
    debugController1->SetEnableGPUBasedValidation(TRUE);
#endif // ENABLE_D3D12_DEBUG_GPU_VALIDATION
#endif // ENABLE_D3D12_DEBUG_LAYER

    ID3D12DeviceComPtr device;
    Utils::AssertIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)));
    assert(device);

    return device;
}

#if ENABLE_D3D12_DEBUG_LAYER
void ReportLiveObjects()
{
    Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
    Utils::AssertIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)));
    debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
}
#endif

CommandQueue CreateCommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, 
                                bool disableTimeout, const std::wstring& name)
{
    assert(device);

    D3D12_COMMAND_QUEUE_DESC queueDesc {};
    queueDesc.Type = type;
    if (disableTimeout)
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;

    CommandQueue cmdQueue;
    Utils::AssertIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue.m_cmdQueue)));
    assert(cmdQueue.m_cmdQueue);

    Utils::AssertIfFailed(cmdQueue.m_cmdQueue->GetTimestampFrequency(&cmdQueue.m_timestampFrequency));
    cmdQueue.m_cmdQueue->SetName(name.c_str());

    return cmdQueue;
}

CommandQueue CreateComputeCmdQueue(ID3D12Device* device)
{
    assert(device);
    return CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, true, L"Compute Queue");
}

CommandQueue CreateCopyCmdQueue(ID3D12Device* device)
{
    assert(device);
    return CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_COPY, true, L"Copy Queue");
}

// Note sticking together the cmdlist and the allocator. Not the most efficient way of doing it
// but good enough for now
CommandList CreateCommandList(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, const std::wstring& name)
{
    assert(device);

    CommandList cmdList;

    Utils::AssertIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&cmdList.m_allocator)));
    assert(cmdList.m_allocator);
    cmdList.m_allocator->SetName((L"Command Allocator : CommandList " + name).c_str());

    Utils::AssertIfFailed(device->CreateCommandList(0, type, cmdList.m_allocator.Get(), nullptr, 
                                                    IID_PPV_ARGS(&cmdList.m_cmdList)));
    assert(cmdList.m_cmdList);
    cmdList.m_cmdList->SetName((L"Command List " + name).c_str());

    return cmdList;
}

CommandList CreateCopyCommandList(ID3D12Device* device, const std::wstring& name)
{
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_COPY;
    return CreateCommandList(device, type, name);
}

CommandList CreateComputeCommandList(ID3D12Device* device, const std::wstring& name)
{
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    return CreateCommandList(device, type, name);
}

ID3D12ResourceComPtr CreateCommitedResource(ID3D12Device* device, D3D12_HEAP_TYPE heapType,
                                            const D3D12_RESOURCE_DESC& resourceDesc, 
                                            D3D12_RESOURCE_STATES initialState,
                                            const std::wstring& name)
{
    assert(device);

    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type                   = heapType;
    heapProperties.CPUPageProperty        = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference   = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask       = 1;
    heapProperties.VisibleNodeMask        = 1;

    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;

    ID3D12ResourceComPtr resource;
    Utils::AssertIfFailed(device->CreateCommittedResource(&heapProperties, heapFlags, &resourceDesc, initialState,
                                                           nullptr, IID_PPV_ARGS(&resource)));
    
    resource->SetName(name.c_str());

    return resource;
}

D3D12_RESOURCE_DESC CreateBufferDesc(uint64_t sizeBytes, bool isUA)
{
    Utils::IsAlignedToPowerof2(sizeBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn903813(v=vs.85).aspx
    // Alignment must be 64KB (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) or 0, which is effectively 64KB.
    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment          = 0;
    resourceDesc.Width              = sizeBytes;
    resourceDesc.Height             = 1;
    resourceDesc.DepthOrArraySize   = 1;
    resourceDesc.MipLevels          = 1;
    resourceDesc.Format             = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc         = { 1, 0 };
    resourceDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags              = isUA? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : 
                                            D3D12_RESOURCE_FLAG_NONE;

    return resourceDesc;
}

BufferAllocation CreateBuffer(ID3D12Device* device, uint64_t sizeBytes, D3D12_HEAP_TYPE heapType,
                              D3D12_RESOURCE_STATES initialState, bool isUA, const std::wstring& name)
{
    size_t bufferAligment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    const auto alignedSize = Utils::AlignToPowerof2(sizeBytes, bufferAligment);
    D3D12_RESOURCE_DESC resourceDesc = CreateBufferDesc(alignedSize, isUA);

    ID3D12ResourceComPtr resource = CreateCommitedResource(device, heapType, resourceDesc, initialState, name);
    return BufferAllocation{ resource, alignedSize };
}

BufferAllocation AllocateUploadBuffer(ID3D12Device* device, uint64_t sizeBytes, const std::wstring& name)
{
    auto heapType = D3D12_HEAP_TYPE_UPLOAD;
    auto initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    return CreateBuffer(device, sizeBytes, heapType, initialState, false, name);
}

BufferAllocation AllocateReadOnlyBuffer(ID3D12Device* device, uint64_t sizeBytes, bool isCopyDstInCopyQueue,
                                        const std::wstring& name)
{
    auto heapType = D3D12_HEAP_TYPE_DEFAULT;
    auto initialState = isCopyDstInCopyQueue?   D3D12_RESOURCE_STATE_COMMON : 
                                                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    return CreateBuffer(device, sizeBytes, heapType, initialState, false, name);
}

BufferAllocation AllocateRWBuffer(ID3D12Device* device, uint64_t sizeBytes, bool isCopyDstInCopyQueue,
                                  const std::wstring& name)
{
    auto heapType = D3D12_HEAP_TYPE_DEFAULT;
    auto initialState = isCopyDstInCopyQueue?   D3D12_RESOURCE_STATE_COMMON : 
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return CreateBuffer(device, sizeBytes, heapType, initialState, true, name);
}

BufferAllocation AllocateReadbackBuffer(ID3D12Device* device, uint64_t sizeBytes, const std::wstring& name)
{
    auto heapType = D3D12_HEAP_TYPE_READBACK;
    auto initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    return CreateBuffer(device, sizeBytes, heapType, initialState, false, name);
}

void MemCpyGPUBuffer(ID3D12Resource* dst, const void* src, size_t sizeBytes)
{
    assert(dst);
    assert(src);
    assert(sizeBytes);

    BYTE* dstByteBuffer = nullptr;
    Utils::AssertIfFailed(dst->Map(0, NULL, reinterpret_cast<void**>(&dstByteBuffer)));
    assert(dstByteBuffer);

    memcpy(dstByteBuffer, src, sizeBytes);

    dst->Unmap(0, NULL);
}

D3D12_RESOURCE_BARRIER CreateTransition(ID3D12Resource* resource, 
                                        D3D12_RESOURCE_STATES before, 
                                        D3D12_RESOURCE_STATES after)
{
    assert(resource);

    D3D12_RESOURCE_BARRIER copyDestToReadDest;
    copyDestToReadDest.Type                     = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyDestToReadDest.Flags                    = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    copyDestToReadDest.Transition.pResource     = resource;
    copyDestToReadDest.Transition.Subresource   = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    copyDestToReadDest.Transition.StateBefore   = before;
    copyDestToReadDest.Transition.StateAfter    = after;

    return copyDestToReadDest;
}

void ExecuteSyncUploadDataToBuffer(ID3D12Device* device, 
                                   ID3D12CommandQueue* copyCmdQueue, 
                                   ID3D12GraphicsCommandList* copyCmdList, 
                                   ID3D12CommandQueue* computeCmdQueue, 
                                   ID3D12GraphicsCommandList* computeCmdList, 
                                   ID3D12Resource* dst, const void* data, uint64_t sizeBytes)
{
    assert(device);
    assert(copyCmdQueue);
    assert(copyCmdList);
    assert(computeCmdQueue);
    assert(computeCmdList);
    assert(dst);
    assert(data && sizeBytes);
    
    assert(copyCmdQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COPY);
    assert(copyCmdList->GetType() == D3D12_COMMAND_LIST_TYPE_COPY);
    assert(computeCmdQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COMPUTE);
    assert(computeCmdList->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE);
    
    // Create temporal buffer in the upload heap
    BufferAllocation src = AllocateUploadBuffer(device, sizeBytes, L"Upload temp buffer");
    MemCpyGPUBuffer(src.m_resource.Get(), data, sizeBytes);

    // Copy from upload heap to final buffer
    const UINT64 dstOffset = 0;
    const UINT64 srcOffset = 0;
    const UINT64 numBytes = sizeBytes;
    copyCmdList->CopyBufferRegion(dst, 0, src.m_resource.Get(), 0, sizeBytes);
    {
        Utils::AssertIfFailed(copyCmdList->Close());
        ID3D12CommandList* cmdLists[] = { copyCmdList };
        copyCmdQueue->ExecuteCommandLists(1, cmdLists);

        // Wait for the cmdlist to finish
        CmdQueueSyncer cmdQueueSyncer(device, copyCmdQueue);
        auto workId = cmdQueueSyncer.SignalWork();
        cmdQueueSyncer.Wait(workId);
    }

    auto transition = CreateTransition(dst, D3D12_RESOURCE_STATE_COMMON,
                                       D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    computeCmdList->ResourceBarrier(1, &transition);
    {
        Utils::AssertIfFailed(computeCmdList->Close());
        ID3D12CommandList* cmdLists[] = { computeCmdList };
        computeCmdQueue->ExecuteCommandLists(1, cmdLists);

        // Wait for the cmdlist to finish
        CmdQueueSyncer cmdQueueSyncer(device, computeCmdQueue);
        auto workId = cmdQueueSyncer.SignalWork();
        cmdQueueSyncer.Wait(workId);
    }
}

int main(int argc, char** argv)
{
    auto dxgiAdapter = CreateDXGIAdapter();
    auto d3d12DevicePtr = CreateD3D12Device(dxgiAdapter);
    auto d3d12Device = d3d12DevicePtr.Get();

    auto computeCmdQueue = CreateComputeCmdQueue(d3d12Device);
    auto computeCmdList = CreateComputeCommandList(d3d12Device, L"Compute");
    auto copyCmdQueue = CreateCopyCmdQueue(d3d12Device);
    auto copyCmdList = CreateCopyCommandList(d3d12Device, L"Copy");

    // Allocates buffers
    const size_t dataElementsCount = 4;
    const float data[dataElementsCount] = { -1.0f, -2.0f, -3.0f, -4.0f };
    const uint64_t dataSizeBytes = dataElementsCount * sizeof(float);
    auto inputBuffer = AllocateReadOnlyBuffer(d3d12Device, dataSizeBytes, true, L"Input");
    ExecuteSyncUploadDataToBuffer(d3d12Device, 
                                  copyCmdQueue.m_cmdQueue.Get(), copyCmdList.m_cmdList.Get(), 
                                  computeCmdQueue.m_cmdQueue.Get(), computeCmdList.m_cmdList.Get(), 
                                  inputBuffer.m_resource.Get(),
                                  data, dataSizeBytes);
    auto outputBuffer = AllocateRWBuffer(d3d12Device, dataSizeBytes, false, L"Output");
    auto readbackBuffer = AllocateReadbackBuffer(d3d12Device, dataSizeBytes, L"ReadBack");
    auto timeStampBuffer = AllocateReadbackBuffer(d3d12Device, dataSizeBytes, L"TimeStamp");

    // Creates descriptors
    // Create a compute shader
    // Setup state
    // Dispatch cs
    // Read readback buffer
    // Read timestamp buffer
    // Output readback and timestamp buffer

#if ENABLE_D3D12_DEBUG_LAYER
    ReportLiveObjects();
#endif

    return 0;
}