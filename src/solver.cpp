#include "mvo/solver.hpp"
#include "mvo/moments.hpp"
namespace mvo {
SolveInfo optimize(const MomentEstimate &m, const Vector &p, std::optional<double> risk,
                   const Options &o, Timing *timing) {
    SolveInfo result;
    {
        ScopedTimer timer(timing, risk && *risk == 0 ? "LP_solve" : "QP_solve");
        result = solve_primary(m, p, risk, o);
    }
#ifdef MVO_VALIDATE_SOLVERS
    if (result.success) {
        auto check = solve_slsqp(m, p, risk, o);
        if (!check.success || std::abs(check.objective - result.objective) > 1e-7 ||
            check.constraints.max_violation() > o.feasibility_tolerance)
            result.message += "; SLSQP cross-check mismatch";
    }
#endif
    if (result.success)
        return result;
    const std::string primary = result.message;
    MomentEstimate repaired = m;
    if (!(risk && *risk == 0))
        regularize(repaired, 0, o.eigenvalue_relative_floor * 10, timing);
    {
        ScopedTimer timer(timing, risk && *risk == 0 ? "LP_solve" : "QP_solve");
        result = solve_primary(repaired, p, risk, o);
    }
    result.retry = true;
    result.message = "retry: " + primary + "; " + result.message;
    if (result.success)
        return result;
    const std::string retry = result.message;
    {
        ScopedTimer timer(timing, "SLSQP_fallback");
        result = solve_slsqp(repaired, p, risk, o);
    }
    result.retry = true;
    result.fallback = true;
    result.message = retry + "; " + result.message;
    if (!result.success) {
        result.weight = p;
        result.solver = SolverKind::PreviousWeight;
        result.constraints = validate_weight(p, p, o.turnover);
        result.objective = objective(p, m, risk);
        result.message += "; previous valid weight retained";
    }
    return result;
}
} // namespace mvo
