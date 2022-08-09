#include "block/drivers/cache.hpp"
#include "block/drivers/dummy.hpp"
#include "block/gpt.hpp"
#include "fs/control.hpp"
#include "fs/drivers/basic.hpp"
#include "fs/drivers/tmp.hpp"
#include "path.hpp"
#include "test.hpp"
#include "util.hpp"

using namespace std::literals;

auto prompt(fs::Controller& controller) -> void {
    while(true) {
        const auto args = split_args(read_line("> "));
        if(args.empty()) {
            continue;
        }
        if(args[0] == "exit") {
            break;
        } else if(args[0] == "open") {
            controller.open("/", fs::OpenMode::Read);
            goto next;
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

    auto       dummy_device        = block::dummy::DummyBlockDevice(argv[1]);
    auto       cached_dummy_device = block::cache::Device(dummy_device);
    const auto partitions          = block::gpt::find_partitions(&cached_dummy_device);
    if(!partitions) {
        printf("cannot find partitions: %d\n", static_cast<int>(partitions.as_error()));
    }

    auto fat_volume = (block::BlockDevice*)nullptr;
    for(const auto& p : partitions.as_value()) {
        auto info = p.device->get_info();
        printf("partition found: %luMib Type=%d\n", info.bytes_per_sector * info.total_sectors / 1024 / 1024, p.filesystem);
        if(p.filesystem == block::gpt::Filesystem::FAT32) {
            fat_volume = p.device.get();
        }
    }

    test(fat_volume);
    // auto controller = fs::Controller();
    // auto tmpfs      = fs::tmp::Driver();
    // controller._root_mount(tmpfs);
    // prompt(controller);
    return 0;
}
