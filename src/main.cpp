#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <iostream>
#include <cassert>
#include <algorithm>

#include "utils.h"
#include "cmdqueuesyncer.h"

#define ENABLE_D3D12_DEBUG_LAYER            ( 1 )
#define ENABLE_D3D12_DEBUG_GPU_VALIDATION   ( 1 )
#define ENABLE_PIX_CAPTURE                  ( 1 )
#define ENABLE_RGA_COMPATIBILITY            ( 1 )

#if ENABLE_D3D12_DEBUG_LAYER
#include <Initguid.h>
#include <dxgidebug.h>
#endif

#if ENABLE_PIX_CAPTURE
#include <DXProgrammableCapture.h>
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
using ID3D12DescriptorHeapComPtr            = Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>;
using ID3D12PipelineStateComPtr             = Microsoft::WRL::ComPtr<ID3D12PipelineState>;
using ID3DBlobComPtr                        = Microsoft::WRL::ComPtr<ID3DBlob>;
using ID3D12RootSignatureComPtr             = Microsoft::WRL::ComPtr<ID3D12RootSignature>;
using IDXGraphicsAnalysisComPtr             = Microsoft::WRL::ComPtr<IDXGraphicsAnalysis>;
using ID3D12QueryHeapComPtr                 = Microsoft::WRL::ComPtr<ID3D12QueryHeap>;

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

// Note this is fixed to D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
struct DescriptorHeap
{
    ID3D12DescriptorHeapComPtr  m_d3d12DescriptorHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE m_currentGpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentCpuHandle;

    uint32_t                    m_descriptorHandleIncrementSize;
};

struct PipelineState
{
    ID3D12RootSignatureComPtr m_rootSignature;
    ID3D12PipelineStateComPtr m_pso;
};

struct ResourceTransitionData
{
    ID3D12Resource*         m_resource;
    D3D12_RESOURCE_STATES   m_after;
};

struct ConstantData
{
    float m_float;
};

const char* g_rootSignatureTarget = "rootsig_1_1";
const char* g_rootSignatureName = "SimpleRootSig";
const char* g_outputTag = "[ComputeBasics]";
const char* g_computeShaderMain = "main";
#ifdef ENABLE_RGA_COMPATIBILITY
const char* g_computeShaderTarget = "cs_5_0";
#else
const char* g_computeShaderTarget = "cs_5_1";
#endif
const UINT g_compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
const D3D12_RESOURCE_STATES g_cbState       = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
const D3D12_RESOURCE_STATES g_bufferState   = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
const D3D12_RESOURCE_STATES g_uaState       = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
const D3D12_RESOURCE_STATES g_cpyDstState   = D3D12_RESOURCE_STATE_COMMON;

#if ENABLE_PIX_CAPTURE
class PixCapture
{
public:
    PixCapture()
    {
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_graphicsAnalysis))))
        {
            m_graphicsAnalysis->BeginCapture();
        }
        else
        {
            std::wcout << g_outputTag << "[PIX] A pix capture will not be triggered. Pix is not attached.\n";
        }
    }
    ~PixCapture()
    {
        if (m_graphicsAnalysis)
            m_graphicsAnalysis->EndCapture();
    }
    PixCapture(const PixCapture&) = delete;
    PixCapture(PixCapture&&) = delete;
    PixCapture& operator=(const PixCapture&) = delete;
    PixCapture& operator=(PixCapture&&) = delete;
    
private:
    IDXGraphicsAnalysisComPtr m_graphicsAnalysis;
};
#endif

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
                std::wcout << g_outputTag << "[Hardware init] Using adapter " << desc.Description << std::endl;
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
    const auto heapType = D3D12_HEAP_TYPE_UPLOAD;
    const auto initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    const auto isUA = false;
    return CreateBuffer(device, sizeBytes, heapType, initialState, isUA, name);
}

BufferAllocation AllocateReadOnlyBuffer(ID3D12Device* device, uint64_t sizeBytes, 
                                        D3D12_RESOURCE_STATES initialState,
                                        const std::wstring& name)
{
    const auto heapType = D3D12_HEAP_TYPE_DEFAULT;
    const bool isUA = false;
    return CreateBuffer(device, sizeBytes, heapType, initialState, isUA, name);
}

BufferAllocation AllocateRWBuffer(ID3D12Device* device, uint64_t sizeBytes, 
                                  D3D12_RESOURCE_STATES initialState,
                                  const std::wstring& name)
{
    const auto heapType = D3D12_HEAP_TYPE_DEFAULT;
    const bool isUA = true;
    return CreateBuffer(device, sizeBytes, heapType, initialState, isUA, name);
}

BufferAllocation AllocateReadbackBuffer(ID3D12Device* device, uint64_t sizeBytes, 
                                        const std::wstring& name)
{
    const auto heapType = D3D12_HEAP_TYPE_READBACK;
    const auto initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    const bool isUA = false;
    return CreateBuffer(device, sizeBytes, heapType, initialState, isUA, name);
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

// TODO not quite happy with returning the temp here. Good enough for now.
// Note returning the tmp so the object outlives the execution in the gpu
BufferAllocation EnqueueUploadDataToBuffer(ID3D12Device* device,
                                           ID3D12GraphicsCommandList* copyCmdList,
                                           ID3D12Resource* dst, const void* data, uint64_t sizeBytes)
{
    assert(device);
    assert(copyCmdList);
    assert(dst);
    assert(data && sizeBytes);

    assert(copyCmdList->GetType() == D3D12_COMMAND_LIST_TYPE_COPY);

    // Create temporal buffer in the upload heap
    BufferAllocation src = AllocateUploadBuffer(device, sizeBytes, L"Upload temp buffer");
    MemCpyGPUBuffer(src.m_resource.Get(), data, sizeBytes);

    // Copy from upload heap to final buffer
    copyCmdList->CopyResource(dst, src.m_resource.Get());

    return src;
}

void EnqueueCopyBuffer(ID3D12Device* device,
                       ID3D12GraphicsCommandList* copyCmdList, 
                       ID3D12Resource* dst, ID3D12Resource* src)
{
    assert(device);
    assert(copyCmdList);
    assert(copyCmdList->GetType() == D3D12_COMMAND_LIST_TYPE_COPY);
    assert(dst);
    assert(src);

    copyCmdList->CopyResource(dst, src);
}

// Note batching up the transitions to improve performance
void TransitionCopyDstResources(const std::vector<ResourceTransitionData>& dsts,
                                ID3D12GraphicsCommandList* computeCmdList)
{
    assert(!dsts.empty());
    assert(computeCmdList);
    assert(computeCmdList->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE);

    std::vector<D3D12_RESOURCE_BARRIER> transitions;

    for (auto& dst : dsts)
    {
        auto transition = CreateTransition(dst.m_resource, 
                                           D3D12_RESOURCE_STATE_COMMON, 
                                           dst.m_after);
        transitions.push_back(transition);
    }

    computeCmdList->ResourceBarrier(static_cast<UINT>(transitions.size()), &transitions[0]);
}

void ExecuteCmdList(ID3D12Device* device, ID3D12CommandQueue* cmdQueue,
                    ID3D12GraphicsCommandList* cmdList)
{
    assert(device);
    assert(cmdQueue);
    assert(cmdList);
 
    Utils::AssertIfFailed(cmdList->Close());
    ID3D12CommandList* cmdLists[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, cmdLists);

    // Wait for the cmdlist to finish
    CmdQueueSyncer cmdQueueSyncer(device, cmdQueue);
    auto workId = cmdQueueSyncer.SignalWork();
    cmdQueueSyncer.Wait(workId);
}

// TODO move these to a class
DescriptorHeap CreateDescriptorHeap(ID3D12Device* device, uint32_t descriptorsCount)
{
    assert(device);
    assert(descriptorsCount > 0);

    DescriptorHeap descriptorHeap;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = descriptorsCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 0;

    Utils::AssertIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap.m_d3d12DescriptorHeap)));

    descriptorHeap.m_currentGpuHandle = descriptorHeap.m_d3d12DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    descriptorHeap.m_currentCpuHandle = descriptorHeap.m_d3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    
    uint32_t incrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    descriptorHeap.m_descriptorHandleIncrementSize = incrementSize;

    return descriptorHeap;
}

void AddDescriptor(DescriptorHeap& descriptorHeap)
{
    descriptorHeap.m_currentCpuHandle.ptr += descriptorHeap.m_descriptorHandleIncrementSize;
    descriptorHeap.m_currentGpuHandle.ptr += descriptorHeap.m_descriptorHandleIncrementSize;
}

void CreateBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst, 
                            ID3D12Resource* resource, DXGI_FORMAT format, 
                            uint32_t elementsCount)
{
    assert(device);
    assert(resource);
    assert(format != DXGI_FORMAT_UNKNOWN);
    assert(elementsCount > 0);

    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elementsCount;
    desc.Buffer.StructureByteStride = 0;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(resource, &desc, dst);
}

void CreateRWBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst,
                              ID3D12Resource* resource, DXGI_FORMAT format,
                              uint32_t elementsCount)
{
    assert(device);
    assert(resource);
    assert(format != DXGI_FORMAT_UNKNOWN);
    assert(elementsCount > 0);

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elementsCount;
    desc.Buffer.StructureByteStride = 0;
    desc.Buffer.CounterOffsetInBytes = 0;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    device->CreateUnorderedAccessView(resource, nullptr, &desc, dst);
}

void CreateStructuredBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst, 
                                      ID3D12Resource* resource, DXGI_FORMAT format,
                                      uint32_t elementsCount, uint32_t structureByteStride)
{
    assert(device);
    assert(resource);
    assert(format != DXGI_FORMAT_UNKNOWN);
    assert(elementsCount > 0);
    assert(structureByteStride > 0);

    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elementsCount;
    desc.Buffer.StructureByteStride = structureByteStride;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(resource, &desc, dst);
}

void CreateRWStructuredBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst,
                                        ID3D12Resource* resource, DXGI_FORMAT format,
                                        uint32_t elementsCount, uint32_t structureByteStride)
{
    assert(device);
    assert(resource);
    assert(format != DXGI_FORMAT_UNKNOWN);
    assert(elementsCount > 0);
    assert(structureByteStride > 0);

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elementsCount;
    desc.Buffer.StructureByteStride = structureByteStride;
    desc.Buffer.CounterOffsetInBytes = 0;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    device->CreateUnorderedAccessView(resource, nullptr, &desc, dst);
}

void CreateByteBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst,
                                ID3D12Resource* resource, DXGI_FORMAT format,
                                uint32_t elementsCount)
{
    assert(device);
    assert(resource);
    assert(format != DXGI_FORMAT_UNKNOWN);
    assert(elementsCount > 0);

    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elementsCount;
    desc.Buffer.StructureByteStride = 0;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

    device->CreateShaderResourceView(resource, &desc, dst);
}

void CreateRWByteBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst,
                                  ID3D12Resource* resource, DXGI_FORMAT format,
                                  uint32_t elementsCount)
{
    assert(device);
    assert(resource);
    assert(format != DXGI_FORMAT_UNKNOWN);
    assert(elementsCount > 0);

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = elementsCount;
    desc.Buffer.StructureByteStride = 0;
    desc.Buffer.CounterOffsetInBytes = 0;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    device->CreateUnorderedAccessView(resource, nullptr, &desc, dst);
}

void CreateConstantBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst,
                                    D3D12_GPU_VIRTUAL_ADDRESS bufferLocation, uint32_t sizeBytes)
{
    assert(device);

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
    desc.BufferLocation = bufferLocation;
    desc.SizeInBytes = sizeBytes;

    device->CreateConstantBufferView(&desc, dst);
}

ID3DBlobComPtr CompileBlob(const char* src, const char* target, const char* mainName, unsigned int flags,
                           ID3DBlob* errors)
{
    ID3DBlobComPtr blob;

    auto result = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, mainName, target, flags, 0, &blob, &errors);
    if (FAILED(result))
    {
        assert(errors);

        std::wcout << g_outputTag << " CompileBlob failed " << static_cast<const char*>(errors->GetBufferPointer());

        return nullptr;
    }

    return blob;
}

ID3D12RootSignatureComPtr CreateComputeRootSignature(ID3D12Device* device, const std::vector<char>& rootSignatureSrc, 
                                                     const std::wstring& name)
{
    assert(device);
    assert(rootSignatureSrc.size());

    ID3DBlobComPtr errors;
    auto rootSignatureBlob = CompileBlob(&rootSignatureSrc[0], g_rootSignatureTarget, g_rootSignatureName, 0, errors.Get());
    if (!rootSignatureBlob)
    {
        std::wcout << g_outputTag << " [CreateComputeRootSignature] Failed to compile blob ";
        if (errors)
            std::cout << static_cast<const char*>(errors->GetBufferPointer()) << "\n";
        return nullptr;
    }

    ID3D12RootSignatureComPtr rootSignature;
    if (FAILED(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSignature))))
    {
        std::wcout << g_outputTag << " [CreateComputeRootSignature] CreateRootSignature call failed\n";
        return nullptr;
    }

    rootSignature->SetName(name.c_str());

    return rootSignature;
}

ID3DBlobComPtr CreateComputeShader(ID3D12Device* device, const std::vector<char>& computeShaderSrc)
{
    assert(device);
    assert(!computeShaderSrc.empty());

    ID3DBlobComPtr errors;
    auto computeShader = CompileBlob(&computeShaderSrc[0], g_computeShaderTarget, g_computeShaderMain, g_compileFlags, errors.Get());
    if (!computeShader)
    {
        std::wcout << g_outputTag << " [CreateComputeRootSignature] failed to compile blob ";
        if (errors)
            std::wcout << static_cast<const char*>(errors->GetBufferPointer()) << "\n";
        return nullptr;
    }

    return computeShader;
}

PipelineState CreatePipelineState(ID3D12Device* device, const std::wstring& shaderFileName, 
                                  const std::wstring& rootSignatureName,
                                  const std::wstring& pipelineStateName)
{
    assert(device);
    assert(!shaderFileName.empty());
    
    const auto shaderSrc = Utils::ReadFullFile(shaderFileName);
    if (shaderSrc.empty())
    {
        std::wcout << g_outputTag << " [CreatePipelineState] ReadFullFile " << shaderFileName.c_str() << " failed\n";
        return {};
    }

    auto rootSignature = CreateComputeRootSignature(device, shaderSrc, rootSignatureName);
    if (!rootSignature)
        return {};

    auto computeShader = CreateComputeShader(device, shaderSrc);
    if (!computeShader)
        return {};

    ID3D12PipelineStateComPtr pipelineState;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
    desc.pRootSignature = rootSignature.Get();
    desc.CS             = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };
    desc.NodeMask       = 0;
    desc.CachedPSO      = {nullptr,0};
    desc.Flags          = D3D12_PIPELINE_STATE_FLAG_NONE;

    Utils::AssertIfFailed(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelineState)));
    pipelineState->SetName(pipelineStateName.c_str());

    return { rootSignature, pipelineState };
}

ID3D12QueryHeapComPtr CreateTimestampQueryHeap(ID3D12Device* device, uint32_t timeStampsCount)
{
    assert(device);

    D3D12_QUERY_HEAP_DESC queryHeapDesc;
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryHeapDesc.Count = timeStampsCount;
    queryHeapDesc.NodeMask = 0;

    ID3D12QueryHeapComPtr queryHeap;
    Utils::AssertIfFailed(device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&queryHeap)));
    return queryHeap;
}

void EnqueueTimestampQuery(ID3D12GraphicsCommandList* cmdList, ID3D12QueryHeap* queryHeap,  uint32_t queryIndex)
{
    assert(cmdList);
    assert(queryHeap);
    cmdList->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
}

void EnqueueResolveTimestampQueries(ID3D12GraphicsCommandList* cmdList, ID3D12QueryHeap* queryHeap,
                                    uint32_t timeStampsCount, ID3D12Resource* readbackBuffer)
{
    assert(cmdList);
    assert(queryHeap);
    assert(timeStampsCount > 0);
    assert(readbackBuffer);

    cmdList->ResolveQueryData(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, timeStampsCount, readbackBuffer, 0);
}

template<class T, class F>
std::vector<T> ReadbackBuffer(ID3D12Resource* readbackBuffer, uint64_t sizeBytes, F&& ProcessData)
{
    assert(readbackBuffer);
    D3D12_RANGE readRange = {};
    readRange.Begin = 0;
    readRange.End = sizeBytes;

    void* data = nullptr;
    Utils::AssertIfFailed(readbackBuffer->Map(0, &readRange, &data));

    std::vector<T> buffer = ProcessData(data);

    D3D12_RANGE emptyRange = {};
    readbackBuffer->Unmap(0, &emptyRange);

    return buffer;
}

std::vector<double> ReadbackTimestamps(ID3D12Resource* readbackBuffer, uint32_t timestampsCount, 
                                       uint64_t cmdQueueTimestampFrequency)
{
    assert(readbackBuffer);
    assert(timestampsCount > 0);

    auto buffer = ReadbackBuffer<double>(readbackBuffer, timestampsCount * sizeof(uint64_t), 
                                         [&](void* data)
    {
        std::vector<double> timestamps(timestampsCount);
        // Note timestamps data are ticks and cmd queue timestamp frequency is ticks/sec
        const uint64_t* timestampsTicks = reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(data));
        for (uint32_t i = 0; i < timestampsCount; ++i)
        {
            timestamps[i] = timestampsTicks[i] / static_cast<double>(cmdQueueTimestampFrequency);
        }
        return timestamps;
    });
    return buffer;
}

int main(int argc, char** argv)
{
    std::wcout << "\n";

#if ENABLE_PIX_CAPTURE
    PixCapture pixCapture;
#endif

    auto dxgiAdapter = CreateDXGIAdapter();
    auto d3d12DevicePtr = CreateD3D12Device(dxgiAdapter);
    auto d3d12Device = d3d12DevicePtr.Get();

    // Create a compute shader
    const std::wstring computeShaderFileName = L"./data/shaders/simple.hlsl";
    PipelineState pipelineState = CreatePipelineState(d3d12Device, computeShaderFileName, L"Simple", L"Simple");
    if (!pipelineState.m_rootSignature || !pipelineState.m_pso)
        return -1;

    // Allocates buffers
    auto constantDataBuffer = AllocateReadOnlyBuffer(d3d12Device, sizeof(ConstantData), g_cpyDstState, L"ConstantData");
    const size_t threadsPerGroup = 64;
    const size_t threadGroupsCount = 16;
    const size_t dataElementsCount = threadsPerGroup * threadGroupsCount;
    const uint64_t dataSizeBytes = dataElementsCount * sizeof(float);
    auto inputBuffer = AllocateReadOnlyBuffer(d3d12Device, dataSizeBytes, g_cpyDstState, L"Input");
    const uint64_t dataPerGroupSizeBytes = threadGroupsCount * sizeof(float);
    auto inputPerGroupBuffer = AllocateReadOnlyBuffer(d3d12Device, dataPerGroupSizeBytes, g_cpyDstState, L"Input Per Thread Group");
    auto outputBuffer = AllocateRWBuffer(d3d12Device, dataSizeBytes, g_uaState, L"Output");
    auto readbackBuffer = AllocateReadbackBuffer(d3d12Device, dataSizeBytes, L"Readback");
    const uint64_t timestampsCount = 2;
    const uint64_t timestampBufferSize = timestampsCount * sizeof(uint64_t);
    auto timeStampBuffer = AllocateReadbackBuffer(d3d12Device, timestampBufferSize, L"TimeStamp");

    // Upload data to gpu memory
    auto computeCmdQueue = CreateComputeCmdQueue(d3d12Device);
    auto computeCmdList = CreateComputeCommandList(d3d12Device, L"Compute");
    auto copyCmdQueue = CreateCopyCmdQueue(d3d12Device);
    auto copyCmdList = CreateCopyCommandList(d3d12Device, L"Copy");
    {
        ConstantData constantData{ -1.0f };
        auto constantDataTmp = EnqueueUploadDataToBuffer(d3d12Device, copyCmdList.m_cmdList.Get(), 
                                                          constantDataBuffer.m_resource.Get(), 
                                                          &constantData, sizeof(ConstantData));

        std::vector<float> inputData(dataElementsCount);
        std::generate(inputData.begin(), inputData.end(), [v = 0.0f]() mutable
        {
            return v++;
        });
        auto inputBufferTmp = EnqueueUploadDataToBuffer(d3d12Device, copyCmdList.m_cmdList.Get(), 
                                                        inputBuffer.m_resource.Get(), 
                                                        &inputData[0], dataSizeBytes);

        std::vector<float> inputDataPerThreadGroup(threadGroupsCount);
        std::generate(inputDataPerThreadGroup.begin(), inputDataPerThreadGroup.end(), [v = 1.0f]() mutable
        {
            return v++;
        });
        auto inputPerGroupBuffertmp = EnqueueUploadDataToBuffer(d3d12Device, copyCmdList.m_cmdList.Get(), 
                                                                inputPerGroupBuffer.m_resource.Get(),
                                                                &inputDataPerThreadGroup[0], dataPerGroupSizeBytes);

        ExecuteCmdList(d3d12Device, copyCmdQueue.m_cmdQueue.Get(), copyCmdList.m_cmdList.Get());

        std::vector<ResourceTransitionData> dsts
        {
            { constantDataBuffer.m_resource.Get(), g_cbState},
            { inputBuffer.m_resource.Get(), g_bufferState},
            { inputPerGroupBuffer.m_resource.Get(), g_bufferState}
        };

        TransitionCopyDstResources(dsts, computeCmdList.m_cmdList.Get());
    }

    // Creates descriptors
    const uint32_t descriptorsCount = 2;
    DescriptorHeap descriptorHeap = CreateDescriptorHeap(d3d12Device, descriptorsCount);
    auto descriptorTable = descriptorHeap.m_currentGpuHandle;
    {
        const DXGI_FORMAT inputBufferFormat = DXGI_FORMAT_R32_FLOAT;
        CreateBufferDescriptor(d3d12Device, descriptorHeap.m_currentCpuHandle, inputBuffer.m_resource.Get(),
                               inputBufferFormat, dataElementsCount);
        AddDescriptor(descriptorHeap);
        const DXGI_FORMAT outputBufferFormat = DXGI_FORMAT_R32_FLOAT;
        CreateRWBufferDescriptor(d3d12Device, descriptorHeap.m_currentCpuHandle, outputBuffer.m_resource.Get(),
                                 outputBufferFormat, dataElementsCount);
        AddDescriptor(descriptorHeap);
    }

    // Start clock
    auto& d3d12Cmdlist = computeCmdList.m_cmdList;
    auto timestampQueryHeap = CreateTimestampQueryHeap(d3d12Device, timestampsCount);
    EnqueueTimestampQuery(d3d12Cmdlist.Get(), timestampQueryHeap.Get(), 0);

    // Setup state
    ID3D12DescriptorHeap* d3d12DescriptorHeaps[] = { descriptorHeap.m_d3d12DescriptorHeap.Get() };
    d3d12Cmdlist->SetDescriptorHeaps(1, d3d12DescriptorHeaps);
    d3d12Cmdlist->SetComputeRootSignature(pipelineState.m_rootSignature.Get());
    d3d12Cmdlist->SetComputeRootDescriptorTable(0, descriptorTable);
    d3d12Cmdlist->SetComputeRootConstantBufferView(1, constantDataBuffer.m_resource->GetGPUVirtualAddress());
    d3d12Cmdlist->SetComputeRootShaderResourceView(2, inputPerGroupBuffer.m_resource->GetGPUVirtualAddress());
    d3d12Cmdlist->SetPipelineState(pipelineState.m_pso.Get());

    // Dispatch and execute
    d3d12Cmdlist->Dispatch(threadGroupsCount, 1, 1);

    // End clock
    EnqueueTimestampQuery(d3d12Cmdlist.Get(), timestampQueryHeap.Get(), 1);
    EnqueueResolveTimestampQueries(d3d12Cmdlist.Get(), timestampQueryHeap.Get(), timestampsCount, timeStampBuffer.m_resource.Get());

    D3D12_RESOURCE_BARRIER transition = CreateTransition(outputBuffer.m_resource.Get(), 
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                         D3D12_RESOURCE_STATE_COMMON);
    d3d12Cmdlist->ResourceBarrier(1, &transition);

    ExecuteCmdList(d3d12Device, computeCmdQueue.m_cmdQueue.Get(), d3d12Cmdlist.Get());

    // Read readback buffer
    {
        // Copy from default buffer to readback buffer
        copyCmdList.m_cmdList->Reset(copyCmdList.m_allocator.Get(), nullptr);
        EnqueueCopyBuffer(d3d12Device, copyCmdList.m_cmdList.Get(), 
                          readbackBuffer.m_resource.Get(), outputBuffer.m_resource.Get());
        ExecuteCmdList(d3d12Device, copyCmdQueue.m_cmdQueue.Get(), copyCmdList.m_cmdList.Get());

        // Read from readback buffer
        std::vector<float> cpuOutputBuffer = ReadbackBuffer<float>(readbackBuffer.m_resource.Get(), dataSizeBytes, 
                                                                   [&](void* data)
        {
            std::vector<float> outputBuffer(dataElementsCount);
            memcpy(&outputBuffer[0], data, dataSizeBytes);
            return outputBuffer;
        });

        std::wcout << g_outputTag << "[Output Buffer] ";
        for (size_t i = 0; i < cpuOutputBuffer.size(); ++i)
        {
            std::wcout << " " << i << ":"<< cpuOutputBuffer[i] << " ";
        }
        std::cout << "\n";
    }
    
    // Read timestamp buffer
    auto timestamps = ReadbackTimestamps(timeStampBuffer.m_resource.Get(), timestampsCount, computeCmdQueue.m_timestampFrequency);
    const double deltaMicroSecs = (timestamps[1] - timestamps[0]) * 1000000.0;
    std::wcout << g_outputTag << "[Performance] GPU execution time " << deltaMicroSecs << "us\n";

#if ENABLE_D3D12_DEBUG_LAYER
    ReportLiveObjects();
#endif

    return 0;
}