# Resources

## Documentation
(Directx 11 Specs: 4.4 The Element)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#4.4%20The%20Element]
(Directx 11 Specs: 5 Resources)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#5%20Resources]
(Directx 11 Specs: 7.5 Constant Buffers)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#7.5%20Constant%20Buffers]
(Directx 11 Specs: 7.16 Textures and Resources Loading)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#7.16%20Textures%20and%20Resource%20Loading]
(Directx 11 Specs: 7.17 Texture Load)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#7.17%20Texture%20Load]
(Directx 11 Specs: 7.18 Texture Sampling)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#7.18%20Texture%20Sampling]
(Directx 12 Specs: D3D12 Resource Binding Functional Spec)[https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html]
(Directx 12 Specs: UAV Typed Load)[https://microsoft.github.io/DirectX-Specs/d3d/UAVTypedLoad.html]
(Directx 11 Specs: 7.11 Uniform Indexing of Resources and Samplers)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#7.11%20Uniform%20Indexing%20of%20Resources%20and%20Samplers]
(Directx 11 Specs: 7.14 Shader Memory Model)[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#7.14%20Shader%20Memory%20Consistency%20Model]
(Directx 11 Specs: Stage-Memory IO[https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#19%20Stage-Memory%20I/O]


## Types
Resource Types (aka memory storage)
    Buffer
        Constant Buffer
    Texture1D(Array)
    Texture2D(Array)
    Texture3D
    TextureCube(Array)
Memory Structure
    Unstructured buffer
    Structured buffer
    Raw
    Prestructured+Typeless
    Prestructured+Typed


(RW)Constant Buffer 
(RW)Typed Buffer (view specifies format)
    pipeline flag: shader resource input
        typed format in descriptor : load: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#inst_LD
(RW)Structured Buffer
    pipeline flag: shader resource input
        ld_structured: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#inst_LD_STRUCTURED
(RW)Raw Buffer
        pipeline flag: shader resource input
        ld_raw: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#inst_LD_RAW



Creation time
    Buffer or Texture
    UAV
    MS
    Array

Binding Time
    Unstructured Buffers (is, ib&vb)
    Constant Buffer
    Typed Buffer
    Structured Buffer
    Raw Buffer
    UAV
    SRV
    Sampler

## HLSL Objects
Read
    Buffer
    StructuredBuffer
    AppendStructuredBuffer
    ByteAddressBuffer
    ConsumeStructuredBuffer
    InputPatch
    OutputPatch
    Texture1D
    Texture1DArray
    Texture2D
    Texture2DArray
    Texture2DMS
    Texture2DMSArray
    Texture3D
    TextureCube
    TextureCubeArray
RW
    RWBuffer
    RWByteAddressBuffer
    RWStructuredBuffer
    RWTexture1D
    RWTexture1DArray
    RWTexture2D
    RWTexture2DArray
    RWTexture3D

## API
CreateResource
    D3D12_RESOURCE_DESC 
        D3D12_RESOURCE_DIMENSION 
            D3D12_RESOURCE_DIMENSION_UNKNOWN,
            D3D12_RESOURCE_DIMENSION_BUFFER,
            D3D12_RESOURCE_DIMENSION_TEXTURE1D,
            D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            D3D12_RESOURCE_DIMENSION_TEXTURE3D
        DXGI_FORMAT
        D3D12_RESOURCE_FLAGS
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
            D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER,
            D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
            D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY
Descriptors
    D3D12_INDEX_BUFFER_VIEW
    D3D12_VERTEX_BUFFER_VIEW
    CreateConstantBufferView
        D3D12_CONSTANT_BUFFER_VIEW_DESC
    CreateShaderResourceView
        D3D12_SHADER_RESOURCE_VIEW_DESC
    CreateSampler
        D3D12_SAMPLER_DESC
    CreateUnorderedAccessView
        D3D12_UNORDERED_ACCESS_VIEW_DESC 
    D3D12_STREAM_OUTPUT_DESC
    CreateRenderTargetView
        D3D12_RENDER_TARGET_VIEW_DESC 
    CreateDepthStencilView
        D3D12_DEPTH_STENCIL_VIEW_DESC