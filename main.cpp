
#include "block/drivers/dummy.hpp"
#include "block/gpt.hpp"
#include "fs/drivers/basic.hpp"
#include "fs/drivers/tmp.hpp"
#include "path.hpp"
#include "util.hpp"

using namespace std::literals;

auto follow_mountpoints(fs::OpenInfo* info) -> fs::OpenInfo* {
    while(info->mount != nullptr) {
        info = info->mount;
    }
    return info;
}

auto prompt(fs::OpenInfo& root) -> void {
    while(true) {
        const auto args = split_args(read_line("> "));
        if(args.empty()) {
            continue;
        }
        if(args[0] == "exit") {
            break;
        } else if(args[0] == "ls") {
            auto elms = split_path(args.size() >= 2 ? args[1] : "/");
            auto info = follow_mountpoints(&root);
            auto buf  = std::optional<fs::OpenInfo>();
            for(auto e = elms.begin(); e != elms.end(); e += 1) {
                auto& children = info->children;
                if(const auto p = children.find(std::string(*e)); p != children.end()) {
                    info = &p->second;
                    continue;
                }
                auto find_result = info->driver->find(info->driver_data, *e);
                if(!find_result) {
                    printf("path lookup error: %u\n", find_result.as_error().as_int());
                    goto next;
                }

                buf  = find_result.as_value();
                info = follow_mountpoints(&buf.value());
            }
            for(auto index = 0;; index += 1) {
                const auto r = info->driver->readdir(info->driver_data, index);
                if(!r) {
                    if(r.as_error() != Error::Code::IndexOutOfRange) {
                        printf("readdir error: %u\n", r.as_error().as_int());
                    }
                    break;
                }
                printf("%s\n", r.as_value().name.data());
            }
        } else if(args[0] == "mkdir") {
            if(args.size() < 2) {
                continue;
            }
            auto elms = split_path(args[1]);
            auto info = follow_mountpoints(&root);
            auto buf  = std::optional<fs::OpenInfo>();
            for(auto e = elms.begin(); e != elms.end() - 1; e += 1) {
                auto& children = info->children;
                if(const auto p = children.find(std::string(*e)); p != children.end()) {
                    info = &p->second;
                    continue;
                }
                auto find_result = info->driver->find(info->driver_data, *e);
                if(!find_result) {
                    printf("path lookup error: %u\n", find_result.as_error().as_int());
                    goto next;
                }

                buf  = find_result.as_value();
                info = follow_mountpoints(&buf.value());
            }

            if(const auto e = info->driver->mkdir(info->driver_data, elms.back())) {
                printf("mkdir error: %u\n", e.as_int());
            }
        }
    next:
        continue;
    }
}

auto main(const int argc, const char* const argv[]) -> int {
    if(argc != 2) {
        puts("invalid usage\n");
        return 1;
    }

    auto       dummy_device = block::dummy::DummyBlockDevice(argv[1]);
    const auto partitions   = block::gpt::find_partitions(&dummy_device);
    if(!partitions) {
        printf("cannot find partitions: %d\n", static_cast<int>(partitions.as_error()));
    }
    for(const auto& p : partitions.as_value()) {
        auto info = p.device->get_info();
        printf("partition found: %luMib Type=%d\n", info.bytes_per_sector * info.total_sectors / 1024 / 1024, p.filesystem);
    }

    auto  basicfs = fs::basic::Driver();
    auto& root    = basicfs.get_root();
    auto  tmpfs   = fs::tmp::Driver();
    root.mount    = &tmpfs.get_root();
    prompt(root);
    return 0;
}
