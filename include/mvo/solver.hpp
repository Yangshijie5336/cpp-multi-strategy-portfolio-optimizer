#pragma once
#include "moments.hpp"
namespace mvo {
ConstraintReport validate_weight(const Vector &w, const Vector &previous, double turnover = .1);
double objective(const Vector &w, const MomentEstimate &m, std::optional<double> risk,
                 bool legacy_ewm_div = false);
SolveInfo solve_primary(const MomentEstimate &m, const Vector &previous, std::optional<double> risk,
                        const Options &options);
SolveInfo solve_slsqp(const MomentEstimate &m, const Vector &previous, std::optional<double> risk,
                      const Options &options, bool legacy_ewm_div = false);
SolveInfo optimize(const MomentEstimate &m, const Vector &previous, std::optional<double> risk,
                   const Options &options, Timing *timing = nullptr);
} // namespace mvo
