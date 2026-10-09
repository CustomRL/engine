#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {

// A fixed pool of worker threads. parallel_for hands out item indices one at a time from a shared counter, so uneven
// items balance themselves; the calling thread waits and does not run items.
class JobPool {
public:
    // threads == 0 uses every logical core
    explicit JobPool(unsigned threads = 0);
    ~JobPool();
    JobPool(const JobPool&) = delete;
    JobPool& operator=(const JobPool&) = delete;

    unsigned size() const { return unsigned(workers_.size()); }

    // Runs fn(item, worker) for every item in [0, count) and returns when all are done. worker is in [0, size()).
    void parallel_for(size_t count, const std::function<void(size_t item, unsigned worker)>& fn);

private:
    void run(unsigned worker);

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(size_t, unsigned)>* fn_ = nullptr;
    size_t count_ = 0;
    std::atomic<size_t> next_{0};
    unsigned busy_ = 0;
    unsigned long long generation_ = 0;
    bool quit_ = false;
};

}
