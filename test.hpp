#include "fs/control.hpp"

#define assert(a)                 \
    if(!(a)) {                    \
        puts("test failed: " #a); \
        return false;             \
    }

inline auto test_nested_dir() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller._root_mount(tmpfs);

    auto root = controller.open("/", fs::OpenMode::Write);
    assert(root);
    assert(controller._compare_root(fs::OpenInfo::Testdata{"/", 0, 1, 0, {}}));

    assert(!root.as_value()->mkdir("dir"));
    assert(controller._compare_root(fs::OpenInfo::Testdata{"/", 0, 1, 0, {}}));

    auto root_dir = controller.open("/dir", fs::OpenMode::Write);
    assert(root_dir);
    assert(controller._compare_root(fs::OpenInfo::Testdata{"/", 0, 1, 1, {
                                                                             {"dir", {"dir", 0, 1, 0}},
                                                                         }}));

    assert(!root_dir.as_value()->mkdir("dir"));
    assert(controller._compare_root(fs::OpenInfo::Testdata{"/", 0, 1, 1, {
                                                                             {"dir", {"dir", 0, 1, 0}},
                                                                         }}));

    auto root_dir_dir = controller.open("/dir/dir", fs::OpenMode::Read);
    assert(root_dir_dir);
    assert(controller._compare_root(fs::OpenInfo::Testdata{"/", 0, 1, 2, {
                                                                             {"dir", {"dir", 0, 1, 1, {
                                                                                                          {"dir", {"dir", 1, 0, 0}},
                                                                                                      }}},
                                                                         }}));

    assert(!root.as_value()->mkdir("dir2"));
    assert(controller._compare_root(fs::OpenInfo::Testdata{"/", 0, 1, 2, {
                                                                             {"dir", {"dir", 0, 1, 1, {
                                                                                                          {"dir", {"dir", 1, 0, 0}},
                                                                                                      }}},
                                                                         }}));

    auto root_dir2 = controller.open("/dir2", fs::OpenMode::Read);
    assert(root_dir2);
    assert(controller._compare_root(fs::OpenInfo::Testdata{"/", 0, 1, 3, {
                                                                             {"dir", {"dir", 0, 1, 1, {
                                                                                                          {"dir", {"dir", 1, 0, 0}},
                                                                                                      }}},
                                                                             {"dir2", {"dir2", 1, 0, 0, {}}},
                                                                         }}));

    return true;
}

inline auto test_open_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller._root_mount(tmpfs);

    auto root = controller.open("/", fs::OpenMode::Read);
    assert(root);

    assert(root.as_value()->mkdir("dir") == Error::Code::FileNotOpened);
    return true;
}

inline auto test_exist_error() -> bool {
    auto controller = fs::Controller();
    auto tmpfs      = fs::tmp::Driver();
    controller._root_mount(tmpfs);

    auto root = controller.open("/", fs::OpenMode::Read);
    assert(root);

    auto root_dir = controller.open("/dir", fs::OpenMode::Read);
    assert(!root_dir);
    assert(root_dir.as_error() == Error::Code::NoSuchFile);
    return true;
}

inline auto test() -> bool {
    assert(test_nested_dir());
    assert(test_open_error());
    assert(test_exist_error());
    puts("all tests passed\n");
    return true;
}

#undef assert
