#pragma once
#include "../path.hpp"
#include "drivers/basic.hpp"
#include "drivers/tmp.hpp"

namespace fs {
enum class OpenMode {
    Read,
    Write,
};

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

    auto find(const std::string_view name) -> Result<OpenInfo> {
        return data->find(name);
    }

    auto mkdir(const std::string_view name) -> Error {
        if(!is_write_opened()) {
            return Error::Code::FileNotOpened;
        }
        return data->mkdir(name);
    }

    auto readdir(const size_t index) -> Result<OpenInfo> {
        return data->readdir(index);
    }

    auto remove(const std::string_view name) -> Error {
        return data->remove(name);
    }

    Handle(OpenInfo* const data, const OpenMode mode) : data(data), mode(mode) {}
};

class Controller {
  private:
    struct MountData {
        std::string path;
        Handle      handle;
    };

    basic::Driver          basic_driver;
    OpenInfo&              root;
    std::vector<MountData> mount_list;

    auto follow_mountpoints(fs::OpenInfo* info) -> fs::OpenInfo* {
        while(info->mount != nullptr) {
            info = info->mount;
        }
        return info;
    }

  public:
    auto open(const std::string_view path, const OpenMode mode) -> Result<Handle> {
        auto elms                = split_path(path);
        auto info                = follow_mountpoints(&root);
        auto existing_info_stack = std::vector<OpenInfo*>{info};
        auto created_info_stack  = std::vector<OpenInfo>();
        for(auto e = elms.begin(); e != elms.end(); e += 1) {
            auto& children = info->children;
            if(const auto p = children.find(std::string(*e)); p != children.end()) {
                info = follow_mountpoints(&p->second);
                existing_info_stack.emplace_back(info);
                continue;
            }
            auto find_result = info->find(*e);
            if(!find_result) {
                return find_result.as_error();
            }
            auto& p = created_info_stack.emplace_back(find_result.as_value());
            info    = &p; // not follow_mountpoints(&p), since find_result is created here and should not be a mountpoint.
        }

        // the file is already write opened
        if(info->write_count >= 1) {
            return Error::Code::FileOpened;
        }

        // cannot modify read opened file
        if(info->read_count >= 1 && mode != OpenMode::Read) {
            return Error::Code::FileOpened;
        }

        // create node
        auto last = existing_info_stack.back();
        for(auto i = 0; i < created_info_stack.size(); i += 1) {
            last = &last->children.emplace(created_info_stack[i].name, created_info_stack[i]).first->second;
            if(i != created_info_stack.size() - 1) {
                last->child_count += 1;
            }
        }
        // also update counts of existing nodes
        for(auto i : existing_info_stack) {
            if(&(*i) != last) {
                i->child_count += 1;
            }
        }

        switch(mode) {
        case OpenMode::Read:
            last->read_count += 1;
            break;
        case OpenMode::Write:
            last->write_count += 1;
        }

        return Handle(last, mode);
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
        node->child_count += 1; // prevents the child_count of the trailing node from being decremented
        while(node != nullptr) {
            node->child_count -= 1;
            auto parent = node->parent;
            if(!node->is_busy() && !node->is_volume_root() && parent != nullptr) {
                parent->children.erase(node->name);
            }
            node = parent;
        }
        return Error();
    }

    auto mount(const std::string_view path, Driver& driver) -> Error {
        auto volume_root = &driver.get_root();
        if(volume_root->parent != nullptr) {
            return Error::Code::VolumeMounted;
        }

        auto open_result = open(path, OpenMode::Write);
        if(!open_result) {
            return open_result.as_error();
        }
        const auto handle   = open_result.as_value();
        handle.data->mount  = volume_root;
        volume_root->parent = handle.data->parent;
        mount_list.emplace_back(MountData{std::string(path), handle});
        return Error();
    }

    auto unmount(const std::string_view path) -> Error {
        for(auto i = mount_list.rbegin(); i != mount_list.rend(); i += 1) {
            if(i->path != path) {
                continue;
            }
            const auto mount_point = i->handle.data;
            const auto volume_root = mount_point->mount;
            if(volume_root->is_busy()) {
                return Error::Code::VolumeBusy;
            }
            mount_point->mount  = nullptr;
            volume_root->parent = nullptr;

            auto e = Error();
            if(e = close(i->handle); e) {
                logger(LogLevel::Error, "failed to close mountpoint, this is kernel bug: %d\n", e.as_int());
            }
            mount_list.erase(std::next(i).base());
            return e;
        }
        return Error::Code::NotMounted;
    }

    auto _compare_root(const OpenInfo::Testdata& data) -> bool {
        return root.test_compare(data);
    }

    Controller() : root(basic_driver.get_root()) {}
};
} // namespace fs
