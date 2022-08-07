#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../error.hpp"
#include "../log.hpp"

namespace fs {
class Driver;

class OpenInfo {
  private:
    Driver*   driver;
    uintptr_t driver_data;

    auto check_opened(const bool write) -> bool {
        if((write && write_count == 0) || (!write && read_count == 0 && write_count == 0)) {
            logger(LogLevel::Error, "file \"%s\" is not %s opened\n", name.data(), write ? "write" : "read");
            return false;
        }
        return true;
    }

  public:
    std::string name;
    uint32_t    read_count  = 0;
    uint32_t    write_count = 0;
    uint32_t    child_count = 0;
    OpenInfo*   parent;
    OpenInfo*   mount = nullptr;

    std::unordered_map<std::string, OpenInfo> children;

    auto read(size_t offset, size_t size, void* buffer) -> Error;
    auto write(size_t offset, size_t size, const void* buffer) -> Error;
    auto find(std::string_view name) -> Result<OpenInfo>;
    auto mkdir(std::string_view name) -> Error;
    auto readdir(size_t index) -> Result<OpenInfo>;

    // test stuff
    struct Testdata {
        std::string                               name;
        uint32_t                                  read_count  = 0;
        uint32_t                                  write_count = 0;
        uint32_t                                  child_count = 0;
        std::unordered_map<std::string, Testdata> children;
    };

    auto test_compare(const Testdata& data) const -> bool {
        if(mount != nullptr) {
            return mount->test_compare(data);
        }

        if(name != data.name || read_count != data.read_count || write_count != data.write_count || child_count != data.child_count) {
            return false;
        }
        if(children.size() != data.children.size()) {
            return false;
        }
        for(auto [k, v] : children) {
            if(auto p = data.children.find(k); p == data.children.end()) {
                return false;
            } else {
                v.test_compare(p->second);
            }
        }
        auto i1 = children.begin();
        auto i2 = data.children.begin();
        while(i1 != children.end()) {
            if(!i1->second.test_compare(i2->second)) {
                return false;
            }
            i1 = std::next(i1, 1);
            i2 = std::next(i2, 1);
        }
        return true;
    }

    OpenInfo(const std::string_view name, Driver& driver, const auto driver_data) : driver(&driver),
                                                                                    driver_data(reinterpret_cast<uintptr_t>(driver_data)),
                                                                                    name(name) {}
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

inline auto OpenInfo::read(const size_t offset, const size_t size, void* const buffer) -> Error {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }
    return driver->read(driver_data, offset, size, buffer);
}

inline auto OpenInfo::write(const size_t offset, const size_t size, const void* const buffer) -> Error {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }
    return driver->write(driver_data, offset, size, buffer);
}

inline auto OpenInfo::find(const std::string_view name) -> Result<OpenInfo> {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }

    auto r = driver->find(driver_data, name);
    if(r) {
        r.as_value().parent = this;
    }
    return r;
}

inline auto OpenInfo::mkdir(const std::string_view name) -> Error {
    if(!check_opened(true)) {
        return Error::Code::FileNotOpened;
    }
    return driver->mkdir(driver_data, name);
}

inline auto OpenInfo::readdir(const size_t index) -> Result<OpenInfo> {
    if(!check_opened(false)) {
        return Error::Code::FileNotOpened;
    }

    auto r = driver->readdir(driver_data, index);
    if(r) {
        r.as_value().parent = this;
    }
    return r;
}
} // namespace fs
