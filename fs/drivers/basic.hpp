#pragma once
#include "../fs.hpp"

namespace fs::basic {
class Driver : public fs::Driver {
  private:
    OpenInfo root;

  public:
    auto read(uintptr_t data, size_t offset, size_t size, void* buffer) -> Error override {
        return Error::Code::InvalidData;
    }

    auto write(uintptr_t data, size_t offset, size_t size, const void* buffer) -> Error override {
        return Error::Code::InvalidData;
    }

    auto find(const uintptr_t data, const std::string_view name) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto create(const uintptr_t data, const std::string_view name, const FileType type) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto readdir(const uintptr_t data, const size_t index) -> Result<OpenInfo> override {
        return Error::Code::InvalidData;
    }

    auto remove(const uintptr_t data, const std::string_view name) -> Error override {
        return Error::Code::InvalidData;
    }

    auto get_root() -> OpenInfo& override {
        return root;
    }

    Driver() : root("/", *this, nullptr, true) {}
};
} // namespace fs::basic
