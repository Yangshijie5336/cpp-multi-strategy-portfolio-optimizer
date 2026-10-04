// Compatibility-only SLSQP. The convex primary solver is in solver.cpp.
#include "mvo/compatibility.hpp"
#include <numeric>
#include <array>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4505 4702 4701)
#endif
#define nlopt_isinf std::isinf
#define nlopt_isfinite std::isfinite
#include "../slsqp_core.hpp"
#undef nlopt_isinf
#undef nlopt_isfinite
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace mvo::legacy {
constexpr int N = 8, M = 18;
using Row = std::array<double, 8>;
using Matrix = std::array<Row, 8>;
struct Statistics {
    Row mean{}, stddev{}, variance{};
    Matrix cov{};
};
enum class SolverMode { Compatible, Analytic };
struct Options {
    int max_iterations = 1000;
    double ftol = 1e-6;
    std::optional<double> risk_averse;
    bool ewm = false;
    SolverMode solver = SolverMode::Compatible;
};
struct SolveInfo {
    Row weight{};
    int status = 0, iterations = 0, evaluations = 0;
    double objective = 0, max_violation = 0, sum_weight_error = 0, turnover_error = 0,
           group_violation = 0;
    bool fallback = false, retry = false, success = false;
    std::string message;
};
using Constraints = std::array<double, M>;
inline Constraints get_constrain(const Row &w, const Row &prev) {
    Constraints c{};
    c[0] = std::accumulate(w.begin(), w.end(), 0.0) - 1;
    for (int j = 0; j < N; ++j)
        c[1 + j] = w[j];
    double turnover = 0;
    for (int j = 0; j < N; ++j)
        turnover += std::abs(w[j] - prev[j]);
    c[9] = .1 - turnover;
    double g = w[0] + w[1] + w[2] + w[3];
    c[10] = g - .15;
    c[11] = .35 - g;
    c[12] = w[4] - .15;
    c[13] = .35 - w[4];
    c[14] = w[5] - .15;
    c[15] = .35 - w[5];
    g = w[6] + w[7];
    c[16] = g - .15;
    c[17] = .35 - g;
    return c;
}
inline double violation(const Row &w, const Row &prev) {
    for (double v : w)
        if (!std::isfinite(v))
            return std::numeric_limits<double>::infinity();
    auto c = get_constrain(w, prev);
    double v = std::abs(c[0]);
    for (int i = 1; i < M; ++i)
        v = std::max(v, -c[i]);
    return v;
}
inline void fill_constraint_diagnostics(SolveInfo &info, const Row &w, const Row &previous) {
    auto c = get_constrain(w, previous);
    info.sum_weight_error = std::abs(c[0]);
    double turnover = 0.0;
    for (int i = 0; i < N; ++i)
        turnover += std::abs(w[i] - previous[i]);
    info.turnover_error = std::max(0.0, turnover - .1);
    info.group_violation = 0.0;
    for (int i = 10; i < M; ++i)
        info.group_violation = std::max(info.group_violation, std::max(0.0, -c[i]));
    info.max_violation = violation(w, previous);
}

class SlsqpSolver {
    static constexpr int n1 = N + 1, mineq = M - 1 + 2 * n1;
    static constexpr int length = (3 * n1 + M) * (n1 + 1) + n1 * (mineq + 2) + 2 * mineq +
                                  (n1 + mineq) * (n1 - 1) + 2 + n1 * N / 2 + 2 * M + 3 * N +
                                  4 * n1 + 1;
    std::array<double, length + 32> work_{};
    std::array<int, mineq> iw_{};
    std::array<double, M *(N + 1)> jac_{};
    std::array<double, N + 1> gradient_{};
    Row lower_{}, upper_{};
    static double objective(const Row &w, const Statistics &s, const Options &o, Row *grad) {
        // Fused Sigma*w reused by objective and its analytic gradient.
        Row cw{};
        for (int a = 0; a < N; ++a)
            for (int b = 0; b < N; ++b)
                cw[a] += s.cov[a][b] * w[b];
        double q = 0;
        for (int j = 0; j < N; ++j)
            q += w[j] * cw[j];
        if (o.risk_averse) {
            double f = 0;
            for (int j = 0; j < N; ++j)
                f -= w[j] * s.mean[j];
            if (grad)
                for (int j = 0; j < N; ++j)
                    (*grad)[j] = -s.mean[j] + *o.risk_averse * cw[j];
            return f + .5 * *o.risk_averse * q;
        }
        const Row &v = o.ewm ? s.variance : s.stddev;
        double numerator = 0;
        for (int j = 0; j < N; ++j)
            numerator += w[j] * v[j];
        double root = std::sqrt(std::max(q, 1e-30));
        if (grad)
            for (int j = 0; j < N; ++j)
                (*grad)[j] = -v[j] / root + numerator * cw[j] / (root * root * root);
        return -numerator / root;
    }
    SolveInfo attempt(const Statistics &s, const Row &prev, const Options &o) {
        SolveInfo out;
        out.weight = prev;
        work_.fill(0);
        iw_.fill(0);
        jac_.fill(0);
        gradient_.fill(0);
        Constraints c;
        double f = 0;
        int m = M, meq = 1, la = M, n = N, lw = length + 32, li = mineq,
            iter = std::max(1, o.max_iterations - 1), mode = 0;
        double acc = o.ftol;
        slsqpb_state state{};
        auto evaluate = [&](bool grad) {
            Row g;
            f = objective(out.weight, s, o,
                          grad && o.solver == SolverMode::Analytic ? &g : nullptr);
            ++out.evaluations;
            c = get_constrain(out.weight, prev);
            if (grad) {
                constexpr double epsilon = 1.490116119384765625e-8;
                for (int j = 0; j < N; ++j) {
                    Row x = out.weight;
                    x[j] += epsilon;
                    double h = x[j] - out.weight[j];
                    auto cp = get_constrain(x, prev);
                    if (o.solver == SolverMode::Compatible) {
                        gradient_[j] = (objective(x, s, o, nullptr) - f) / h;
                        ++out.evaluations;
                    } else
                        gradient_[j] = g[j];
                    // The L1 kink uses the same forward secant as scipy approx_derivative.
                    for (int i = 0; i < M; ++i)
                        jac_[i + j * M] = (cp[i] - c[i]) / h;
                }
                gradient_[N] = 0;
            }
        };
        evaluate(true);
        int calls = 0;
        for (; calls < 100000; ++calls) {
            slsqp(&m, &meq, &la, &n, out.weight.data(), lower_.data(), upper_.data(), &f, c.data(),
                  gradient_.data(), jac_.data(), &acc, &iter, &mode, work_.data(), &lw, iw_.data(),
                  &li, &state);
            if (mode == 1)
                evaluate(false);
            else if (mode == -1 || mode == -2)
                evaluate(true);
            else
                break;
        }
        out.status = calls >= 100000 ? 9 : mode;
        out.iterations = iter;
        out.objective = objective(out.weight, s, o, nullptr);
        fill_constraint_diagnostics(out, out.weight, prev);
        out.success = (out.status == 0 && out.max_violation <= std::max(1e-6, 10 * o.ftol) &&
                       std::isfinite(out.objective));
        out.message = out.success ? "SLSQP converged" : "SLSQP did not pass validation";
        return out;
    }

  public:
    SlsqpSolver() {
        lower_.fill(-std::numeric_limits<double>::infinity());
        upper_.fill(std::numeric_limits<double>::infinity());
    }
    SolveInfo solve(const Statistics &s, const Row &prev, const Options &o) {
        if (!o.risk_averse && *std::max_element(s.variance.begin(), s.variance.end()) <= 0) {
            SolveInfo r;
            r.weight = prev;
            r.status = 5;
            r.fallback = true;
            return r;
        }
        auto r = attempt(s, prev, o);
        if (o.solver == SolverMode::Analytic && (r.status != 0 || r.max_violation > 1e-6)) {
            Options compatible = o;
            compatible.solver = SolverMode::Compatible;
            auto retry = attempt(s, prev, compatible);
            retry.evaluations += r.evaluations;
            retry.retry = true;
            r = retry;
        }
        if (r.max_violation > std::max(1e-6, 10 * o.ftol) || !std::isfinite(r.objective)) {
            r.weight = prev;
            r.fallback = true;
            r.success = false;
            r.max_violation = violation(prev, prev);
            r.objective = objective(prev, s, o, nullptr);
            r.message = "SLSQP fallback to previous valid weight";
        }
        return r;
    }
};

} // namespace mvo::legacy
namespace mvo {
SolveInfo solve_compatibility(const MomentEstimate &m, const Vector &previous,
                              std::optional<double> risk, bool ewm) {
    legacy::Statistics stats;
    legacy::Row p;
    for (int i = 0; i < 8; ++i) {
        p[i] = previous[i];
        stats.mean[i] = m.mean[i];
        stats.stddev[i] = m.volatility[i];
        stats.variance[i] = m.covariance(i, i);
        for (int j = 0; j < 8; ++j)
            stats.cov[i][j] = m.covariance(i, j);
    }
    legacy::Options options;
    options.risk_averse = risk;
    options.ewm = ewm;
    legacy::SlsqpSolver solver;
    auto solved = solver.solve(stats, p, options);
    SolveInfo result;
    for (int i = 0; i < 8; ++i)
        result.weight[i] = solved.weight[i];
    result.solver = SolverKind::SLSQP;
    result.success = solved.success;
    result.fallback = solved.fallback;
    result.retry = solved.retry;
    result.iterations = solved.iterations;
    result.objective = solved.objective;
    result.constraints = validate_weight(result.weight, previous);
    result.primal_residual = result.constraints.max_violation();
    result.message =
        solved.message + "; legacy finite-difference tolerance 1e-6; dual/KKT unavailable";
    return result;
}
} // namespace mvo
