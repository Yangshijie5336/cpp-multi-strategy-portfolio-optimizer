#include "mvo/app.hpp"
#include "mvo/cli.hpp"
#include "mvo/portfolio.hpp"
#include <iomanip>
#include <iostream>

namespace mvo {

int run_cli(int argc, char **argv) {
    try {
        const auto command = parse_command_line(argc, argv);
        const auto &input = command.input_path;
        const auto &base = command.options;

        Timing timing;
        const auto total_start = Timing::Clock::now();
        {
            SheetData sheet;
            {
                ScopedTimer t(&timing, "XLS_read");
                sheet = read_xls(input, base.missing);
            }
            Samples daily;
            {
                ScopedTimer t(&timing, "daily_return_conversion");
                daily = convert_daily(sheet.values, base.input, base.capital);
            }
            auto moments = prepare(daily, base, &timing);
            const std::vector<std::pair<const char *, std::optional<double>>> risks = {
                {"最大分散", std::nullopt}, {"最大目标收益率", 0}, {"风险厌恶-20", 20}};
            int all_fallback = 0, all_retry = 0, all_failure = 0;
            for (auto [name, risk] : risks) {
                for (bool ewm : {false, true}) {
                    Options options = base;
                    options.risk_averse = risk;
                    options.ewm = ewm;
                    auto result = run_portfolio(daily, moments, options, &timing);
                    auto metrics = Evaluator::evaluate(result.pnl, sheet.dates, options.yr_dates);
                    std::cout << name << "/" << (ewm ? "指数加权" : "等权") << "\t";
                    Evaluator::print(metrics, sheet.dates, std::cout);
                    if (base.debug && !result.solves.empty()) {
                        const auto &solved = result.solves.back();
                        std::cerr << name << "/" << (ewm ? "ewm" : "ordinary")
                                  << " solver=" << solver_name(solved.solver)
                                  << " success=" << solved.success
                                  << " fallback=" << solved.fallback << " retry=" << solved.retry
                                  << " msg=" << solved.message << " kkt=" << solved.kkt_residual
                                  << " feas=" << solved.constraints.max_violation() << '\n';
                    }
                    all_fallback += result.fallback_count;
                    all_retry += result.retry_count;
                    all_failure += result.solver_failure_count;
                }
            }
            std::vector<double> baseline(daily.size(), 1);
            for (size_t t = 0; t < daily.size(); ++t)
                baseline[t] = (t ? baseline[t - 1] : 1) + base.initial.dot(daily[t]);
            std::cout << "初始权重，基准组合\t";
            Evaluator::print(Evaluator::evaluate(baseline, sheet.dates, base.yr_dates), sheet.dates,
                             std::cout);
            std::cout << "solver_fallback=" << all_fallback << " retry=" << all_retry
                      << " failure=" << all_failure << '\n';
        }
        timing.ms["total"] =
            std::chrono::duration<double, std::milli>(Timing::Clock::now() - total_start).count();
        for (const auto &[name, milliseconds] : timing.ms)
            std::cout << "time_" << name << "_ms=" << std::setprecision(3) << milliseconds << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

} // namespace mvo
