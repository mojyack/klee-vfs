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

    OpenInfo& root;

    auto follow_mountpoints(fs::OpenInfo* info) -> fs::OpenInfo* {
        while(info->mount != nullptr) {
            info = info->mount;
        }
        return info;
    }

  public:
    auto open(const std::string_view path, const OpenMode mode) -> Result<OpenInfo*> {
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

        return last;
    }

    auto mount(const std::string_view path, Driver& driver) {
        const auto elms            = split_path(path);
        auto       open_info_stack = std::vector<OpenInfo>();
    }

    auto _root_mount(Driver& driver) {
        root.mount = &driver.get_root();
    }

    auto _compare_root(const OpenInfo::Testdata& data) -> bool {
        return root.test_compare(data);
    }

    Controller() : root(basic_driver.get_root()) {}
};
} // namespace fs
