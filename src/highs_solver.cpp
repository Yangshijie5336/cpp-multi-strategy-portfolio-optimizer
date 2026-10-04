#include "detail/optimization_model.hpp"
#include <Highs.h>
namespace mvo {
using namespace detail;
SolveInfo solve_primary(const MomentEstimate &m, const Vector &p, std::optional<double> risk,
                        const Options &o) {
    if (risk)
        require(std::isfinite(*risk) && *risk >= 0, "risk aversion must be nonnegative");
    Eigen::SelfAdjointEigenSolver<Covariance> eig(m.covariance);
    require(eig.info() == Eigen::Success && eig.eigenvalues().minCoeff() >= 0,
            "QP requires PSD covariance");
    auto data = build_model(m, p, risk, o);
    HighsModel model;
    auto &lp = model.lp_;
    lp.num_col_ = 16;
    lp.num_row_ = static_cast<HighsInt>(data.a.rows());
    lp.col_cost_.assign(data.c.data(), data.c.data() + 16);
    lp.col_lower_.assign(16, 0);
    lp.col_upper_.assign(16, kHighsInf);
    lp.row_lower_.assign(data.lower.data(), data.lower.data() + data.lower.size());
    lp.row_upper_.assign(data.upper.data(), data.upper.data() + data.upper.size());
    lp.a_matrix_.format_ = MatrixFormat::kRowwise;
    lp.a_matrix_.start_.clear();
    lp.a_matrix_.index_.clear();
    lp.a_matrix_.value_.clear();
    lp.a_matrix_.start_.push_back(0);
    for (Eigen::Index i = 0; i < data.a.rows(); ++i) {
        for (int j = 0; j < 16; ++j)
            if (data.a(i, j) != 0) {
                lp.a_matrix_.index_.push_back(j);
                lp.a_matrix_.value_.push_back(data.a(i, j));
            }
        lp.a_matrix_.start_.push_back(static_cast<HighsInt>(lp.a_matrix_.value_.size()));
    }
    if (!data.lp) {
        auto &h = model.hessian_;
        h.dim_ = 16;
        h.format_ = HessianFormat::kTriangular;
        h.start_.clear();
        h.index_.clear();
        h.value_.clear();
        h.start_.push_back(0);
        for (int j = 0; j < 16; ++j) {
            for (int i = j; i < 16; ++i)
                if (data.h(i, j) != 0) {
                    h.index_.push_back(i);
                    h.value_.push_back(data.h(i, j));
                }
            h.start_.push_back(static_cast<HighsInt>(h.value_.size()));
        }
    }
    Highs highs;
    auto option = [&](const char *name, const auto &value) {
        require(highs.setOptionValue(name, value) == HighsStatus::kOk,
                std::string("unsupported HiGHS option: ") + name);
    };
    option("output_flag", false);
    option("threads", 1);
    option("parallel", std::string("off"));
    option("primal_feasibility_tolerance", 1e-10);
    option("dual_feasibility_tolerance", 1e-10);
    option("ipm_optimality_tolerance", 1e-10);
    option("qp_regularization_value", 0.0);
    option("qp_iteration_limit", 10000);
    option("small_matrix_value", 1e-12);
    SolveInfo out;
    out.weight = p;
    out.solver = data.lp ? SolverKind::LP : SolverKind::QP;
    if (highs.passModel(model) == HighsStatus::kError) {
        out.message = "HiGHS rejected model";
        return out;
    }
    HighsSolution start;
    start.col_value.assign(data.start.data(), data.start.data() + 16);
    start.value_valid = true;
    highs.setSolution(start);
    const auto status = highs.run();
    const auto &info = highs.getInfo();
    const auto &sol = highs.getSolution();
    out.iterations =
        info.simplex_iteration_count + info.qp_iteration_count + info.ipm_iteration_count;
    out.message = highs.modelStatusToString(highs.getModelStatus());
    out.success = status != HighsStatus::kError &&
                  highs.getModelStatus() == HighsModelStatus::kOptimal && sol.value_valid &&
                  sol.dual_valid;
    if (sol.col_value.size() == 16 && sol.col_dual.size() == 16 &&
        sol.row_dual.size() == static_cast<size_t>(data.a.rows())) {
        Eigen::Map<const Eigen::VectorXd> x(sol.col_value.data(), 16), z(sol.col_dual.data(), 16),
            y(sol.row_dual.data(), data.a.rows());
        residuals(data, x, y, z, out);
        out.success = out.success && out.kkt_residual <= o.optimality_tolerance;
        extract(data, x, m, p, risk, o, out);
    } else
        out.success = false;
    if (!out.success)
        out.message += "; independent residual validation failed";
    return out;
}
} // namespace mvo
