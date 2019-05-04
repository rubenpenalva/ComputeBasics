#pragma once

#include "common.h"

namespace ComputeBasics
{

struct GpuMemAllocation
{
    ID3D12ResourceComPtr m_resource;
};

enum TextureType
{
    Texture1D,
    Texture2D,
    Texture3D
};

struct TextureDesc
{
    TextureType m_type;
    uint64_t    m_width;
    uint32_t    m_height;
    union
    {
        uint16_t    m_depth;
        uint16_t    m_arraySize;
    };
    // mipsCount = 0 : automatically calculates it
    uint16_t    m_mipsCount;
    DXGI_FORMAT m_format;
};

class ScopedMappedGpuMemAlloc
{
public:
    ScopedMappedGpuMemAlloc(const GpuMemAllocation& allocation);
    ~ScopedMappedGpuMemAlloc();

    void* GetBuffer() { return m_buffer; }
private:
    const GpuMemAllocation& m_allocation;
    void* m_buffer;
};

GpuMemAllocation Allocate(ID3D12Device* device, uint64_t sizeBytes, bool isRW, const std::wstring& name);
GpuMemAllocation Allocate(ID3D12Device* device, const TextureDesc& desc, bool isRW, const std::wstring& name);
GpuMemAllocation AllocateUpload(ID3D12Device* device, uint64_t sizeBytes, const std::wstring& name);
GpuMemAllocation AllocateReadback(ID3D12Device* device, uint64_t sizeBytes, const std::wstring& name);
GpuMemAllocation AllocateReadback(ID3D12Device* device, const TextureDesc& desc, const std::wstring& name);

void* MemMap(const GpuMemAllocation& allocation);
void MemUnmap(const GpuMemAllocation& allocation);
void MemCpy(GpuMemAllocation& dst, const void* src, size_t sizeBytes);
void MemCpy(void* dst, const GpuMemAllocation& src, uint64_t sizeBytes);

}