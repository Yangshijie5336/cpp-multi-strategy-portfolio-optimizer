#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <vector>

namespace mvo {

// Compat reproduces algo.py's non-zero-return metric convention. High keeps
// zero-return observations and uses linear-time drawdown evaluation.
enum class EvaluateMode { Compat, High };

class EvaluatePNL {
  public:
    struct Drawdown {
        double value = 0.0;
        std::size_t peak = 0;
        std::size_t trough = 0;
    };

    struct Result {
        std::optional<double> annual_return;
        std::optional<double> annual_std;
        std::optional<double> sharpe;
        std::optional<Drawdown> max_drawdown;
        std::optional<double> win_percent;
        std::optional<double> calmar;
    };

    explicit EvaluatePNL(std::vector<double> pnl = {}, int yr_dates = 250,
                         EvaluateMode mode = EvaluateMode::Compat,
                         std::vector<double> dates = {});

    std::optional<double> get_return() const;
    std::optional<double> get_std() const;
    std::optional<double> get_sharp() const;
    std::optional<Drawdown> get_max_drawdown() const;
    std::optional<double> get_win_percent() const;
    std::optional<double> get_calmar() const;
    Result evaluate() const;

    // Compatible with algo.py's final presentation, while printing "NA"
    // whenever a metric is mathematically unavailable.
    void print(std::ostream& out) const;

    EvaluateMode mode() const noexcept { return mode_; }
    int annual_days() const noexcept { return yr_dates_; }

  private:
    std::vector<double> returns() const;
    std::vector<double> pnl_;
    int yr_dates_;
    EvaluateMode mode_;
    std::vector<double> dates_;
};

} // namespace mvo
