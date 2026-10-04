#include "mvo/evaluation.hpp"
#include "mvo/dates.hpp"
namespace mvo {
Metrics Evaluator::evaluate(const std::vector<double> &pnl, const std::vector<double> &dates,
                            int annual_days, AccuracyMode mode) {
    require(pnl.size() == dates.size() && pnl.size() >= 2 && annual_days > 0, "invalid PNL series");
    for (size_t i = 0; i < pnl.size(); ++i) {
        require(std::isfinite(pnl[i]), "nonfinite PNL");
        if (i)
            require(dates[i] > dates[i - 1], "invalid evaluation dates");
    }
    std::vector<double> returns;
    returns.reserve(pnl.size() - 1);
    for (size_t i = 1; i < pnl.size(); ++i) {
        double v = pnl[i] - pnl[i - 1];
        if (mode == AccuracyMode::HighAccuracy || v != 0)
            returns.push_back(v);
    }
    const double count = static_cast<double>(returns.size());
    double sum = 0;
    for (double v : returns)
        sum += v;
    double mean = count ? sum / count : unavailable(), ss = 0;
    for (double v : returns)
        ss += (v - mean) * (v - mean);
    double vol = count > 1 ? std::sqrt(ss / (count - 1) * annual_days) : unavailable();
    double peak = pnl[0], drawdown = -std::numeric_limits<double>::infinity();
    size_t peak_i = 0, best_i = 0, best_j = 1;
    for (size_t j = 1; j < pnl.size(); ++j) {
        double dd = peak - pnl[j];
        if (dd > drawdown ||
            (dd == drawdown && (peak_i < best_i || (peak_i == best_i && j < best_j)))) {
            drawdown = dd;
            best_i = peak_i;
            best_j = j;
        }
        if (pnl[j] > peak) {
            peak = pnl[j];
            peak_i = j;
        }
    }
    double wins = 0;
    for (double v : returns)
        wins += v > 0;
    double years = elapsed_days(dates.front(), dates.back()) / 365.0;
    double annual = mean * annual_days;
    return {annual,
            vol,
            annual / vol,
            (pnl.back() - pnl.front()) / (years * drawdown),
            count ? wins / count : unavailable(),
            drawdown,
            best_i,
            best_j};
}

} // namespace mvo
