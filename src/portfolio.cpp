#include "mvo/portfolio.hpp"
#include "mvo/solver.hpp"
#include "mvo/compatibility.hpp"
namespace mvo {
PortfolioResult run_portfolio(const Samples &daily, const std::vector<RebalanceMoments> &moments,
                              const Options &options, Timing *timing) {
    PortfolioResult result;
    result.weight.resize(daily.size(), options.initial);
    result.daily_pnl.assign(daily.size(), 0);
    result.pnl.assign(daily.size(), 1);
    result.solves.reserve(moments.size());
    Vector previous = options.initial;
    size_t next = 0;
    int fallback_streak = 0;
    const auto construction_start = Timing::Clock::now();
    double solver_ms = 0;
    for (size_t t = 0; t < daily.size(); ++t) {
        require(daily[t].allFinite(), "nonfinite daily PNL");
        if (next < moments.size() && moments[next].day == t) {
            const auto &current = moments[next++];
            const auto &selected = options.ewm ? current.ewm : current.ordinary;
            const auto start = Timing::Clock::now();
            SolveInfo solved;
            if (options.accuracy == AccuracyMode::PythonCompatible) {
                ScopedTimer timer(timing, "SLSQP_compatibility");
                solved = solve_compatibility(selected, previous, options.risk_averse, options.ewm);
            } else
                solved = optimize(selected, previous, options.risk_averse, options, timing);
            solver_ms +=
                std::chrono::duration<double, std::milli>(Timing::Clock::now() - start).count();
            if (solved.fallback) {
                ++result.fallback_count;
                ++fallback_streak;
                result.max_fallback_streak = std::max(result.max_fallback_streak, fallback_streak);
            } else
                fallback_streak = 0;
            if (solved.retry)
                ++result.retry_count;
            if (!solved.success)
                ++result.solver_failure_count;
            result.solves.push_back(solved);
            previous = solved.weight;
        }
        result.weight[t] = previous;
        result.daily_pnl[t] = previous.dot(daily[t]);
        result.pnl[t] = (t ? result.pnl[t - 1] : 1) + result.daily_pnl[t];
    }
    if (timing)
        timing->ms["portfolio_construction"] +=
            std::chrono::duration<double, std::milli>(Timing::Clock::now() - construction_start)
                .count() -
            solver_ms;
    return result;
}
} // namespace mvo
