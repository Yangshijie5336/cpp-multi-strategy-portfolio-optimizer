#pragma once
#include <Eigen/Dense>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mvo {
constexpr int assets = 8;
using Vector = Eigen::Matrix<double, assets, 1>;
using Covariance = Eigen::Matrix<double, assets, assets>;
using Samples = std::vector<Vector>;
inline Vector initial_weights() {
    Vector p;
    p << .089, .1025, .0635, .0645, .2572, .1655, .1272, .1306;
    return p;
}
inline double unavailable() {
    return std::numeric_limits<double>::quiet_NaN();
}
enum class AccuracyMode { PythonCompatible, HighAccuracy };
enum class InputType { CumulativePnL, NAV, DailyReturn };
enum class MissingValuePolicy { Reject, DropRow };
enum class SolverKind { LP, QP, SLSQP, PreviousWeight };
struct Options {
    int window = 60, keep = 15, yr_dates = 250;
    double sigma = 3, turnover = .1, feasibility_tolerance = 1e-9, optimality_tolerance = 1e-9;
    double eigenvalue_relative_floor = 1e-12;
    // Reproduction of algo.py is the public/default path.  HighAccuracy is
    // opt-in because its robust covariance and convex solver intentionally
    // change the statistical model and therefore the numerical result.
    AccuracyMode accuracy = AccuracyMode::PythonCompatible;
    InputType input = InputType::CumulativePnL;
    MissingValuePolicy missing = MissingValuePolicy::Reject;
    std::optional<double> capital;
    std::optional<double> risk_averse;
    bool ewm = false;
    Vector initial = initial_weights();
    bool debug = false;
};
struct SheetData {
    std::vector<double> dates;
    Samples values;
    size_t dropped_rows = 0;
};
struct Timing {
    std::map<std::string, double> ms;
    using Clock = std::chrono::steady_clock;
};
struct ScopedTimer {
    Timing *timings;
    std::string name;
    Timing::Clock::time_point start = Timing::Clock::now();
    ScopedTimer(Timing *t, std::string n) : timings(t), name(std::move(n)) {}
    ~ScopedTimer() {
        if (timings)
            timings->ms[name] +=
                std::chrono::duration<double, std::milli>(Timing::Clock::now() - start).count();
    }
};
inline void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct MomentEstimate {
    Vector mean = Vector::Zero(), volatility = Vector::Zero();
    Covariance covariance = Covariance::Zero();
    double shrinkage_alpha = 0, effective_sample_size = 0;
    double min_eigen_before = 0, min_eigen_after = 0, condition_before = 0, condition_after = 0;
    int bandwidth = 0;
};
struct RebalanceMoments {
    size_t day;
    MomentEstimate ordinary, ewm;
};
struct ConstraintReport {
    double sum_weight_error = 0, nonnegative_error = 0, turnover_error = 0, group_violation = 0,
           turnover = 0;
    double max_violation() const {
        return std::max({sum_weight_error, nonnegative_error, turnover_error, group_violation});
    }
};
struct SolveInfo {
    Vector weight = Vector::Zero();
    SolverKind solver = SolverKind::PreviousWeight;
    bool success = false, retry = false, fallback = false;
    int iterations = 0;
    double objective = unavailable(), primal_residual = unavailable(),
           dual_residual = unavailable(), kkt_residual = unavailable(),
           complementarity = unavailable();
    ConstraintReport constraints;
    std::string message;
};
struct PortfolioResult {
    Samples weight;
    std::vector<double> daily_pnl, pnl;
    std::vector<SolveInfo> solves;
    int fallback_count = 0, retry_count = 0, solver_failure_count = 0, max_fallback_streak = 0;
};
struct Metrics {
    double annual_pnl, volatility, sharpe, calmar, win_rate, drawdown;
    size_t peak, trough;
};
inline const char *solver_name(SolverKind k) {
    switch (k) {
    case SolverKind::LP:
        return "HiGHS-LP";
    case SolverKind::QP:
        return "HiGHS-QP";
    case SolverKind::SLSQP:
        return "SLSQP";
    default:
        return "previous";
    }
}
} // namespace mvo
