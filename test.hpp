#include "fs/control.hpp"

#define open_handle(var, expr)     \
    auto var##_open_result = expr; \
    assert(var##_open_result);     \
    auto& var = var##_open_result.as_value();

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

inline auto test_nested_mount() -> bool {
    auto controller = fs::Controller();
    assert(controller._compare_root(tdc("/", 0, 0, 0)));
    auto tmpfs1 = fs::tmp::Driver();
    assert(!controller.mount("/", tmpfs1));
    open_handle(root, controller.open("/", fs::OpenMode::Write));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 1, 0))));

    assert(!root.mkdir("tmp"));
    auto tmpfs2 = fs::tmp::Driver();
    assert(!controller.mount("/tmp", tmpfs2));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 1, 1, nomount, {
                                                                                              tdc("tmp", 0, 1, 0, tdc(/*tmp2*/ "/", 0, 0, 0)),
                                                                                          }))));

    auto tmpfs3 = fs::tmp::Driver();
    assert(!controller.mount("/tmp", tmpfs3));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 1, 2, nomount, {
                                                                                              tdc("tmp", 0, 1, 0, tdc(/*tmp2*/ "/", 0, 1, 0, tdc(/*tmp3*/ "/", 0, 0, 0))),
                                                                                          }))));

    assert(!controller.unmount("/tmp"));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 1, 1, nomount, {
                                                                                              tdc("tmp", 0, 1, 0, tdc(/*tmp2*/ "/", 0, 0, 0)),
                                                                                          }))));

    assert(!controller.unmount("/tmp"));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc(/*tmp1*/ "/", 0, 1, 0))));

    assert(!controller.close(root));
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

    // mkdir "/dir"
    open_handle(root, controller.open("/", fs::OpenMode::Write));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 0))));

    assert(!root.mkdir("dir"));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 0))));

    // mkdir "/dir/dir"
    open_handle(root_dir, controller.open("/dir", fs::OpenMode::Write));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 1, nomount, {
                                                                                     tdc("dir", 0, 1, 0),
                                                                                 }))));

    assert(!root_dir.mkdir("dir"));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 1, nomount, {
                                                                                     tdc("dir", 0, 1, 0),
                                                                                 }))));

    open_handle(root_dir_dir, controller.open("/dir/dir", fs::OpenMode::Read));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 2, nomount, {
                                                                                     tdc("dir", 0, 1, 1, nomount, {
                                                                                                                      tdc("dir", 1, 0, 0),
                                                                                                                  }),
                                                                                 }))));

    // mkdir "/dir2"
    assert(!root.mkdir("dir2"));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 2, nomount, {
                                                                                     tdc("dir", 0, 1, 1, nomount, {
                                                                                                                      tdc("dir", 1, 0, 0),
                                                                                                                  }),
                                                                                 }))));

    open_handle(root_dir2, controller.open("/dir2", fs::OpenMode::Read));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 3, nomount, {
                                                                                     tdc("dir", 0, 1, 1, nomount, {
                                                                                                                      tdc("dir", 1, 0, 0),
                                                                                                                  }),
                                                                                     tdc("dir2", 1, 0, 0),
                                                                                 }))));

    // close and remove "/dir2"
    assert(root.remove("dir2") == Error::Code::FileOpened);
    assert(!controller.close(root_dir2));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 1, 2, nomount, {
                                                                                     tdc("dir", 0, 1, 1, nomount, {
                                                                                                                      tdc("dir", 1, 0, 0),
                                                                                                                  }),
                                                                                 }))));
    assert(!root.remove("dir2"));
    assert(controller.open("/dir2", fs::OpenMode::Read).as_error() == Error::Code::NoSuchFile);

    // close and remove all directories
    assert(!controller.close(root_dir_dir));
    assert(!controller.close(root_dir));
    assert(!root.remove("dir"));
    assert(!controller.close(root));
    assert(controller._compare_root(tdc("/", 0, 1, 0, tdc("/", 0, 0, 0))));

    return true;
}

inline auto test_open_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller.mount("/", tmpfs);

    open_handle(root, controller.open("/", fs::OpenMode::Read));
    assert(root.mkdir("dir") == Error::Code::FileNotOpened);
    return true;
}

inline auto test_exist_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller.mount("/", tmpfs);

    auto root = controller.open("/", fs::OpenMode::Read);
    assert(root);

    auto root_dir = controller.open("/dir", fs::OpenMode::Read);
    assert(!root_dir);
    assert(root_dir.as_error() == Error::Code::NoSuchFile);
    return true;
}

inline auto test() -> bool {
    assert(test_nested_mount());
    assert(test_nested_open_close());
    assert(test_open_error());
    assert(test_exist_error());
    puts("all tests passed\n");
    return true;
}

#undef assert
#undef open_handle
