#pragma once

#include "common.h"

#include "gpumemory.h"

namespace ComputeBasics
{

struct Descriptor
{
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle;
};

// Note this is a very barebones implementation of a heap of descriptors.
// Its fixed to live in the gpu and being of type cbv_srv_uav.
// Only provides push semantics.
class DescriptorHeap
{
public:
    DescriptorHeap(ID3D12Device* device, uint32_t descriptorsCount);

    ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const { return m_d3d12DescriptorHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE BeginGpuHandle() const { return m_beginGpuHandle; }

    Descriptor CreateConstantBufferDescriptor(const GpuMemAllocation& allocation, uint32_t sizeBytes);
    Descriptor CreateBufferDescriptor(const GpuMemAllocation& allocation, DXGI_FORMAT format, uint32_t elementsCount, bool isRW);
    Descriptor CreateByteBufferDescriptor(const GpuMemAllocation& allocation, uint32_t elementsCount, bool isRW);
    Descriptor CreateStructuredBufferDescriptor(const GpuMemAllocation& allocation, uint32_t elementsCount,
                                                uint32_t structureByteStride, bool isRW);

private:
    ID3D12Device* m_device;

    ID3D12DescriptorHeapComPtr  m_d3d12DescriptorHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE m_beginGpuHandle;

    D3D12_GPU_DESCRIPTOR_HANDLE m_currentGpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_currentCpuHandle;

    uint32_t                    m_descriptorHandleIncrementSize;

    Descriptor Top();
    void Push();
};

}