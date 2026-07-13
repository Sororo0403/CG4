#pragma once
#include <cstdint>
#include <limits>

inline constexpr uint32_t kInvalidResourceId = (std::numeric_limits<uint32_t>::max)();

[[nodiscard]] constexpr bool IsValidResourceId(uint32_t id) noexcept {
    return id != kInvalidResourceId;
}

template <typename Tag> class ResourceHandle {
public:
    static constexpr uint32_t kInvalidIndex = kInvalidResourceId;

    /// <summary>
    /// ResourceHandleを実行する
    /// </summary>
    constexpr ResourceHandle() = default;
    explicit constexpr ResourceHandle(uint32_t index) : index_(index) {}

    [[nodiscard]] constexpr bool IsValid() const {
        return index_ != kInvalidIndex;
    }
    [[nodiscard]] constexpr uint32_t Get() const {
        return index_;
    }

    explicit constexpr operator bool() const {
        return IsValid();
    }

    friend constexpr bool operator==(ResourceHandle lhs, ResourceHandle rhs) {
        return lhs.index_ == rhs.index_;
    }

    friend constexpr bool operator!=(ResourceHandle lhs, ResourceHandle rhs) {
        return !(lhs == rhs);
    }

private:
    uint32_t index_ = kInvalidIndex;
};

template <typename Tag> constexpr ResourceHandle<Tag> MakeResourceHandle(uint32_t id) noexcept {
    return ResourceHandle<Tag>(id);
}

template <typename Tag> constexpr uint32_t ToResourceId(ResourceHandle<Tag> handle) noexcept {
    return handle.Get();
}

struct TextureHandleTag;
struct MeshHandleTag;
struct MaterialHandleTag;
struct ModelHandleTag;
struct DescriptorHandleTag;
struct SoundHandleTag;
struct VoiceHandleTag;
struct FontHandleTag;

using TextureHandle = ResourceHandle<TextureHandleTag>;
using MeshHandle = ResourceHandle<MeshHandleTag>;
using MaterialHandle = ResourceHandle<MaterialHandleTag>;
using ModelHandle = ResourceHandle<ModelHandleTag>;
using DescriptorHandle = ResourceHandle<DescriptorHandleTag>;
using SoundHandle = ResourceHandle<SoundHandleTag>;
using VoiceHandle = ResourceHandle<VoiceHandleTag>;
using FontHandle = ResourceHandle<FontHandleTag>;
