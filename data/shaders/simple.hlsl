#define SimpleRootSig                       \
    "RootFlags( 0 ),"                       \
    "DescriptorTable( UAV(u0) )"            \
    //"CBV(b0),"                              \
    //"DescriptorTable( SRV(t0), UAV(u0) ),"  \
    //"SRV(t1)"

//struct ConstantData
//{
//    float m_float;
//};
//ConstantBuffer<ConstantData> g_constantData : register(b0);

//Buffer<float>           g_inputData     : register(t0);
//StructuredBuffer<float> g_inputData1    : register(t1);
RWBuffer<float>         g_outputData    : register(u0);

[numthreads( 64, 1, 1 )]
void main(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex, 
          uint3 groupThreadId : SV_GroupThreadID, uint3 dispatchThreadId : SV_DispatchThreadID )
{
    //uint threadGroupId = groupId.x;
    uint threadId = dispatchThreadId.x;
    //g_outputData[threadId] = g_inputData[threadId] * g_constantData.m_float + g_inputData1[threadGroupId];
    g_outputData[threadId] = -1.0f;
}