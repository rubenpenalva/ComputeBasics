#include "CmdQueueSyncer.h"

#include <wrl.h>
#include <d3d12.h>

#include "utils.h"
#include <cassert>

class CmdQueueSyncer::WorkWaiter
{
public:
    WorkWaiter(ID3D12Device* device, uint64_t workId);
    ~WorkWaiter();

    ID3D12Fence* GetFence() const { return m_fence.Get(); }
    HANDLE GetEvent() const { return m_event; }

private:
    using ID3D12FenceComPtr = Microsoft::WRL::ComPtr<ID3D12Fence>;

    ID3D12FenceComPtr   m_fence;
    HANDLE              m_event;
};

CmdQueueSyncer::WorkWaiter::WorkWaiter(ID3D12Device* device, uint64_t workID)
{
    assert(device);

    Utils::AssertIfFailed(device->CreateFence(workID, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    assert(m_fence);

    m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_event);
}

CmdQueueSyncer::WorkWaiter::~WorkWaiter()
{
    CloseHandle(m_event);
}

CmdQueueSyncer::CmdQueueSyncer(ID3D12Device* device, 
                               ID3D12CommandQueue* cmdQueue) : m_device(device), m_cmdQueue(cmdQueue), m_nextWorkId(0)
{
    assert(m_device);
    assert(m_cmdQueue);
}

// NOTE this is added so the compiler doesnt generate CmdQueueSyncer destructor inline. If it does, the compiler
// will also need the WorkWaiterPtr destructor and therefore the WorkWaiter destructor
CmdQueueSyncer::~CmdQueueSyncer() = default;

uint64_t CmdQueueSyncer::SignalWork()
{
    const uint64_t workId = m_nextWorkId++;

    auto workWaiter = CreateWorkWaiter(workId);
    assert(workWaiter);

    Utils::AssertIfFailed(m_cmdQueue->Signal(workWaiter->GetFence(), workId));

    return workId;
}

void CmdQueueSyncer::Wait(uint64_t workId)
{
    assert(m_workWaiters.size() > workId);

    auto& workWaiter = m_workWaiters[workId];
    auto fence = workWaiter->GetFence();
    assert(fence);
    auto event = workWaiter->GetEvent();
    assert(event);

    Utils::AssertIfFailed(fence->SetEventOnCompletion(workId, event));
    Utils::AssertIfFailed(WaitForSingleObject(event, INFINITE), WAIT_FAILED);
}

CmdQueueSyncer::WorkWaiter* CmdQueueSyncer::CreateWorkWaiter(uint64_t workId)
{
    auto workWaiter = std::make_unique<WorkWaiter>(m_device, workId);
    assert(workWaiter);
    m_workWaiters.push_back(std::move(workWaiter));

    return m_workWaiters.back().get();
}