#include "ExecutorService.hpp"

namespace Atlas {
    ExecutorService::ExecutorService(size_t numThreads) {
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) {
                numThreads = 4;
            }
        }

        workers.reserve(numThreads);

        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task; {
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

                    task(); // Execute task outside of lock
                }
            });
        }
    }

    ExecutorService::~ExecutorService() { {
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
