#pragma once
#include "../path.hpp"
#include "drivers/basic.hpp"
#include "drivers/tmp.hpp"

namespace fs {
enum class OpenMode {
    Read,
    Write,
};

class Controller {
  private:
    basic::Driver basic_driver;

    OpenInfo root;

    auto follow_mountpoints(fs::OpenInfo* info) -> fs::OpenInfo* {
        while(info->mount != nullptr) {
            info = info->mount;
        }
        return info;
    }

  public:
    auto open(const std::string_view path, const OpenMode mode) -> Result<int> {
        auto elms                = split_path(path);
        auto info                = follow_mountpoints(&root);
        auto existing_info_stack = std::vector<OpenInfo*>{info};
        auto created_info_stack  = std::vector<OpenInfo>();
        for(auto e = elms.begin(); e != elms.end(); e += 1) {
            auto& children = info->children;
            if(const auto p = children.find(std::string(*e)); p != children.end()) {
                info = &p->second;
                existing_info_stack.emplace_back(info);
                continue;
            }
            auto find_result = info->driver->find(info->driver_data, *e);
            if(!find_result) {
                return find_result.as_error();
            }
            auto& p = created_info_stack.emplace_back(find_result.as_value());
            info    = &p; // not follow_mountpoints(&p), since find_result is created here and should not be a mountpoint.
        }

        if(info->write_count >= 1) {
            return Error::Code::FileOpened;
        }

        for(auto i = 0; i < elms.size() - 1; i += 1) {
            if(i < existing_info_stack.size()) {
                existing_info_stack[i]->child_count += 1;
            }
        }
        for(auto e = elms.begin(); e != elms.end(); e += 1) {
            auto& children = info->children;
            if(const auto p = children.find(std::string(*e)); p != children.end()) {
                info = &p->second;
                continue;
            }
        }
    }

    auto mount(const std::string_view path, Driver& driver) {
        const auto elms            = split_path(path);
        auto       open_info_stack = std::vector<OpenInfo>();
    }

    Controller() : root(basic_driver.get_root()) {}
};
} // namespace fs
