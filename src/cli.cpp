#include "mvo/cli.hpp"

namespace mvo {
CommandLine parse_command_line(int argc, char **argv) {
    CommandLine result;
    if (argc > 1)
        result.input_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--compat")
            result.options.accuracy = AccuracyMode::PythonCompatible;
        else if (flag == "--high" || flag == "--high-accuracy")
            result.options.accuracy = AccuracyMode::HighAccuracy;
        else if (flag == "--debug")
            result.options.debug = true;
        else if (flag == "--nav")
            result.options.input = InputType::NAV;
        else if (flag == "--daily-return")
            result.options.input = InputType::DailyReturn;
        else
            throw std::invalid_argument("unknown option: " + flag);
    }
    return result;
}
} // namespace mvo
