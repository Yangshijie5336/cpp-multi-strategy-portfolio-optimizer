#include "mvo/evaluation.hpp"
#include "mvo/dates.hpp"
#include <iomanip>
#include <ostream>
namespace mvo {
void Evaluator::print(const Metrics &m, const std::vector<double> &dates, std::ostream &out) {
    out << std::fixed << std::setprecision(2) << m.annual_pnl * 100 << " & " << m.volatility * 100
        << " & " << std::setprecision(3) << m.sharpe << " & " << m.calmar << " & "
        << std::setprecision(2) << m.win_rate * 100 << " & " << m.drawdown * 100 << " & "
        << date_string(dates[m.peak]) << " & " << date_string(dates[m.trough]) << '\n';
}

} // namespace mvo
