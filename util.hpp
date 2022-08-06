#pragma once
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error.hpp"

inline auto read_line(const std::optional<std::string_view> prompt = std::nullopt) -> std::string {
    if(prompt) {
        std::cout << *prompt;
    }
    auto line = std::string();
    std::getline(std::cin, line);
    return line;
}

inline auto split_args(const std::string_view str) -> std::vector<std::string_view> {
    auto       result = std::vector<std::string_view>();
    const auto len    = str.size();
    auto       qot    = '\0';
    auto       arglen = size_t();
    for(auto i = size_t(0); i < len; i += 1) {
        auto start = i;
        if(str[i] == '\"' || str[i] == '\'') {
            qot = str[i];
        }
        if(qot != '\0') {
            i += 1;
            start += 1;
            while(i < len && str[i] != qot) {
                i += 1;
            }
            if(i < len) {
                qot = '\0';
            }
            arglen = i - start;
        } else {
            while(i < len && str[i] != ' ') {
                i += 1;
            }
            arglen = i - start;
        }
        result.emplace_back(str.data() + start, arglen);
    }
    // dynamic_assert(qot == '\0', "unclosed quotes");
    return result;
}
