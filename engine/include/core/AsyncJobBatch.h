#pragma once

#include <array>
#include <cstddef>
#include <future>
#include <utility>

template <class Result, size_t Count> class AsyncJobBatch {
public:
    template <class Callable> void Start(size_t index, Callable&& callable) {
        futures_[index] = std::async(std::launch::async, std::forward<Callable>(callable));
    }

    Result Get(size_t index) {
        return futures_[index].get();
    }

private:
    std::array<std::future<Result>, Count> futures_{};
};
