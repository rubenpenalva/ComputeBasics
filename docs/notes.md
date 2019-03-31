# ComputeBasics notes

## Questions
### When to use (RW)StructuredBuffer vs (RW)Buffer?
When setting a `Buffer<float>` as a root srv this error comes up: 
D3D12 ERROR: ID3D12Device::CreateComputePipelineState: Root Signature doesn't match Compute Shader: A Shader is declaring a resource object as a texture using a register mapped to a root descriptor SRV (ShaderRegister=1, RegisterSpace=0).  SRV or UAV root descriptors can only be Raw or Structured buffers.*
According to msdn [Using descriptors directly in the root signature](https://docs.microsoft.com/en-us/windows/desktop/direct3d12/using-descriptors-directly-in-the-root-signature): "The only types of descriptors supported in the root signature are CBVs and SRV/UAVs of buffer resources, where the SRV/UAV format contains only 32 bit FLOAT/UINT/SINT components. There is no format conversion."
Switching to an StructuredBuffer<float> fixes the issue. **Why?**

## TODO
Add texture support
Readback heap vs default&copy&readback
    Test writing and then reading a buffer from a readback heap vs writing to a default buffer, copying it to a readback heap and then reading it.