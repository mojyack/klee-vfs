#pragma once
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "../fs.hpp"

namespace fs::tmp {
class Object {
  private:
    std::string name;

  protected:
    Object(std::string name) : name(std::move(name)) {}

  public:
    auto get_name() const -> const std::string& {
        return name;
    }
};

class File : public Object {
  private:
    std::vector<uint8_t> data;

  public:
    File(std::string name) : Object(std::move(name)) {}
};

class Directory : public Object {
  private:
    std::unordered_map<std::string, std::variant<File, Directory>> children;

  public:
    auto find(const std::string_view name) const -> const std::variant<File, Directory>* {
        // TODO
        // children.find(name)
        // https://onihusube.hatenablog.com/entry/2021/12/17/002236
        const auto p = children.find(std::string(name));
        return p != children.end() ? &p->second : nullptr;
    }

    auto mkdir(const std::string_view name) -> void {
        children.emplace(std::string(name), Directory(std::string(name)));
    }

    auto remove(const std::string_view name) -> bool {
        return children.erase(std::string(name)) != 0;
    }

    auto find_nth(const size_t index) const -> Result<std::pair<const std::string&, const std::variant<File, Directory>&>> {
        if(index >= children.size()) {
            return Error::Code::IndexOutOfRange;
        }

        const auto i = std::next(children.begin(), index);
        return std::pair<const std::string&, const std::variant<File, Directory>&>{i->first, i->second};
    }

    Directory(std::string name) : Object(std::move(name)) {}
};

#define unwrap(var, func)              \
    auto result##var = func;           \
    if(!result##var) {                 \
        return result##var.as_error(); \
    }                                  \
    auto& var = result##var.as_value();

class Driver : public fs::Driver {
  private:
    std::variant<File, Directory> data;
    OpenInfo                      root;

    auto data_as_directory(const uintptr_t data) -> Result<Directory*> {
        auto& obj = *reinterpret_cast<std::variant<File, Directory>*>(data);
        if(!std::holds_alternative<Directory>(obj)) {
            return Error::Code::NotDirectory;
        }
        return &std::get<Directory>(obj);
    }

  public:
    auto read(const uintptr_t data, const size_t offset, const size_t size, void* const buffer) -> Error override {
        return Error::Code::InvalidData;
    }

    auto write(const uintptr_t data, const size_t offset, const size_t size, const void* const buffer) -> Error override {
        return Error::Code::InvalidData;
    }

    auto find(const uintptr_t data, const std::string_view name) -> Result<OpenInfo> override {
        unwrap(dir, data_as_directory(data));
        const auto p = dir->find(name);
        return p != nullptr ? Result(OpenInfo(name, *this, p)) : Error::Code::NoSuchFile;
    }

    auto mkdir(const uintptr_t data, const std::string_view name) -> Error override {
        unwrap(dir, data_as_directory(data));
        if(dir->find(name) != nullptr) {
            return Error::Code::FileExists;
        }
        dir->mkdir(name);
        return Error();
    }

    auto readdir(const uintptr_t data, const size_t index) -> Result<OpenInfo> override {
        unwrap(dir, data_as_directory(data));
        unwrap(child, dir->find_nth(index));
        return OpenInfo(child.first, *this, &child.second);
    }

    auto remove(const uintptr_t data, const std::string_view name) -> Error override {
        unwrap(dir, data_as_directory(data));
        if(!dir->remove(name)) {
            return Error::Code::NoSuchFile;
        }
        return Error();
    }

    auto get_root() -> OpenInfo& override {
        return root;
    }

    Driver() : data(Directory("/")),
               root("/", *this, &data, true) {}
};

#undef unwrap
} // namespace fs::tmp
