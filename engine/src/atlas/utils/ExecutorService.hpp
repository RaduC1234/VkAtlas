#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>

namespace Atlas {
    class ExecutorService {
    public:
        /**
         * @brief Constructs an executor service with the specified number of threads
         * @param numThreads Number of worker threads (0 = auto-detect based on hardware)
         */
        explicit ExecutorService(size_t numThreads = 0);

        /**
         * @brief Destructor - stops all threads and waits for them to finish
         */
        ~ExecutorService();

        /**
         * @brief Submit a task to the executor service
         * @param f Function to execute
         * @param args Arguments to pass to the function
         * @return Future that will contain the result of the function
         */
        template<typename F, typename... Args>
        auto submit(F &&f, Args &&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

        /**
         * @brief Get the number of worker threads in the pool
         * @return Number of threads
         */
        size_t getThreadCount() const;

        /**
         * @brief Get the number of pending tasks in the queue
         * @return Number of queued tasks
         */
        size_t getQueuedTaskCount() const;

        // Delete copy operations
        ExecutorService(const ExecutorService &) = delete;
        ExecutorService &operator=(const ExecutorService &) = delete;

        // Allow move operations
        ExecutorService(ExecutorService &&) noexcept = default;
        ExecutorService &operator=(ExecutorService &&) noexcept = default;

    private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()> > tasks;

        mutable std::mutex queueMutex;
        std::condition_variable condition;
        bool stop = false;
    };

    template<typename F, typename... Args>
    auto ExecutorService::submit(F &&f, Args &&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using ReturnType = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<ReturnType()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<ReturnType> result = task->get_future(); {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop) {
                throw std::runtime_error("Cannot submit task to stopped ExecutorService");
            }

            tasks.emplace([task]() { (*task)(); });
        }

        condition.notify_one();
        return result;
    }
}
