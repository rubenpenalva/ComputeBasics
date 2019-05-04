#include "descriptors.h"

#include <cassert>
#include "Utils.h"

namespace 
{
    void CreateBufferSRV(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT format, 
                         uint32_t elementsCount, uint32_t structuredStride, bool isRAW, 
                         D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = elementsCount;
        desc.Buffer.StructureByteStride = structuredStride;
        desc.Buffer.Flags = isRAW? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;

        device->CreateShaderResourceView(resource, &desc, cpuHandle);
    }

    void CreateBufferUAV(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT format,
                         uint32_t elementsCount, uint32_t structuredStride, bool isRAW, 
                         D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = elementsCount;
        desc.Buffer.StructureByteStride = structuredStride;
        desc.Buffer.CounterOffsetInBytes = 0;
        desc.Buffer.Flags = isRAW? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;

        device->CreateUnorderedAccessView(resource, nullptr, &desc, cpuHandle);
    }

    void CreateBufferView(ID3D12Device* device, ID3D12Resource* resource, DXGI_FORMAT format,
                          uint32_t elementsCount, uint32_t structuredStride, bool isRAW, bool isRW,
                          D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
    {
        assert(device);
        assert(resource);
        assert(elementsCount > 0);

        if (isRW)
        {
            CreateBufferUAV(device, resource, format, elementsCount, structuredStride, isRAW, cpuHandle);
        }
        else
        {
            CreateBufferSRV(device, resource, format, elementsCount, structuredStride, isRAW, cpuHandle);
        }
    }
}

using namespace ComputeBasics;

DescriptorHeap::DescriptorHeap(ID3D12Device * device, uint32_t descriptorsCount) : m_device(device)
{
    assert(m_device);
    assert(descriptorsCount > 0);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = descriptorsCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 0;

    Utils::AssertIfFailed(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_d3d12DescriptorHeap)));

    m_currentGpuHandle = m_d3d12DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    m_currentCpuHandle = m_d3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    m_beginGpuHandle = m_currentGpuHandle;

    uint32_t incrementSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_descriptorHandleIncrementSize = incrementSize;
}

Descriptor DescriptorHeap::CreateConstantBufferDescriptor(const GpuMemAllocation& allocation, uint32_t sizeBytes)
{
    assert(allocation.m_resource);

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
    desc.BufferLocation = allocation.m_resource->GetGPUVirtualAddress();
    desc.SizeInBytes = sizeBytes;
    m_device->CreateConstantBufferView(&desc, m_currentCpuHandle);

    auto currentDescriptor = Top();
    Push();
    return currentDescriptor;
}

Descriptor DescriptorHeap::CreateBufferDescriptor(const GpuMemAllocation& allocation, DXGI_FORMAT format, 
                                                  uint32_t elementsCount, bool isRW)
{
    assert(allocation.m_resource);
    assert(format != DXGI_FORMAT_UNKNOWN);
    assert(elementsCount > 0);

    auto currentDescriptor = Top();
    CreateBufferView(m_device, allocation.m_resource.Get(), format, elementsCount, 0, 
                     false, isRW, currentDescriptor.m_cpuHandle);
    Push();
    return currentDescriptor;
}

Descriptor DescriptorHeap::CreateByteBufferDescriptor(const GpuMemAllocation& allocation, uint32_t elementsCount, bool isRW)
{
    assert(allocation.m_resource);

    auto currentDescriptor = Top();
    CreateBufferView(m_device, allocation.m_resource.Get(), DXGI_FORMAT_UNKNOWN, elementsCount, 0, 
                     true, isRW, currentDescriptor.m_cpuHandle);
    Push();
    return currentDescriptor;
}

Descriptor DescriptorHeap::CreateStructuredBufferDescriptor(const GpuMemAllocation& allocation, uint32_t elementsCount,
                                                            uint32_t structureByteStride, bool isRW)
{
    assert(allocation.m_resource);

    auto currentDescriptor = Top();
    CreateBufferView(m_device, allocation.m_resource.Get(), DXGI_FORMAT_UNKNOWN, elementsCount, structureByteStride, false, isRW,
                     currentDescriptor.m_cpuHandle);
    Push();
    return currentDescriptor;
}

Descriptor DescriptorHeap::Top()
{
    return Descriptor{ m_currentGpuHandle, m_currentCpuHandle };
}

void DescriptorHeap::Push()
{
    m_currentCpuHandle.ptr += m_descriptorHandleIncrementSize;
    m_currentGpuHandle.ptr += m_descriptorHandleIncrementSize;
}
