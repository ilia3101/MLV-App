#ifndef AVIRTHREADPOOL_H
#define AVIRTHREADPOOL_H

#include "ThreadPool.h"
#include "avir.h"

using thread_pool_base = ThreadPool;
class avir_scale_thread_pool : public avir::CImageResizerThreadPool, public thread_pool_base
{
public:
    virtual int getSuggestedWorkloadCount() const override
    {
        return thread_pool_base::size();
    }

    virtual void addWorkload(CWorkload *const workload) override
    {
        _workloads.emplace_back(workload);
    }

    virtual void startAllWorkloads() override
    {
        for (auto &workload : _workloads) _tasks.emplace_back(thread_pool_base::enqueue([](auto workload){ workload->process(); }, workload));
    }

    virtual void waitAllWorkloadsToFinish() override
    {
        for (auto &task : _tasks) task.wait();
    }

    virtual void removeAllWorkloads() override
    {
        _tasks.clear();
        _workloads.clear();
    }

private:
    std::deque<std::future<void>> _tasks;
    std::deque<CWorkload*> _workloads;
};

#endif // AVIRTHREADPOOL_H
