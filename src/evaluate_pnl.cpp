#include "mvo/evaluate_pnl.hpp"

#include "mvo/dates.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace mvo {
namespace {
template <class T> void print_optional(std::ostream& out, const std::optional<T>& value) {
    if (value)
        out << *value;
    else
        out << "NA";
}

std::optional<double> sample_std(const std::vector<double>& values) {
    if (values.size() < 2)
        return std::nullopt;
    double mean = 0.0;
    for (double value : values)
        mean += value;
    mean /= static_cast<double>(values.size());
    double sum_sq = 0.0;
    for (double value : values) {
        const double delta = value - mean;
        sum_sq += delta * delta;
    }
    const double result = std::sqrt(sum_sq / static_cast<double>(values.size() - 1));
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}
} // namespace

EvaluatePNL::EvaluatePNL(std::vector<double> pnl, int yr_dates, EvaluateMode mode,
                         std::vector<double> dates)
    : pnl_(std::move(pnl)), yr_dates_(yr_dates), mode_(mode), dates_(std::move(dates)) {
    if (yr_dates_ <= 0)
        throw std::invalid_argument("annual days must be positive");
    for (double value : pnl_)
        if (!std::isfinite(value))
            throw std::invalid_argument("PNL must be finite");
    if (!dates_.empty()) {
        if (dates_.size() != pnl_.size())
            throw std::invalid_argument("dates/PNL dimension mismatch");
        for (std::size_t i = 1; i < dates_.size(); ++i)
            if (!std::isfinite(dates_[i]) || dates_[i] <= dates_[i - 1])
                throw std::invalid_argument("dates must be strictly increasing");
        if (!dates_.empty() && !std::isfinite(dates_.front()))
            throw std::invalid_argument("dates must be finite");
    }
}

std::vector<double> EvaluatePNL::returns() const {
    std::vector<double> result;
    if (pnl_.size() < 2)
        return result;
    result.reserve(pnl_.size() - 1);
    for (std::size_t i = 1; i < pnl_.size(); ++i) {
        const double value = pnl_[i] - pnl_[i - 1];
        // Python compatibility drops zero differences. High mode treats a
        // zero daily result as a real observation in the denominator.
        if (mode_ == EvaluateMode::High || value != 0.0)
            result.push_back(value);
    }
    return result;
}

std::optional<double> EvaluatePNL::get_return() const {
    const auto values = returns();
    if (values.empty())
        return std::nullopt;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    const double result = sum / static_cast<double>(values.size()) * yr_dates_;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

std::optional<double> EvaluatePNL::get_std() const {
    const auto values = returns();
    const auto deviation = sample_std(values);
    if (!deviation)
        return std::nullopt;
    const double result = *deviation * std::sqrt(static_cast<double>(yr_dates_));
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

std::optional<double> EvaluatePNL::get_sharp() const {
    const auto average = get_return();
    const auto deviation = get_std();
    if (!average || !deviation || *deviation == 0.0)
        return std::nullopt;
    const double result = *average / *deviation;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

std::optional<EvaluatePNL::Drawdown> EvaluatePNL::get_max_drawdown() const {
    if (pnl_.size() < 2)
        return std::nullopt;
    if (mode_ == EvaluateMode::Compat) {
        // Preserve algo.py's all-pairs behavior for exact compatibility.
        double best = -std::numeric_limits<double>::infinity();
        Drawdown result{best, 0, 1};
        for (std::size_t i = 0; i + 1 < pnl_.size(); ++i)
            for (std::size_t j = i + 1; j < pnl_.size(); ++j)
                if (const double value = pnl_[i] - pnl_[j]; value > result.value)
                    result = {value, i, j};
        return std::isfinite(result.value) ? std::optional<Drawdown>(result) : std::nullopt;
    }

    // High mode: the same absolute drawdown definition in O(n) time and O(1)
    // extra memory. A monotone series has zero drawdown.
    std::size_t peak = 0;
    Drawdown result{0.0, 0, 1};
    for (std::size_t j = 1; j < pnl_.size(); ++j) {
        const double value = pnl_[peak] - pnl_[j];
        if (value > result.value)
            result = {value, peak, j};
        if (pnl_[j] > pnl_[peak])
            peak = j;
    }
    return result;
}

std::optional<double> EvaluatePNL::get_win_percent() const {
    const auto values = returns();
    if (values.empty())
        return std::nullopt;
    std::size_t wins = 0;
    for (double value : values)
        wins += value > 0.0;
    const double result = static_cast<double>(wins) / static_cast<double>(values.size());
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

std::optional<double> EvaluatePNL::get_calmar() const {
    if (dates_.size() < 2)
        return std::nullopt;
    const auto drawdown = get_max_drawdown();
    if (!drawdown || drawdown->value == 0.0)
        return std::nullopt;
    const double years = (dates_.back() - dates_.front()) / 365.0;
    const double denominator = years * drawdown->value;
    if (years <= 0.0 || denominator == 0.0 || !std::isfinite(denominator))
        return std::nullopt;
    const double result = (pnl_.back() - pnl_.front()) / denominator;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

EvaluatePNL::Result EvaluatePNL::evaluate() const {
    return {get_return(), get_std(), get_sharp(), get_max_drawdown(), get_win_percent(),
            get_calmar()};
}

void EvaluatePNL::print(std::ostream& out) const {
    const auto result = evaluate();
    out << std::fixed << std::setprecision(2);
    if (result.annual_return)
        out << *result.annual_return * 100.0;
    else
        out << "NA";
    out << " & ";
    if (result.annual_std)
        out << *result.annual_std * 100.0;
    else
        out << "NA";
    out << " & " << std::setprecision(3);
    print_optional(out, result.sharpe);
    out << " & ";
    print_optional(out, result.calmar);
    out << " & " << std::setprecision(2);
    if (result.win_percent)
        out << *result.win_percent * 100.0;
    else
        out << "NA";
    out << " & ";
    if (result.max_drawdown)
        out << result.max_drawdown->value * 100.0;
    else
        out << "NA";
    if (result.max_drawdown && !dates_.empty())
        out << " & " << date_string(dates_[result.max_drawdown->peak]) << " & "
            << date_string(dates_[result.max_drawdown->trough]);
    out << '\n';
}
} // namespace mvo
