#pragma once
#include <string_view>
#include <vector>

inline auto split_path(const std::string_view path) -> std::vector<std::string_view> {
    auto r = std::vector<std::string_view>();

    auto end   = size_t(0);
    auto start = size_t();
    while((start = path.find_first_not_of('/', end)) != std::string_view::npos) {
        end = path.find('/', start);
        r.emplace_back(path.substr(start, end - start));
    }
    return r;
}
