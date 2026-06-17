#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

class FrameRollbackRegistry {
public:
    bool Add(const void* owner, std::function<void()> rollback) {
        if (!rollback) {
            return true;
        }
        if (entries_.size() == entries_.max_size()) {
            return false;
        }
        try {
            entries_.push_back({owner, std::move(rollback)});
        } catch (...) {
            return false;
        }
        return true;
    }

    bool ReserveAdditional(size_t additional) {
        if (additional > entries_.max_size() - entries_.size()) {
            return false;
        }
        try {
            entries_.reserve(entries_.size() + additional);
        } catch (...) {
            return false;
        }
        return true;
    }

    void RemoveOwner(const void* owner) noexcept {
        if (owner == nullptr) {
            return;
        }
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [owner](const Entry& entry) { return entry.owner == owner; }),
                       entries_.end());
    }

    void Restore() noexcept {
        std::vector<Entry> entries = std::move(entries_);
        entries_.clear();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->rollback) {
                try {
                    it->rollback();
                } catch (...) {
                }
            }
        }
    }

    void Clear() noexcept {
        entries_.clear();
    }

    bool Empty() const noexcept {
        return entries_.empty();
    }

private:
    struct Entry {
        const void* owner = nullptr;
        std::function<void()> rollback;
    };

    std::vector<Entry> entries_;
};
