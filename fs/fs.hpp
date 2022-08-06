#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../error.hpp"

namespace fs {
class Driver;

struct OpenInfo {
    std::string name;
    uint32_t    read_count  = 0;
    uint32_t    write_count = 0;
    uint32_t    child_count = 0;
    Driver*     driver;
    uintptr_t   driver_data;
    OpenInfo*   mount = nullptr;

    std::unordered_map<std::string, OpenInfo> children;

    OpenInfo(std::string_view name, Driver& driver, const auto driver_data) : name(name),
                                                                              driver(&driver),
                                                                              driver_data(reinterpret_cast<uintptr_t>(driver_data)) {}
};

class Driver {
  public:
    virtual auto read(uintptr_t data, size_t offset, size_t size, void* buffer) -> Error        = 0;
    virtual auto write(uintptr_t data, size_t offset, size_t size, const void* buffer) -> Error = 0;

    virtual auto find(uintptr_t data, std::string_view name) -> Result<OpenInfo> = 0;
    virtual auto mkdir(uintptr_t data, std::string_view name) -> Error           = 0;
    virtual auto readdir(uintptr_t data, size_t index) -> Result<OpenInfo>       = 0;

    virtual auto get_root() -> OpenInfo& = 0;

    virtual ~Driver() = default;
};
} // namespace fs
