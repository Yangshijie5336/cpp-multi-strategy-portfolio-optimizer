#pragma once
#include "types.hpp"
#include <iosfwd>

namespace mvo {
class Evaluator {
  public:
    static Metrics evaluate(const std::vector<double> &pnl, const std::vector<double> &dates,
                            int annual_days, AccuracyMode mode = AccuracyMode::PythonCompatible);
    // Presentation is implemented separately in reporting.cpp.
    static void print(const Metrics &metrics, const std::vector<double> &dates, std::ostream &out);
};
} // namespace mvo
