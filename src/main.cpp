#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>

#include <iostream>
#include <cassert>

#include "utils.h"
#include "cmdqueuesyncer.h"

// Note actually comptr is not a smart ptr but a raii class using
// IUnknown AddRef and Release functions
using IDXGIAdapter1ComPtr  = Microsoft::WRL::ComPtr<IDXGIAdapter1>;
using IDXGIFactory6ComPtr       = Microsoft::WRL::ComPtr<IDXGIFactory6>;
using ID3D12DeviceComPtr        = Microsoft::WRL::ComPtr<ID3D12Device>;
using ID3D12CommandQueueComPtr  = Microsoft::WRL::ComPtr<ID3D12CommandQueue>;

#define ENABLE_D3D12_DEBUG_LAYER            ( 1 )
#define ENABLE_D3D12_DEBUG_GPU_VALIDATION   ( 1 )

struct CommandQueue
{
    ID3D12CommandQueueComPtr    m_cmdQueue;
    uint64_t                    m_timestampFrequency;
};

IDXGIAdapter1ComPtr CreateDXGIAdapter()
{
    IDXGIFactory6* factory;
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

CommandQueue CreateCommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, bool disableTimeout)
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

    return cmdQueue;
}

CommandQueue CreateComputeCmdQueue(ID3D12Device* device)
{
    assert(device);
    return CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, true);
}

int main(int argc, char** argv)
{
    // Create the device
    auto dxgiAdapter = CreateDXGIAdapter();
    auto d3d12Device = CreateD3D12Device(dxgiAdapter);

    // Create a buffer
    // Create a compute shader

    // Dispatch cs to write stuff on the buffer
    auto computeCmdQueue = CreateComputeCmdQueue(d3d12Device.Get());
    CmdQueueSyncer cmdQueueSyncer(d3d12Device.Get(), computeCmdQueue.m_cmdQueue.Get());
    cmdQueueSyncer.SignalWork();

    // Readback that buffer to main memory
    // Output that buffer to console

    return 0;
}