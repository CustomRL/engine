#include "engine/core/jobs.h"

namespace engine {

JobPool::JobPool(unsigned threads) {
    if (threads == 0)
        threads = std::thread::hardware_concurrency();
    if (threads == 0)
        threads = 1;
    for (unsigned i = 0; i < threads; i++)
        workers_.emplace_back([this, i] { run(i); });
}

JobPool::~JobPool() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_)
        t.join();
}

void JobPool::parallel_for(size_t count, const std::function<void(size_t, unsigned)>& fn) {
    if (count == 0)
        return;
    std::unique_lock lock(mutex_);
    fn_ = &fn;
    count_ = count;
    next_.store(0, std::memory_order_relaxed);
    busy_ = size();
    generation_++;
    wake_.notify_all();
    done_.wait(lock, [this] { return busy_ == 0; });
    fn_ = nullptr;
}

void JobPool::run(unsigned worker) {
    unsigned long long seen = 0;
    for (;;) {
        const std::function<void(size_t, unsigned)>* fn;
        size_t count;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return quit_ || generation_ != seen; });
            if (quit_)
                return;
            seen = generation_;
            fn = fn_;
            count = count_;
        }
        for (size_t i; (i = next_.fetch_add(1, std::memory_order_relaxed)) < count;)
            (*fn)(i, worker);
        std::lock_guard lock(mutex_);
        if (--busy_ == 0)
            done_.notify_one();
    }
}

}
