#include "mvo/solver.hpp"
namespace mvo {
ConstraintReport validate_weight(const Vector &w, const Vector &p, double tau) {
    ConstraintReport r;
    if (!w.allFinite() || !p.allFinite()) {
        r.sum_weight_error = std::numeric_limits<double>::infinity();
        return r;
    }
    r.sum_weight_error = std::abs(w.sum() - 1);
    r.nonnegative_error = std::max(0.0, -w.minCoeff());
    r.turnover = (w - p).lpNorm<1>();
    r.turnover_error = std::max(0.0, r.turnover - tau);
    for (double g : {w.head<4>().sum(), w[4], w[5], w.tail<2>().sum()})
        r.group_violation = std::max({r.group_violation, .15 - g, g - .35});
    return r;
}
double objective(const Vector &w, const MomentEstimate &m, std::optional<double> risk,
                 bool legacy_ewm_div) {
    if (risk)
        return -m.mean.dot(w) + .5 * *risk * w.dot(m.covariance * w);
    double q = w.dot(m.covariance * w);
    if (q <= 0)
        return unavailable();
    Vector numerator = legacy_ewm_div ? m.covariance.diagonal().eval() : m.volatility;
    return -numerator.dot(w) / std::sqrt(q);
}

} // namespace mvo
