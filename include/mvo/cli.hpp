#pragma once
#include "types.hpp"

namespace mvo {
struct CommandLine {
    std::string input_path = "data.xls";
    Options options;
};
CommandLine parse_command_line(int argc, char **argv);
} // namespace mvo
