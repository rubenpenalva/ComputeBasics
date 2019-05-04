#include "gpumemory.h"

#include "utils.h"

namespace
{
    D3D12_RESOURCE_DIMENSION TextureDescToResourceDimension(ComputeBasics::TextureType type)
    {
        return  type == ComputeBasics::TextureType::Texture1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                : type == ComputeBasics::TextureType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D
                : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    }

    D3D12_FORMAT_SUPPORT1 TextureTypeToSupportFormat(ComputeBasics::TextureType type)
    {
        return  type == ComputeBasics::TextureType::Texture1D ? D3D12_FORMAT_SUPPORT1_TEXTURE1D
                : type == ComputeBasics::TextureType::Texture2D ? D3D12_FORMAT_SUPPORT1_TEXTURE2D
                : D3D12_FORMAT_SUPPORT1_TEXTURE3D;
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

    D3D12_RESOURCE_DESC CreateTextureDesc(const ComputeBasics::TextureDesc& desc, bool isUA)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension          = TextureDescToResourceDimension(desc.m_type);
        resourceDesc.Alignment          = 0;
        resourceDesc.Width              = desc.m_width;
        resourceDesc.Height             = desc.m_height;
        resourceDesc.DepthOrArraySize   = desc.m_depth;
        resourceDesc.MipLevels          = desc.m_mipsCount;
        resourceDesc.Format             = desc.m_format;
        resourceDesc.SampleDesc         = { 1, 0 };
        resourceDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags              = isUA? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

        return resourceDesc;
    }

    ComputeBasics::GpuMemAllocation CreateBuffer(ID3D12Device* device, uint64_t sizeBytes, D3D12_HEAP_TYPE heapType,
                                                 D3D12_RESOURCE_STATES initialState, bool isUA, const std::wstring& name)
    {
        assert(device);

        size_t bufferAligment               = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        const auto alignedSize              = Utils::AlignToPowerof2(sizeBytes, bufferAligment);
        D3D12_RESOURCE_DESC resourceDesc    = CreateBufferDesc(alignedSize, isUA);
    
        ID3D12ResourceComPtr resource = CreateCommitedResource(device, heapType, resourceDesc, initialState, name);
        return ComputeBasics::GpuMemAllocation{ resource };
    }

    ComputeBasics::GpuMemAllocation CreateTexture(ID3D12Device* device, 
                                                  const ComputeBasics::TextureDesc& textureDesc, D3D12_HEAP_TYPE heapType,
                                                  D3D12_RESOURCE_STATES initialState, bool isUA, const std::wstring& name)
    {
        assert(device);
        assert(Utils::CheckFormatSupport(device, textureDesc.m_format, TextureTypeToSupportFormat(textureDesc.m_type)));
        assert(!isUA || 
               (isUA && Utils::CheckFormatSupport(device, textureDesc.m_format, 
                                                  static_cast<D3D12_FORMAT_SUPPORT2>
                                                  (D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE ||
                                                  D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))));

        D3D12_RESOURCE_DESC resourceDesc = CreateTextureDesc(textureDesc, isUA);

        ID3D12ResourceComPtr resource = CreateCommitedResource(device, heapType, resourceDesc, initialState, name);
        return ComputeBasics::GpuMemAllocation{ resource };
    }
}

ComputeBasics::ScopedMappedGpuMemAlloc::ScopedMappedGpuMemAlloc(const GpuMemAllocation& allocation) : m_allocation(allocation)
{
    m_buffer = MemMap(allocation);
}

ComputeBasics::ScopedMappedGpuMemAlloc::~ScopedMappedGpuMemAlloc()
{
    MemUnmap(m_allocation);
}

ComputeBasics::GpuMemAllocation ComputeBasics::Allocate(ID3D12Device* device, uint64_t sizeBytes, bool isRW, const std::wstring& name)
{
    const auto heapType     = D3D12_HEAP_TYPE_DEFAULT;
    const auto initialState = isRW? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON;

    return CreateBuffer(device, sizeBytes, heapType, initialState, isRW, name);
}

ComputeBasics::GpuMemAllocation ComputeBasics::Allocate(ID3D12Device* device, const TextureDesc& desc, bool isRW, const std::wstring& name)
{
    const auto heapType     = D3D12_HEAP_TYPE_DEFAULT;
    const auto initialState = isRW ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON;
    return CreateTexture(device, desc, heapType, initialState, isRW, name);
}

ComputeBasics::GpuMemAllocation ComputeBasics::AllocateUpload(ID3D12Device* device, uint64_t sizeBytes, const std::wstring& name)
{
    const auto heapType = D3D12_HEAP_TYPE_UPLOAD;
    const auto initialState = D3D12_RESOURCE_STATE_GENERIC_READ;

    return CreateBuffer(device, sizeBytes, heapType, initialState, false, name);
}

ComputeBasics::GpuMemAllocation ComputeBasics::AllocateReadback(ID3D12Device* device, uint64_t sizeBytes, const std::wstring& name)
{
    const auto heapType = D3D12_HEAP_TYPE_READBACK;
    const auto initialState = D3D12_RESOURCE_STATE_COPY_DEST;

    return CreateBuffer(device, sizeBytes, heapType, initialState, false, name);
}

ComputeBasics::GpuMemAllocation ComputeBasics::AllocateReadback(ID3D12Device* device, const TextureDesc& desc, const std::wstring& name)
{
    const auto heapType = D3D12_HEAP_TYPE_READBACK;
    const auto initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    return CreateTexture(device, desc, heapType, initialState, false, name);
}

void* ComputeBasics::MemMap(const ComputeBasics::GpuMemAllocation& allocation)
{
    assert(allocation.m_resource);

    void* dst = nullptr;
    Utils::AssertIfFailed(allocation.m_resource->Map(0, nullptr, reinterpret_cast<void**>(&dst)));
    assert(dst);

    return dst;
}

void ComputeBasics::MemUnmap(const ComputeBasics::GpuMemAllocation& allocation)
{
    assert(allocation.m_resource);
    allocation.m_resource->Unmap(0, nullptr);
}

void ComputeBasics::MemCpy(ComputeBasics::GpuMemAllocation& dst, const void* src, size_t sizeBytes)
{
    assert(dst.m_resource);
    assert(src);
    assert(sizeBytes);
    
    void* dstBuffer = MemMap(dst);

    Utils::AssertIfFailed(dst.m_resource->Map(0, nullptr, reinterpret_cast<void**>(&dstBuffer)));
    assert(dstBuffer);

    memcpy(dstBuffer, src, sizeBytes);

    MemUnmap(dst);
}

void ComputeBasics::MemCpy(void* dst, const GpuMemAllocation& src, uint64_t sizeBytes)
{
    assert(dst);
    assert(src.m_resource);
    assert(sizeBytes);

    void* srcBuffer = MemMap(src);

    memcpy(dst, srcBuffer, sizeBytes);

    MemUnmap(src);
}