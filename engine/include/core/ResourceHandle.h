#pragma once
#include <cstdint>
#include <limits>

template <typename Tag> class ResourceHandle {
  public:
    static constexpr uint32_t kInvalidIndex =
        (std::numeric_limits<uint32_t>::max)();

    constexpr ResourceHandle() = default;
    explicit constexpr ResourceHandle(uint32_t index) : index_(index) {}

    constexpr bool IsValid() const { return index_ != kInvalidIndex; }
    constexpr uint32_t Get() const { return index_; }

    explicit constexpr operator bool() const { return IsValid(); }

    friend constexpr bool operator==(ResourceHandle lhs,
                                     ResourceHandle rhs) {
        return lhs.index_ == rhs.index_;
    }

    friend constexpr bool operator!=(ResourceHandle lhs,
                                     ResourceHandle rhs) {
        return !(lhs == rhs);
    }

  private:
    uint32_t index_ = kInvalidIndex;
};

struct TextureHandleTag;
struct MeshHandleTag;
struct MaterialHandleTag;
struct ModelHandleTag;
struct DescriptorHandleTag;

using TextureHandle = ResourceHandle<TextureHandleTag>;
using MeshHandle = ResourceHandle<MeshHandleTag>;
using MaterialHandle = ResourceHandle<MaterialHandleTag>;
using ModelHandle = ResourceHandle<ModelHandleTag>;
using DescriptorHandle = ResourceHandle<DescriptorHandleTag>;
