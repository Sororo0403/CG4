#pragma once

#include "core/ResourceHandle.h"

#include <DirectXTex.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct TextureManagerDecodedTexture {
    std::wstring pathKey;
    DirectX::ScratchImage scratch;
    DirectX::TexMetadata metadata{};
    bool succeeded = false;
};

struct TextureManagerAsyncJob {
    TextureManagerDecodedTexture decoded;
    std::atomic_bool ready = false;
};

struct TextureManagerAsyncRequest {
    uint32_t requestId = 0;
    std::wstring filePath;
    std::wstring pathKey;
    std::shared_ptr<TextureManagerAsyncJob> job;
    uint32_t textureId = kInvalidResourceId;
    bool queued = false;
    bool completed = false;
    bool failed = false;
};

struct TextureManagerAsyncTerminalState {
    uint32_t requestId = 0;
    std::optional<uint32_t> textureId;
    bool failed = false;
};

struct TextureManagerAsyncWorkItem {
    uint32_t requestId = 0;
    std::wstring filePath;
    std::shared_ptr<TextureManagerAsyncJob> job;
};

class TextureAsyncWorkerPool {
public:
    template <class WorkHandler> bool Start(size_t workerCount, WorkHandler workHandler) {
        if (!workers_.empty()) {
            return true;
        }

        stopWorkers_ = false;
        try {
            workers_.reserve(workerCount);
            for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
                workers_.emplace_back([this, workHandler]() mutable {
                    for (;;) {
                        TextureManagerAsyncWorkItem item{};
                        {
                            std::unique_lock<std::mutex> lock(workerMutex_);
                            workerCv_.wait(
                                lock, [this]() { return stopWorkers_ || !pendingJobs_.empty(); });
                            if (stopWorkers_ && pendingJobs_.empty()) {
                                return;
                            }
                            item = std::move(pendingJobs_.front());
                            pendingJobs_.pop_front();
                        }
                        workHandler(item);
                    }
                });
            }
        } catch (...) {
            Stop();
            return false;
        }
        return true;
    }

    bool IsRunning() const {
        return !workers_.empty();
    }

    bool Enqueue(TextureManagerAsyncWorkItem item) {
        try {
            {
                std::lock_guard<std::mutex> lock(workerMutex_);
                pendingJobs_.push_back(std::move(item));
            }
            workerCv_.notify_one();
            return true;
        } catch (...) {
            return false;
        }
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            stopWorkers_ = true;
        }
        workerCv_.notify_all();
        JoinWorkers();
        stopWorkers_ = false;
    }

    void ClearPending() {
        std::lock_guard<std::mutex> lock(workerMutex_);
        pendingJobs_.clear();
    }

private:
    void JoinWorkers() {
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

    std::deque<TextureManagerAsyncWorkItem> pendingJobs_;
    std::vector<std::thread> workers_;
    std::mutex workerMutex_;
    std::condition_variable workerCv_;
    bool stopWorkers_ = false;
};

struct TextureManagerAsyncState {
    std::vector<TextureManagerAsyncRequest> requests;
    std::deque<TextureManagerAsyncTerminalState> terminalStates;
    TextureAsyncWorkerPool workerPool;
    uint32_t nextRequestId = 1;

    void Reset() {
        StopWorkers();
        requests.clear();
        terminalStates.clear();
        workerPool.ClearPending();
        nextRequestId = 1;
    }

    void StopWorkers() {
        workerPool.Stop();
    }
};
