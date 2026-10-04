#include "detail/optimization_model.hpp"
#include <nlopt.hpp>
namespace mvo {
using namespace detail;
namespace {
struct NlContext {
    const Model *m;
};
double nl_objective(const std::vector<double> &x, std::vector<double> &grad, void *ptr) {
    const auto &m = *static_cast<NlContext *>(ptr)->m;
    Eigen::Map<const Eigen::VectorXd> v(x.data(), static_cast<Eigen::Index>(x.size()));
    if (!grad.empty())
        Eigen::Map<Eigen::VectorXd>(grad.data(), v.size()) = m.h * v + m.c;
    return .5 * v.dot(m.h * v) + m.c.dot(v);
}
struct NlRow {
    Eigen::VectorXd row;
    double rhs;
};
double nl_constraint(const std::vector<double> &x, std::vector<double> &grad, void *ptr) {
    const auto &r = *static_cast<NlRow *>(ptr);
    if (!grad.empty())
        std::copy(r.row.data(), r.row.data() + r.row.size(), grad.begin());
    return r.row.dot(Eigen::Map<const Eigen::VectorXd>(x.data(), r.row.size())) - r.rhs;
}
} // namespace
SolveInfo solve_slsqp(const MomentEstimate &m, const Vector &p, std::optional<double> risk,
                      const Options &o, bool legacy_ewm_div) {
    (void)legacy_ewm_div;
    auto data = build_model(m, p, risk, o);
    NlContext context{&data};
    nlopt::opt solver(nlopt::LD_SLSQP, 16);
    solver.set_lower_bounds(std::vector<double>(16, 0));
    solver.set_min_objective(nl_objective, &context);
    solver.set_maxeval(20000);
    solver.set_ftol_abs(1e-12);
    solver.set_xtol_rel(1e-12);
    std::vector<NlRow> equal, ineq;
    equal.reserve(4);
    ineq.reserve(60);
    for (Eigen::Index i = 0; i < data.a.rows(); ++i) {
        if (data.lower[i] == data.upper[i])
            equal.push_back({data.a.row(i).transpose(), data.lower[i]});
        else {
            if (finite_bound(data.lower[i]))
                ineq.push_back({-data.a.row(i).transpose(), -data.lower[i]});
            if (finite_bound(data.upper[i]))
                ineq.push_back({data.a.row(i).transpose(), data.upper[i]});
        }
    }
    for (auto &row : equal)
        solver.add_equality_constraint(nl_constraint, &row, 1e-11);
    for (auto &row : ineq)
        solver.add_inequality_constraint(nl_constraint, &row, 1e-11);
    std::vector<double> x(data.start.data(), data.start.data() + 16);
    double f = 0;
    SolveInfo out;
    out.weight = p;
    out.solver = SolverKind::SLSQP;
    out.fallback = true;
    try {
        auto code = solver.optimize(x, f);
        out.success = code > 0 && code != nlopt::MAXEVAL_REACHED && code != nlopt::MAXTIME_REACHED;
        out.message = "NLopt status " + std::to_string(code);
    } catch (const std::exception &e) {
        out.message = e.what();
    }
    out.iterations = solver.get_numevals();
    Eigen::Map<const Eigen::VectorXd> v(x.data(), 16);
    extract(data, v, m, p, risk, o, out);
    out.primal_residual = out.constraints.max_violation();
    out.message += "; dual/KKT unavailable (not zero)";
    return out;
}

} // namespace mvo
