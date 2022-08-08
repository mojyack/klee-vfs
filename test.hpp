#include "fs/control.hpp"

#define open_handle(var, expr)     \
    auto var##_open_result = expr; \
    assert(var##_open_result);     \
    auto& var = var##_open_result.as_value();

#define open_handle_again(var, expr) \
    var##_open_result = expr;        \
    assert(var##_open_result);       \
    var = var##_open_result.as_value();

#define assert(a)                                        \
    if(!(a)) {                                           \
        printf("test failed at %d: " #a "\n", __LINE__); \
        return false;                                    \
    }

using Testdata         = fs::OpenInfo::Testdata;
constexpr auto nomount = std::nullopt;

// test data construction
inline auto tdc(const std::string_view path, const uint32_t read, const uint32_t write, const uint32_t child, std::optional<Testdata> mount = nomount, std::vector<Testdata> children = {}) -> Testdata {
    auto r = Testdata{std::string(path), read, write, child, {}, {}};
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
    open_handle(handle, controller.open(dirname, fs::OpenMode::Write));
    assert(!handle.create(filename, type));
    assert(!controller.close(handle));
    return true;
}

inline auto test_nested_mount() -> bool {
    auto controller = fs::Controller();
    assert(controller._compare_root(tdc("/", 0, 0, 0)));
    auto tmpfs1 = fs::tmp::Driver();
    assert(!controller.mount("/", tmpfs1));

    assert(create(controller, "/", "tmp", fs::FileType::Directory));

    auto tmpfs2 = fs::tmp::Driver();
    assert(!controller.mount("/tmp", tmpfs2));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 0, 1, nomount, {
                                                                                              tdc("tmp", 0, 1, 0, tdc(/*tmp2*/ "/", 0, 0, 0)),
                                                                                          }))));

    auto tmpfs3 = fs::tmp::Driver();
    assert(!controller.mount("/tmp", tmpfs3));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 0, 2, nomount, {
                                                                                              tdc("tmp", 0, 1, 0, tdc(/*tmp2*/ "/", 0, 1, 0, tdc(/*tmp3*/ "/", 0, 0, 0))),
                                                                                          }))));

    assert(!controller.unmount("/tmp"));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 0, 1, nomount, {
                                                                                              tdc("tmp", 0, 1, 0, tdc(/*tmp2*/ "/", 0, 0, 0)),
                                                                                          }))));

    assert(!controller.unmount("/tmp"));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 0, 0))));

    assert(!controller.unmount("/"));
    assert(controller._compare_root(tdc("/", 0, 0, 0)));
    return true;
}

inline auto test_nested_open_close() -> bool {
    auto controller = fs::Controller();
    assert(controller._compare_root(tdc("/", 0, 0, 0)));
    auto tmpfs = fs::tmp::Driver();
    assert(!controller.mount("/", tmpfs));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 0, 0))));

    assert(create(controller, "/", "dir", fs::FileType::Directory));
    assert(create(controller, "/", "dir2", fs::FileType::Directory));
    assert(create(controller, "/dir", "dir", fs::FileType::Directory));

    open_handle(root_dir, controller.open("/dir", fs::OpenMode::Read));
    open_handle(root_dir2, controller.open("/dir2", fs::OpenMode::Read));
    open_handle(root_dir_dir, controller.open("/dir/dir", fs::OpenMode::Read));

    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 0, 3, nomount, {
                                                                                     tdc("dir", 1, 0, 1, nomount, {
                                                                                                                      tdc("dir", 1, 0, 0),
                                                                                                                  }),
                                                                                     tdc("dir2", 1, 0, 0),
                                                                                 }))));
    assert(!controller.close(root_dir));
    assert(!controller.close(root_dir2));
    assert(!controller.close(root_dir_dir));

    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 0, 0))));

    return true;
}

inline auto test_open_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller.mount("/", tmpfs);

    open_handle(root, controller.open("/", fs::OpenMode::Read));
    assert(root.create("dir", fs::FileType::Directory) == Error::Code::FileNotOpened);
    return true;
}

inline auto test_exist_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller.mount("/", tmpfs);

    auto root_dir = controller.open("/dir", fs::OpenMode::Read);
    assert(!root_dir);
    assert(root_dir.as_error() == Error::Code::NoSuchFile);
    return true;
}

inline auto test_tmpfs_rw() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller.mount("/", tmpfs);

    assert(create(controller, "/", "file", fs::FileType::Regular));
    open_handle(root_file, controller.open("file", fs::OpenMode::Write));

    {
        auto       buffer = std::string("test data");
        const auto len    = buffer.size();
        assert(root_file.write(0, len, buffer.data()));
        auto buffer2 = std::string(buffer.size(), 0xFF);
        assert(root_file.read(0, len, buffer2.data()));
        assert(buffer == buffer2);
    }

    {
        auto buffer = std::array<uint8_t, bytes_per_frame * 3>();
        for(auto i = 0 ; i < buffer.size(); i += 1) {
            buffer[i] = i;
        }
        const auto write_head = bytes_per_frame + 1;
        assert(root_file.write(write_head, buffer.size(), buffer.data()));
        auto buffer2 = std::array<uint8_t, 256>();
        assert(root_file.read(write_head, buffer2.size(), buffer2.data()));
        for(auto i = 0; i < buffer2.size(); i += 1) {
            assert(buffer2[i] == buffer[i]);
        }
    }

    return true;
}

inline auto test() -> bool {
    assert(test_nested_mount());
    assert(test_nested_open_close());
    assert(test_open_error());
    assert(test_exist_error());
    assert(test_tmpfs_rw());
    puts("all tests passed\n");
    return true;
}

#undef assert
#undef open_handle
#undef open_handle_again
