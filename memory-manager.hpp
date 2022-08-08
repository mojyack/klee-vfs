#pragma once
#include <array>
#include <limits>

#include "error.hpp"

constexpr auto operator""_KiB(const unsigned long long kib) -> unsigned long long {
    return kib * 1024;
}

constexpr auto operator""_MiB(const unsigned long long mib) -> unsigned long long {
    return mib * 1024_KiB;
}

constexpr auto operator""_GiB(const unsigned long long gib) -> unsigned long long {
    return gib * 1024_MiB;
}

static constexpr auto bytes_per_frame = 4_KiB;

class FrameID {
  private:
    void* id;

  public:
    auto set_frame(void* const new_id) -> void {
        id = new_id;
    }

    auto get_frame() const -> void* {
        return reinterpret_cast<void*>(id);
    }

    explicit FrameID(void* const id) : id(id) {}
};

class BitmapMemoryManager {
  public:
    auto allocate(const size_t frames) -> Result<FrameID> {
        return FrameID(static_cast<void*>(new uint8_t[bytes_per_frame * frames]));
    }

    auto deallocate(const FrameID begin, const size_t frames) -> Error {
        delete[] static_cast<uint8_t*>(begin.get_frame());
        return Error();
    }
};

inline auto allocator = (BitmapMemoryManager*)(nullptr);

class SmartFrameID {
  private:
    FrameID id = FrameID(nullptr);
    size_t  frames;

  public:
    auto operator=(SmartFrameID&& o) -> SmartFrameID& {
        if(id.get_frame() != nullptr) {
            allocator->deallocate(id, frames);
        }

        id     = o.id;
        frames = o.frames;
        o.id.set_frame(nullptr);
        return *this;
    }

    auto operator->() -> FrameID* {
        return &id;
    }

    auto operator*() -> FrameID {
        return id;
    }

    SmartFrameID(SmartFrameID&& o) {
        *this = std::move(o);
    }

    SmartFrameID() = default;
    SmartFrameID(const FrameID id, const size_t frames) : id(id), frames(frames) {}
    ~SmartFrameID() {
        if(id.get_frame() != nullptr) {
            allocator->deallocate(id, frames);
        }
    }
};
