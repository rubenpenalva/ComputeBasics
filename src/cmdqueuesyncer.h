#pragma once

#include "d3d12fwd.h"

#include <memory>
#include <vector>

// NOTE Every time a work is signaled, an event and a fence are created.
// This is potentially a performance issue. Solution would be to have a 
// pool of fences and events and reuse them.
// Good enough for now.
class CmdQueueSyncer
{
public:
    CmdQueueSyncer(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
    ~CmdQueueSyncer();

    uint64_t SignalWork();

    void Wait(uint64_t workId);

private:
    class WorkWaiter;
    using WorkWaiterPtr     = std::unique_ptr<WorkWaiter>;

    ID3D12Device*       m_device;
    ID3D12CommandQueue* m_cmdQueue;

    uint64_t m_nextWorkId;

    std::vector<WorkWaiterPtr> m_workWaiters;

    WorkWaiter* CreateWorkWaiter();
    uint64_t CurrentWorkId();

};