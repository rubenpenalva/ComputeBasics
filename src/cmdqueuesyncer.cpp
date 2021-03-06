#include "CmdQueueSyncer.h"

#include "utils.h"

class CmdQueueSyncer::WorkWaiter
{
public:
    WorkWaiter(ID3D12Device* device);
    ~WorkWaiter();

    ID3D12Fence* GetFence() const { return m_fence.Get(); }
    HANDLE GetEvent() const { return m_event; }

private:
    using ID3D12FenceComPtr = Microsoft::WRL::ComPtr<ID3D12Fence>;

    ID3D12FenceComPtr   m_fence;
    HANDLE              m_event;
};

CmdQueueSyncer::WorkWaiter::WorkWaiter(ID3D12Device* device)
{
    assert(device);

    Utils::AssertIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    assert(m_fence);

    m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_event);
}

CmdQueueSyncer::WorkWaiter::~WorkWaiter()
{
    CloseHandle(m_event);
}

CmdQueueSyncer::CmdQueueSyncer(ID3D12Device* device, 
                               ID3D12CommandQueue* cmdQueue) : m_device(device), m_cmdQueue(cmdQueue)
{
    assert(m_device);
    assert(m_cmdQueue);
}

// NOTE this is added so the compiler doesnt generate CmdQueueSyncer destructor inline. If it does, the compiler
// will also need the WorkWaiterPtr destructor and therefore the WorkWaiter destructor
CmdQueueSyncer::~CmdQueueSyncer() = default;

uint64_t CmdQueueSyncer::SignalWork()
{
    auto workWaiter = CreateWorkWaiter();
    assert(workWaiter);

    const uint64_t workId = CurrentWorkId();
    Utils::AssertIfFailed(m_cmdQueue->Signal(workWaiter->GetFence(), workId));

    return workId;
}

void CmdQueueSyncer::Wait(uint64_t workId)
{
    assert(workId != 0);
    const size_t workWaitersIndex = workId - 1;
    assert(m_workWaiters.size() > workWaitersIndex);

    auto& workWaiter = m_workWaiters[workWaitersIndex];
    auto fence = workWaiter->GetFence();
    assert(fence);
    auto event = workWaiter->GetEvent();
    assert(event);

    Utils::AssertIfFailed(fence->SetEventOnCompletion(workId, event));
    Utils::AssertIfFailed(WaitForSingleObject(event, INFINITE), WAIT_FAILED);
}

CmdQueueSyncer::WorkWaiter* CmdQueueSyncer::CreateWorkWaiter()
{
    auto workWaiter = std::make_unique<WorkWaiter>(m_device);
    assert(workWaiter);
    m_workWaiters.push_back(std::move(workWaiter));

    return m_workWaiters.back().get();
}

uint64_t CmdQueueSyncer::CurrentWorkId()
{
    assert(m_workWaiters.size());

    return m_workWaiters.size();
}