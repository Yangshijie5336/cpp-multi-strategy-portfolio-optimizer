#include "detail/optimization_model.hpp"
#include <Highs.h>
namespace mvo::detail {
Model build_model(const MomentEstimate &m, const Vector &p, std::optional<double> risk,
                  const Options &o) {
    require(m.mean.allFinite() && m.covariance.allFinite() && m.volatility.allFinite(),
            "nonfinite solver input");
    require(validate_weight(p, p, o.turnover).max_violation() <= o.feasibility_tolerance,
            "previous weight is invalid");
    Model model;
    model.div = !risk;
    model.lp = risk && *risk == 0;
    model.h = Eigen::MatrixXd::Zero(16, 16);
    model.c = Eigen::VectorXd::Zero(16);
    model.start = Eigen::VectorXd::Zero(16);
    std::vector<Eigen::VectorXd> rows;
    std::vector<double> lower, upper;
    auto add = [&](const Eigen::VectorXd &r, double lo, double hi) {
        rows.push_back(r);
        lower.push_back(lo);
        upper.push_back(hi);
    };
    const double inf = kHighsInf;
    Eigen::VectorXd row = Eigen::VectorXd::Zero(16);
    if (model.div) {
        const double ref = m.volatility.maxCoeff();
        require(ref > 0, "zero volatility normalization");
        row.head<8>() = m.volatility / ref;
        add(row, 1, 1);
        model.start.head<8>() = p / (m.volatility.dot(p) / ref);
        model.h.topLeftCorner<8, 8>() = 2 * m.covariance / (ref * ref);
    } else {
        model.start.head<8>() = p;
        row.head<8>().setOnes();
        add(row, 1, 1);
        model.c.head<8>() = -m.mean;
        if (!model.lp)
            model.h.topLeftCorner<8, 8>() = *risk * m.covariance;
    }
    for (int g = 0; g < 4; ++g) {
        row.setZero();
        if (g == 0)
            row.head<4>().setOnes();
        else if (g == 1)
            row[4] = 1;
        else if (g == 2)
            row[5] = 1;
        else
            row.segment<2>(6).setOnes();
        if (model.div) {
            auto lo = row;
            lo.head<8>().array() -= .15;
            add(lo, 0, inf);
            auto hi = row;
            hi.head<8>().array() -= .35;
            add(hi, -inf, 0);
        } else
            add(row, .15, .35);
    }
    for (int i = 0; i < 8; ++i) {
        row.setZero();
        row[i] = 1;
        row[8 + i] = -1;
        if (model.div)
            row.head<8>().array() -= p[i];
        add(row, -inf, model.div ? 0 : p[i]);
        row.setZero();
        row[i] = -1;
        row[8 + i] = -1;
        if (model.div)
            row.head<8>().array() += p[i];
        add(row, -inf, model.div ? 0 : -p[i]);
    }
    row.setZero();
    row.tail<8>().setOnes();
    if (model.div)
        row.head<8>().setConstant(-o.turnover);
    add(row, -inf, model.div ? 0 : o.turnover);
    const auto count = static_cast<Eigen::Index>(rows.size());
    model.a.resize(count, 16);
    model.lower.resize(count);
    model.upper.resize(count);
    for (Eigen::Index i = 0; i < count; ++i) {
        model.a.row(i) = rows[static_cast<size_t>(i)];
        model.lower[i] = lower[static_cast<size_t>(i)];
        model.upper[i] = upper[static_cast<size_t>(i)];
    }
    model.scale = std::max(model.c.cwiseAbs().maxCoeff(), model.h.cwiseAbs().maxCoeff());
    if (model.scale == 0)
        model.scale = 1;
    model.h /= model.scale;
    model.c /= model.scale;
    return model;
}
bool finite_bound(double x) {
    return std::abs(x) < kHighsInf;
}
void residuals(const Model &m, const Eigen::VectorXd &x, const Eigen::VectorXd &yd,
               const Eigen::VectorXd &zd, SolveInfo &r) {
    Eigen::VectorXd ax = m.a * x;
    double primal = std::max(0.0, -x.minCoeff()), dual = 0, comp = 0;
    for (Eigen::Index i = 0; i < ax.size(); ++i) {
        if (finite_bound(m.lower[i]))
            primal = std::max(primal, m.lower[i] - ax[i]);
        if (finite_bound(m.upper[i]))
            primal = std::max(primal, ax[i] - m.upper[i]);
        if (m.lower[i] == m.upper[i])
            continue;
        if (yd[i] > 0) {
            if (!finite_bound(m.lower[i]))
                dual = std::max(dual, yd[i]);
            else
                comp = std::max(comp, std::abs(yd[i] * (ax[i] - m.lower[i])));
        } else {
            if (!finite_bound(m.upper[i]))
                dual = std::max(dual, -yd[i]);
            else
                comp = std::max(comp, std::abs(yd[i] * (m.upper[i] - ax[i])));
        }
    }
    for (Eigen::Index i = 0; i < x.size(); ++i) {
        dual = std::max(dual, -zd[i]);
        comp = std::max(comp, std::abs(x[i] * zd[i]));
    }
    double stationarity = (m.h * x + m.c - m.a.transpose() * yd - zd).lpNorm<Eigen::Infinity>();
    r.primal_residual = primal;
    r.dual_residual = std::max(dual, stationarity);
    r.complementarity = comp;
    r.kkt_residual = std::max({primal, r.dual_residual, comp});
}
void extract(const Model &model, const Eigen::VectorXd &x, const MomentEstimate &moments,
             const Vector &p, std::optional<double> risk, const Options &o, SolveInfo &r) {
    if (x.size() != 16 || !x.allFinite()) {
        r.success = false;
        r.message += "; nonfinite solution";
        return;
    }
    r.weight = x.head<8>();
    if (model.div) {
        double s = r.weight.sum();
        if (s <= 0) {
            r.success = false;
            r.message += "; invalid diversification normalization";
            return;
        }
        r.weight /= s;
    }
    r.constraints = validate_weight(r.weight, p, o.turnover);
    r.objective = objective(r.weight, moments, risk);
    r.success = r.success && r.constraints.max_violation() <= o.feasibility_tolerance &&
                std::isfinite(r.objective);
}
} // namespace mvo::detail
