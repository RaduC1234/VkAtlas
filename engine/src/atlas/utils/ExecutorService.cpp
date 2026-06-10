#include "ExecutorService.hpp"

#include <string>

#include "core/Profiler.hpp"

namespace Atlas {
    ExecutorService::ExecutorService(size_t numThreads) {
        ATLAS_PROFILE_FUNCTION();

        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) {
                numThreads = 4;
            }
        }

        workers.reserve(numThreads);

        for (size_t i = 0; i < numThreads; ++i) {
            std::string threadName = "Atlas Worker " + std::to_string(i);
            workers.emplace_back([this, threadName] {
                ATLAS_PROFILE_THREAD(threadName.c_str());

                while (true) {
                    std::function<void()> task; {
                        ATLAS_PROFILE_SCOPE("ExecutorService::wait");
                        std::unique_lock<std::mutex> lock(queueMutex);

                        condition.wait(lock, [this] {
                            return stop || !tasks.empty();
                        });

                        if (stop && tasks.empty()) {
                            return;
                        }

                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    ATLAS_PROFILE_SCOPE("ExecutorService::task");
                    task(); // Execute task outside of lock
                }
            });
        }
    }

    ExecutorService::~ExecutorService() {
        ATLAS_PROFILE_FUNCTION();

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();

        for (std::thread &worker: workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    size_t ExecutorService::getThreadCount() const {
        return workers.size();
    }

    size_t ExecutorService::getQueuedTaskCount() const {
        std::unique_lock<std::mutex> lock(queueMutex);
        return tasks.size();
    }
}
