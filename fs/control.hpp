#pragma once
#include "../path.hpp"
#include "drivers/basic.hpp"
#include "drivers/tmp.hpp"

namespace fs {
enum class OpenMode {
    Read,
    Write,
};

inline auto follow_mountpoints(fs::OpenInfo* info) -> fs::OpenInfo* {
    while(info->mount != nullptr) {
        info = info->mount;
    }
    return info;
}

inline auto try_open(fs::OpenInfo* const info, const OpenMode mode) -> Error {
    // the file is already write opened
    if(info->write_count >= 1) {
        return Error::Code::FileOpened;
    }

    // cannot modify read opened file
    if(info->read_count >= 1 && mode != OpenMode::Read) {
        return Error::Code::FileOpened;
    }

    switch(mode) {
    case OpenMode::Read:
        info->read_count += 1;
        break;
    case OpenMode::Write:
        info->write_count += 1;
    }

    return Error();
}

class Controller;

class Handle {
    friend class Controller;

  private:
    OpenInfo* data;
    OpenMode  mode;

    auto is_write_opened() -> bool {
        if(mode != OpenMode::Write) {
            logger(LogLevel::Error, "attempt to write to ro opened file \"%s\"\n", data->name.data());
            return false;
        }
        return true;
    }

  public:
    auto read(const size_t offset, const size_t size, void* const buffer) -> Error {
        return data->read(offset, size, buffer);
    }

    auto write(const size_t offset, const size_t size, const void* const buffer) -> Error {
        if(!is_write_opened()) {
            return Error::Code::FileNotOpened;
        }
        return data->write(offset, size, buffer);
    }

    auto open(const std::string_view name, const OpenMode mode) -> Result<Handle> {
        auto& children     = data->children;
        auto  created_info = std::optional<OpenInfo>();
        auto  result       = (OpenInfo*)(nullptr);
        if(const auto p = children.find(std::string(name)); p != children.end()) {
            result = follow_mountpoints(&p->second);
        } else {
            auto find_result = data->find(name);
            if(!find_result) {
                return find_result.as_error();
            }
            result = &created_info.emplace(find_result.as_value());
        }

        if(const auto e = try_open(result, mode)) {
            return e;
        }

        if(created_info) {
            auto& v = created_info.value();
            result  = &children.emplace(v.name, v).first->second;
        }

        return Handle(result, mode);
    }

    auto find(const std::string_view name) -> Result<OpenInfo> {
        return data->find(name);
    }

    auto create(const std::string_view name, const FileType type) -> Error {
        if(!is_write_opened()) {
            return Error::Code::FileNotOpened;
        }
        const auto r = data->create(name, type);
        return r ? Error() : r.as_error();
    }

    auto readdir(const size_t index) -> Result<OpenInfo> {
        return data->readdir(index);
    }

    auto remove(const std::string_view name) -> Error {
        return data->remove(name);
    }

    auto get_size() const -> size_t {
        return data->size;
    }

    Handle(OpenInfo* const data, const OpenMode mode) : data(data), mode(mode) {}
};

class Controller {
  private:
    basic::Driver       basic_driver;
    OpenInfo&           root;
    std::vector<Handle> mountpoints;

    auto open_root(const OpenMode mode) -> Result<Handle> {
        auto info = follow_mountpoints(&root);
        if(const auto e = try_open(info, mode)) {
            return e;
        }
        return Handle(info, mode);
    }

    static auto find_top_mountpoint(OpenInfo* node) -> Result<OpenInfo*> {
        if(node->mount == nullptr) {
            return Error::Code::NotMounted;
        }
        while(node->mount->mount != nullptr) {
            node = node->mount;
        }
        return node;
    }

    auto open_parent_directory(std::vector<std::string_view>& elms, const OpenMode mode) -> Result<Handle> {
        if(elms.empty()) {
            return open_root(mode);
        }

        auto dirname = std::span<std::string_view>(elms.begin(), elms.size() - 1);
        auto result  = open_root(OpenMode::Read);
        if(!result) {
            return result.as_error();
        }

        for(const auto& d : dirname) {
            auto handle = result.as_value();
            result      = handle.open(d, OpenMode::Read);
            close(handle);
            if(!result) {
                return result;
            }
        }
        return result;
    }

  public:
    auto open(const std::string_view path, const OpenMode mode) -> Result<Handle> {
        auto elms = split_path(path);
        if(elms.empty()) {
            return open_root(mode);
        }

        auto filename = elms.back();
        value_or(handle, open_parent_directory(elms, mode));

        auto result = handle.open(filename, mode);
        close(handle);
        return result;
    }

    auto close(Handle handle) -> Error {
        auto node = handle.data;
        switch(handle.mode) {
        case OpenMode::Read:
            node->read_count -= 1;
            break;
        case OpenMode::Write:
            node->write_count -= 1;
            break;
        }
        while(node->parent != nullptr) {
            if(node->is_busy() || node->is_volume_root()) {
                break;
            }

            if(node->parent == nullptr) {
                // bug
                // this node should be a volume root
                break;
            }

            node->parent->children.erase(node->name);
            node = node->parent;
        }
        return Error();
    }

    auto mount(const std::string_view path, Driver& driver) -> Error {
        auto volume_root = &driver.get_root();

        auto open_result = open(path, OpenMode::Write);
        if(!open_result) {
            return open_result.as_error();
        }
        const auto handle  = open_result.as_value();
        handle.data->mount = volume_root;
        mountpoints.emplace_back(handle);
        return Error();
    }

    auto unmount(const std::string_view path) -> Result<const Driver*> {
        auto mountpoint = (OpenInfo*)(nullptr);
        auto elms       = split_path(path);
        if(elms.empty()) {
            value_or(node, find_top_mountpoint(&root));
            mountpoint = node;
        } else {
            value_or(parent, open_parent_directory(elms, OpenMode::Read));
            error_or(close(parent));
            auto& children = parent.data->children;
            if(const auto p = children.find(std::string(elms.back())); p == children.end()) {
                return Error::Code::NoSuchFile;
            } else {
                value_or(node, find_top_mountpoint(&p->second));
                mountpoint = node;
            }
        }

        for(auto m = mountpoints.begin(); m != mountpoints.end(); m += 1) {
            if(m->data != mountpoint) {
                continue;
            }

            const auto volume_root = mountpoint->mount;
            if(volume_root->is_busy()) {
                return Error::Code::VolumeBusy;
            }
            mountpoint->mount = nullptr;

            if(const auto e = close(*m)) {
                logger(LogLevel::Error, "failed to close mountpoint, this is kernel bug: %d\n", e.as_int());
                mountpoints.erase(m);
                return e;
            }
            mountpoints.erase(m);
            return volume_root->read_driver();
        }
        return Error::Code::NotMounted;
    }

    auto _compare_root(const OpenInfo::Testdata& data) -> bool {
        return root.test_compare(data);
    }

    Controller() : root(basic_driver.get_root()) {}
};
} // namespace fs
