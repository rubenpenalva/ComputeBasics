#include "common.h"

#include <d3dcompiler.h>
#include <iostream>
#include <algorithm>

#include "utils.h"
#include "cmdqueuesyncer.h"
#include "gpumemory.h"
#include "descriptors.h"

#if ENABLE_D3D12_DEBUG_LAYER
#include <Initguid.h>
#include <dxgidebug.h>
#endif

namespace ComputeBasics
{

struct CommandQueue
{
    ID3D12CommandQueueComPtr    m_cmdQueue;
    uint64_t                    m_timestampFrequency;
};

struct CommandList
{
    ID3D12CommandAllocatorComPtr    m_allocator;
    ID3D12GraphicsCommandListComPtr m_cmdList;
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

// TODO move the command queues and lists codes to its own file
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
GpuMemAllocation EnqueueUploadDataToBuffer(ID3D12Device* device,
                                           ID3D12GraphicsCommandList* copyCmdList,
                                           ID3D12Resource* dst, const void* data, uint64_t sizeBytes)
{
    assert(device);
    assert(copyCmdList);
    assert(dst);
    assert(data && sizeBytes);

    assert(copyCmdList->GetType() == D3D12_COMMAND_LIST_TYPE_COPY);

    // Create temporal buffer in the upload heap
    GpuMemAllocation src = AllocateUpload(device, sizeBytes, L"Upload temp buffer");
    MemCpy(src, data, sizeBytes);

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

// TODO move pipelinestate stuff to other file
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

std::vector<double> ReadbackTimestamps(const GpuMemAllocation& allocation, uint32_t timestampsCount, 
                                       uint64_t cmdQueueTimestampFrequency)
{
    assert(timestampsCount > 0);

    ScopedMappedGpuMemAlloc memMap(allocation);

    std::vector<double> timestamps(timestampsCount);
    // Note timestamps data are ticks and cmd queue timestamp frequency is ticks/sec
    const uint64_t* timestampsTicks = reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(memMap.GetBuffer()));
    for (uint32_t i = 0; i < timestampsCount; ++i)
    {
        timestamps[i] = timestampsTicks[i] / static_cast<double>(cmdQueueTimestampFrequency);
    }

    return timestamps;
}

}

using namespace ComputeBasics;

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
    auto constantDataBuffer = Allocate(d3d12Device, sizeof(ConstantData), false, L"ConstantData");
    const size_t threadsPerGroup = 64;
    const size_t threadGroupsCount = 16;
    const size_t dataElementsCount = threadsPerGroup * threadGroupsCount;
    const uint64_t dataSizeBytes = dataElementsCount * sizeof(float);
    auto inputBuffer = Allocate(d3d12Device, dataSizeBytes, false, L"Input");
    const uint64_t dataPerGroupSizeBytes = threadGroupsCount * sizeof(float);
    auto inputPerGroupBuffer = Allocate(d3d12Device, dataPerGroupSizeBytes, false, L"Input Per Thread Group");
    auto outputBuffer = Allocate(d3d12Device, dataSizeBytes, true, L"Output");
    auto readbackBuffer = AllocateReadback(d3d12Device, dataSizeBytes, L"Readback");
    const uint64_t timestampsCount = 2;
    const uint64_t timestampBufferSize = timestampsCount * sizeof(uint64_t);
    auto timeStampBuffer = AllocateReadback(d3d12Device, timestampBufferSize, L"TimeStamp");

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
    ComputeBasics::DescriptorHeap descriptorHeap(d3d12Device, descriptorsCount);
    descriptorHeap.CreateBufferDescriptor(inputBuffer, DXGI_FORMAT_R32_FLOAT, dataElementsCount, false);
    descriptorHeap.CreateBufferDescriptor(outputBuffer, DXGI_FORMAT_R32_FLOAT, dataElementsCount, true);

    // Start clock
    auto& d3d12Cmdlist = computeCmdList.m_cmdList;
    auto timestampQueryHeap = CreateTimestampQueryHeap(d3d12Device, timestampsCount);
    EnqueueTimestampQuery(d3d12Cmdlist.Get(), timestampQueryHeap.Get(), 0);

    // Setup state
    ID3D12DescriptorHeap* d3d12DescriptorHeaps[] = { descriptorHeap.GetD3D12DescriptorHeap() };
    d3d12Cmdlist->SetDescriptorHeaps(1, d3d12DescriptorHeaps);
    d3d12Cmdlist->SetComputeRootSignature(pipelineState.m_rootSignature.Get());
    d3d12Cmdlist->SetComputeRootDescriptorTable(0, descriptorHeap.BeginGpuHandle());
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

        std::vector<float> readbackData(dataElementsCount);
        {
            ScopedMappedGpuMemAlloc scopedMappedAlloc(readbackBuffer);
            memcpy(&readbackData[0], scopedMappedAlloc.GetBuffer(), dataSizeBytes);
        }

        std::wcout << g_outputTag << "[Output Buffer] ";
        for (size_t i = 0; i < readbackData.size(); ++i)
        {
            std::wcout << " " << i << ":"<< readbackData[i] << " ";
        }
        std::cout << "\n";
    }
    
    // Read timestamp buffer
    auto timestamps = ReadbackTimestamps(timeStampBuffer, timestampsCount, computeCmdQueue.m_timestampFrequency);
    const double deltaMicroSecs = (timestamps[1] - timestamps[0]) * 1000000.0;
    std::wcout << g_outputTag << "[Performance] GPU execution time " << deltaMicroSecs << "us\n";

#if ENABLE_D3D12_DEBUG_LAYER
    ReportLiveObjects();
#endif

    return 0;
}