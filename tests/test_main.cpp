#include "mvo/solver.hpp"
#include "mvo/cli.hpp"
#include "mvo/data.hpp"
#include "mvo/dates.hpp"
#include "mvo/evaluation.hpp"
#include <iostream>
using namespace mvo;
int main() {
    try {
        Options options;
        options.accuracy = AccuracyMode::HighAccuracy;
        Vector previous = options.initial;
        Samples identical(80, Vector::Zero());
        for (auto &row : identical)
            row.setConstant(.001);
        auto ordinary = ordinary_moments(identical, 0, identical.size());
        regularize(ordinary, oas_alpha(ordinary.covariance, 80), 1e-12);
        require(ordinary.covariance.allFinite(), "covariance must be finite");
        Eigen::SelfAdjointEigenSolver<Covariance> eig(ordinary.covariance);
        require(eig.eigenvalues().minCoeff() > 0,
                "regularization must produce positive eigenvalues");
        require(validate_weight(previous, previous).max_violation() < 1e-12,
                "initial weights invalid");
        // A zero-turnover constraint fixes the solution at the known initial weights.
        // Exercise the shared model and both HiGHS objective types in Release too.
        MomentEstimate model;
        model.mean = Vector::LinSpaced(8, 0.01, 0.08);
        model.covariance = Covariance::Identity();
        model.volatility = Vector::Ones();
        options.turnover = 0;
        for (const std::optional<double> risk :
             {std::optional<double>{0}, std::optional<double>{20}, std::optional<double>{}}) {
            auto solved = solve_primary(model, previous, risk, options);
            require(solved.success, "HiGHS failed: " + solved.message);
            require((solved.weight - previous).lpNorm<Eigen::Infinity>() < 1e-8,
                    "zero turnover violated");
            require(solved.kkt_residual <= options.optimality_tolerance, "KKT residual too large");
        }
        require(date_string(date_serial("2021-07-22")) == "20210722", "date roundtrip failed");
        Samples values{Vector::Constant(2), Vector::Constant(3)};
        require(convert_daily(values, InputType::NAV)[1].isApprox(Vector::Constant(.5)),
                "NAV conversion failed");
        require(convert_daily(values, InputType::CumulativePnL)[1].isApprox(Vector::Ones()),
                "PnL conversion failed");
        const std::vector<double> dates{44197, 44198, 44199};
        auto metrics = Evaluator::evaluate({1, 1.1, 1.05}, dates, 250);
        require(std::abs(metrics.drawdown - .05) < 1e-12 && metrics.peak == 1 &&
                    metrics.trough == 2,
                "drawdown failed");
        char exe[] = "mvo", path[] = "sample.xls", flag[] = "--compat";
        char *args[]{exe, path, flag};
        auto cli = parse_command_line(3, args);
        require(cli.input_path == "sample.xls" &&
                    cli.options.accuracy == AccuracyMode::PythonCompatible,
                "CLI parsing failed");
        std::cout << "module regression passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
