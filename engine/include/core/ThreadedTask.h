#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>

template <class Result> class ThreadedTask {
public:
    ThreadedTask() = default;
    ThreadedTask(std::future<Result> future, std::thread worker)
        : future_(std::move(future)), worker_(std::move(worker)) {}

    ThreadedTask(const ThreadedTask&) = delete;
    ThreadedTask& operator=(const ThreadedTask&) = delete;
    ThreadedTask(ThreadedTask&&) noexcept = default;
    ThreadedTask& operator=(ThreadedTask&&) noexcept = default;

    template <class Callable> static ThreadedTask Start(Callable&& callable) {
        auto packagedTask =
            std::make_shared<std::packaged_task<Result()>>(std::forward<Callable>(callable));
        std::future<Result> future = packagedTask->get_future();
        std::thread worker([packagedTask]() { (*packagedTask)(); });
        return ThreadedTask(std::move(future), std::move(worker));
    }

    bool IsValid() const {
        return future_.valid();
    }

    bool IsReady() const {
        return future_.valid() &&
               future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    bool IsRunning() const {
        return future_.valid() &&
               future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }

    Result Get() {
        return future_.get();
    }

    void JoinWorker() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void DetachWorker() noexcept {
        if (worker_.joinable()) {
            worker_.detach();
        }
    }

private:
    std::future<Result> future_{};
    std::thread worker_{};
};
