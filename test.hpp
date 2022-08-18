#include "fs/control.hpp"
#include "fs/drivers/fat/driver.hpp"

#ifdef value_or
#undef value_or
#endif

#define value_or(var, expr)        \
    auto var##_open_result = expr; \
    assert(var##_open_result);     \
    auto& var = var##_open_result.as_value();

#define assert(a)                                        \
    if(!(a)) {                                           \
        printf("test failed at %d: " #a "\n", __LINE__); \
        return false;                                    \
    }

using Testdata         = fs::OpenInfo::Testdata;
using Type             = fs::FileType;
constexpr auto nomount = std::nullopt;

// test data construction
inline auto tdc(const std::string_view path, const uint32_t read, const uint32_t write, const Type type, std::optional<Testdata> mount = nomount, std::vector<Testdata> children = {}) -> Testdata {
    auto r = Testdata{std::string(path), read, write, type, {}, {}};
    if(mount) {
        r.mount.reset(new fs::OpenInfo::Testdata);
        *r.mount.get() = std::move(mount.value());
    }
    for(auto& c : children) {
        r.children[c.name] = std::move(c);
    }
    return r;
}

inline auto create(fs::Controller& controller, const std::string_view dirname, const std::string_view filename, const fs::FileType type) -> bool {
    value_or(handle, controller.open(dirname, fs::OpenMode::Write));
    assert(!handle.create(filename, type));
    assert(!controller.close(handle));
    return true;
}

inline auto test_nested_mount() -> bool {
    auto controller = fs::Controller();
    assert(controller._compare_root(tdc("/", 0, 0, Type::Directory)));
    auto tmpfs1 = fs::tmp::new_driver();
    assert(!controller.mount("/", tmpfs1));

    assert(create(controller, "/", "tmp", fs::FileType::Directory));

    auto tmpfs2 = fs::tmp::new_driver();
    assert(!controller.mount("/tmp", tmpfs2));
    assert(controller._compare_root(tdc("/", 0, 1, Type::Directory, tdc(/*tmp1*/ "/", 0, 0, Type::Directory, nomount, {
                                                                                                                          tdc("tmp", 0, 1, Type::Directory, tdc(/*tmp2*/ "/", 0, 0, Type::Directory)),
                                                                                                                      }))));

    auto tmpfs3 = fs::tmp::new_driver();
    assert(!controller.mount("/tmp", tmpfs3));
    assert(controller._compare_root(tdc("/", 0, 1, Type::Directory, tdc(/*tmp1*/ "/", 0, 0, Type::Directory, nomount, {
                                                                                                                          tdc("tmp", 0, 1, Type::Directory, tdc(/*tmp2*/ "/", 0, 1, Type::Directory, tdc(/*tmp3*/ "/", 0, 0, Type::Directory))),
                                                                                                                      }))));

    assert(controller.unmount("/tmp"));
    assert(controller._compare_root(tdc("/", 0, 1, Type::Directory, tdc(/*tmp1*/ "/", 0, 0, Type::Directory, nomount, {
                                                                                                                          tdc("tmp", 0, 1, Type::Directory, tdc(/*tmp2*/ "/", 0, 0, Type::Directory)),
                                                                                                                      }))));

    assert(controller.unmount("/tmp"));
    assert(controller._compare_root(tdc("/", 0, 1, Type::Directory, tdc(/*tmp1*/ "/", 0, 0, Type::Directory))));

    assert(controller.unmount("/"));
    assert(controller._compare_root(tdc("/", 0, 0, Type::Directory)));
    return true;
}

inline auto test_nested_open_close() -> bool {
    auto controller = fs::Controller();
    assert(controller._compare_root(tdc("/", 0, 0, Type::Directory)));
    auto tmpfs = fs::tmp::new_driver();
    assert(!controller.mount("/", tmpfs));
    assert(controller._compare_root(tdc("/", 0, 1, Type::Directory, tdc("/", 0, 0, Type::Directory))));

    assert(create(controller, "/", "dir", fs::FileType::Directory));
    assert(create(controller, "/", "dir2", fs::FileType::Directory));
    assert(create(controller, "/dir", "dir", fs::FileType::Directory));

    value_or(root_dir, controller.open("/dir", fs::OpenMode::Read));
    value_or(root_dir2, controller.open("/dir2", fs::OpenMode::Read));
    value_or(root_dir_dir, controller.open("/dir/dir", fs::OpenMode::Read));

    assert(controller._compare_root(tdc("/", 0, 1, Type::Directory, tdc("/", 0, 0, Type::Directory, nomount, {
                                                                                                                 tdc("dir", 1, 0, Type::Directory, nomount, {
                                                                                                                                                                tdc("dir", 1, 0, Type::Directory),
                                                                                                                                                            }),
                                                                                                                 tdc("dir2", 1, 0, Type::Directory),
                                                                                                             }))));
    assert(!controller.close(root_dir));
    assert(!controller.close(root_dir2));
    assert(!controller.close(root_dir_dir));

    assert(controller._compare_root(tdc("/", 0, 1, Type::Directory, tdc("/", 0, 0, Type::Directory))));

    return true;
}

inline auto test_open_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::new_driver();
    controller.mount("/", tmpfs);

    value_or(root, controller.open("/", fs::OpenMode::Read));
    assert(root.create("dir", fs::FileType::Directory) == Error::Code::FileNotOpened);
    return true;
}

inline auto test_exist_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::new_driver();
    controller.mount("/", tmpfs);

    auto root_dir = controller.open("/dir", fs::OpenMode::Read);
    assert(!root_dir);
    assert(root_dir.as_error() == Error::Code::NoSuchFile);
    return true;
}

inline auto test_tmpfs_rw() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::new_driver();
    controller.mount("/", tmpfs);

    assert(create(controller, "/", "file", fs::FileType::Regular));
    value_or(root_file, controller.open("file", fs::OpenMode::Write));

    {
        auto       buffer = std::string("test data");
        const auto len    = buffer.size();
        assert(!root_file.write(0, len, buffer.data()));
        auto buffer2 = std::string(buffer.size(), 0xFF);
        assert(!root_file.read(0, len, buffer2.data()));
        assert(buffer == buffer2);
    }

    {
        auto buffer = std::array<uint8_t, bytes_per_frame * 3>();
        for(auto i = 0; i < buffer.size(); i += 1) {
            buffer[i] = i;
        }
        const auto write_head = bytes_per_frame + 1;
        assert(!root_file.write(write_head, buffer.size(), buffer.data()));
        auto buffer2 = std::array<uint8_t, 256>();
        assert(!root_file.read(write_head, buffer2.size(), buffer2.data()));
        for(auto i = 0; i < buffer2.size(); i += 1) {
            assert(buffer2[i] == buffer[i]);
        }
    }

    return true;
}

template <size_t size>
inline auto test_ls(fs::Handle handle, std::array<const char*, size> expected) -> bool {
    for(auto i = 0; i < expected.size(); i += 1) {
        const auto r = handle.readdir(i);
        if(!r) {
            const auto e = r.as_error();
            if(e == Error::Code::EndOfFile) {
                return true;
            } else {
                return false;
            }
        }
        auto& o = r.as_value();

        assert(o.name == expected[i]);
    }
    return true;
}

inline auto test_fat_rw(block::BlockDevice& block) -> bool {
    auto controller = fs::Controller();
    value_or(fatfs, fs::fat::new_driver(block));
    controller.mount("/", *fatfs.get());

    value_or(root, controller.open("/", fs::OpenMode::Read));
    assert(test_ls(root, std::array{"apps", "EFI", "kernel.elf", "NvVars", "MEMMAP"}));

    value_or(root_efi, controller.open("/EFI", fs::OpenMode::Read));
    assert(test_ls(root_efi, std::array{".", "..", "BOOT"}));

    value_or(root_memmap, controller.open("/MEMMAP", fs::OpenMode::Read));
    const auto size   = root_memmap.get_size();
    auto       buffer = std::vector<uint8_t>(size);
    assert(!root_memmap.read(0, size, buffer.data()));
    printf("%lu, %s\n", size, buffer.data());

    return true;
}

inline auto test_duplicated_mount() -> bool {
    constexpr auto d = Type::Directory;

    auto controller = fs::Controller();
    assert(controller._compare_root(tdc("/", 0, 0, d)));
    auto tmpfs_root = fs::tmp::new_driver();
    assert(!controller.mount("/", tmpfs_root));

    assert(create(controller, "/", "a", fs::FileType::Directory));
    assert(create(controller, "/", "b", fs::FileType::Directory));
    assert(create(controller, "/b", "bb", fs::FileType::Directory));

    auto tmpfs = fs::tmp::new_driver();
    assert(!controller.mount("/a", tmpfs));
    assert(!controller.mount("/b/bb", tmpfs));
    assert(controller._compare_root(tdc("/", 0, 1, d, tdc(/*tmpfs_root*/ "/", 0, 0, d, nomount, {
                                                                                                    tdc("a", 0, 1, d, tdc(/*tmpfs*/ "/", 0, 0, d)),
                                                                                                    tdc("b", 0, 0, d, nomount, {
                                                                                                                                   tdc("bb", 0, 1, d, tdc(/*tmpfs*/ "/", 0, 0, d)),
                                                                                                                               }),
                                                                                                }))));
    auto tmpfs2 = fs::tmp::new_driver();
    assert(!controller.mount("/a", tmpfs2));
    assert(controller._compare_root(tdc("/", 0, 1, d, tdc(/*tmpfs_root*/ "/", 0, 0, d, nomount, {
                                                                                                    tdc("a", 0, 1, d, tdc(/*tmpfs*/ "/", 0, 1, d, tdc(/*tmpfs2*/"/", 0, 0, d))),
                                                                                                    tdc("b", 0, 0, d, nomount, {
                                                                                                                                   tdc("bb", 0, 1, d, tdc(/*tmpfs*/ "/", 0, 1, d, tdc(/*tmpfs2*/"/", 0, 0, d))),
                                                                                                                               }),
                                                                                                }))));

    return true;
}

inline auto test(block::BlockDevice* const fat_volume) -> bool {
    assert(test_nested_mount());
    assert(test_nested_open_close());
    assert(test_open_error());
    assert(test_exist_error());
    assert(test_tmpfs_rw());
    assert(test_duplicated_mount());

    if(fat_volume == nullptr) {
        puts("fat volume not found, skipping test");
    } else {
        assert(test_fat_rw(*fat_volume));
    }

    puts("all tests passed\n");
    return true;
}

#undef assert
#undef value_or
