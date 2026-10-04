#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace mvo {

struct ConvexSolveResult {
    std::vector<double> x;
    bool success = false;
    int iterations = 0;
    double objective = std::numeric_limits<double>::infinity();
    double primal_residual = std::numeric_limits<double>::infinity();
    double dual_residual = std::numeric_limits<double>::infinity();
    double kkt_residual = std::numeric_limits<double>::infinity();
    std::string message;
};

namespace convex_detail {

inline double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double result = 0.0;
    for (size_t i = 0; i < a.size(); ++i) result += a[i] * b[i];
    return result;
}

inline double norm_inf(const std::vector<double>& x) {
    double result = 0.0;
    for (double value : x) result = std::max(result, std::abs(value));
    return result;
}

inline bool solve_linear(std::vector<std::vector<double>> matrix,
                         std::vector<double> rhs,
                         std::vector<double>& solution) {
    const int n = static_cast<int>(rhs.size());
    if (static_cast<int>(matrix.size()) != n) return false;
    for (const auto& row : matrix) if (static_cast<int>(row.size()) != n) return false;
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        double pivot_abs = std::abs(matrix[col][col]);
        for (int row = col + 1; row < n; ++row) {
            if (std::abs(matrix[row][col]) > pivot_abs) {
                pivot = row;
                pivot_abs = std::abs(matrix[row][col]);
            }
        }
        if (!std::isfinite(pivot_abs) || pivot_abs <= 1e-14) return false;
        if (pivot != col) {
            std::swap(matrix[pivot], matrix[col]);
            std::swap(rhs[pivot], rhs[col]);
        }
        for (int row = col + 1; row < n; ++row) {
            const double factor = matrix[row][col] / matrix[col][col];
            if (factor == 0.0) continue;
            matrix[row][col] = 0.0;
            for (int j = col + 1; j < n; ++j) matrix[row][j] -= factor * matrix[col][j];
            rhs[row] -= factor * rhs[col];
        }
    }
    solution.assign(n, 0.0);
    for (int row = n - 1; row >= 0; --row) {
        double value = rhs[row];
        for (int j = row + 1; j < n; ++j) value -= matrix[row][j] * solution[j];
        if (std::abs(matrix[row][row]) <= 1e-14) return false;
        solution[row] = value / matrix[row][row];
        if (!std::isfinite(solution[row])) return false;
    }
    return true;
}

} // namespace convex_detail

// Dense active-set solver for the small strictly convex LP/QP models used here.
// LPs receive a tiny diagonal regularizer from the caller, so the same KKT
// implementation handles both cases without a separate simplex code path.
class ActiveSetQpSolver {
public:
    ConvexSolveResult solve(const std::vector<std::vector<double>>& h,
                            const std::vector<double>& f,
                            const std::vector<std::vector<double>>& a,
                            const std::vector<double>& b,
                            const std::vector<std::vector<double>>& equality,
                            const std::vector<double>& equality_rhs,
                            const std::vector<double>& initial,
                            const std::vector<int>& initial_active,
                            int max_iterations = 2000,
                            double tolerance = 1e-10) const {
        using convex_detail::dot;
        using convex_detail::norm_inf;
        ConvexSolveResult result;
        const int n = static_cast<int>(f.size());
        const int me = static_cast<int>(equality.size());
        if (n == 0 || static_cast<int>(h.size()) != n || static_cast<int>(initial.size()) != n ||
            static_cast<int>(equality_rhs.size()) != me || static_cast<int>(a.size()) != static_cast<int>(b.size())) {
            result.message = "invalid convex model dimensions";
            return result;
        }
        for (const auto& row : h) if (static_cast<int>(row.size()) != n) { result.message = "invalid Hessian"; return result; }
        for (const auto& row : a) if (static_cast<int>(row.size()) != n) { result.message = "invalid inequality matrix"; return result; }
        for (const auto& row : equality) if (static_cast<int>(row.size()) != n) { result.message = "invalid equality matrix"; return result; }

        std::vector<double> x = initial;
        std::vector<int> active;
        for (int index : initial_active) {
            if (index < 0 || index >= static_cast<int>(a.size())) continue;
            if (std::find(active.begin(), active.end(), index) == active.end()) active.push_back(index);
        }

        auto gradient = [&](const std::vector<double>& point) {
            std::vector<double> g(n, 0.0);
            for (int i = 0; i < n; ++i) {
                g[i] = f[i];
                for (int j = 0; j < n; ++j) g[i] += h[i][j] * point[j];
            }
            return g;
        };
        auto primal = [&](const std::vector<double>& point) {
            double value = 0.0;
            for (size_t i = 0; i < a.size(); ++i) value = std::max(value, dot(a[i], point) - b[i]);
            for (int i = 0; i < me; ++i) value = std::max(value, std::abs(dot(equality[i], point) - equality_rhs[i]));
            return std::max(0.0, value);
        };
        auto build_kkt = [&](const std::vector<int>& active_indices,
                             const std::vector<double>& rhs_gradient,
                             bool direction,
                             std::vector<std::vector<double>>& matrix,
                             std::vector<double>& rhs) {
            const int mc = me + static_cast<int>(active_indices.size());
            matrix.assign(n + mc, std::vector<double>(n + mc, 0.0));
            rhs.assign(n + mc, 0.0);
            for (int i = 0; i < n; ++i) {
                rhs[i] = -rhs_gradient[i];
                for (int j = 0; j < n; ++j) matrix[i][j] = h[i][j];
            }
            for (int i = 0; i < me; ++i) {
                for (int j = 0; j < n; ++j) {
                    matrix[j][n + i] = matrix[n + i][j] = equality[i][j];
                }
                rhs[n + i] = direction ? 0.0 : equality_rhs[i];
            }
            for (size_t q = 0; q < active_indices.size(); ++q) {
                const int row = active_indices[q];
                const int k = me + static_cast<int>(q);
                for (int j = 0; j < n; ++j) matrix[j][n + k] = matrix[n + k][j] = a[row][j];
                rhs[n + k] = direction ? 0.0 : b[row];
            }
        };

        std::vector<double> multipliers;
        bool converged = false;
        for (int iteration = 0; iteration < max_iterations; ++iteration) {
            result.iterations = iteration + 1;
            std::vector<double> g = gradient(x);
            std::vector<std::vector<double>> kkt;
            std::vector<double> rhs, solution;
            build_kkt(active, g, true, kkt, rhs);
            if (!convex_detail::solve_linear(kkt, rhs, solution)) {
                if (!active.empty()) { active.pop_back(); continue; }
                result.message = "singular KKT system";
                break;
            }
            std::vector<double> direction(solution.begin(), solution.begin() + n);
            const double direction_norm = norm_inf(direction);
            if (direction_norm <= tolerance) {
                build_kkt(active, g, false, kkt, rhs);
                if (!convex_detail::solve_linear(kkt, rhs, solution)) {
                    if (!active.empty()) { active.pop_back(); continue; }
                    result.message = "singular multiplier system";
                    break;
                }
                multipliers.assign(solution.begin() + n, solution.end());
                int remove_position = -1;
                double most_negative = -tolerance;
                for (size_t q = 0; q < active.size(); ++q) {
                    const double multiplier = multipliers[me + static_cast<int>(q)];
                    if (multiplier < most_negative) {
                        most_negative = multiplier;
                        remove_position = static_cast<int>(q);
                    }
                }
                if (remove_position >= 0) {
                    active.erase(active.begin() + remove_position);
                    continue;
                }
                converged = primal(x) <= 10.0 * tolerance;
                if (converged) break;
                result.message = "active set is feasible but not stationary";
                break;
            }

            double step = 1.0;
            int blocking = -1;
            for (int i = 0; i < static_cast<int>(a.size()); ++i) {
                if (std::find(active.begin(), active.end(), i) != active.end()) continue;
                const double ap = dot(a[i], direction);
                if (ap <= tolerance) continue;
                const double candidate = (b[i] - dot(a[i], x)) / ap;
                if (candidate < step) {
                    step = std::max(0.0, candidate);
                    blocking = i;
                }
            }
            for (int i = 0; i < n; ++i) x[i] += step * direction[i];
            if (blocking >= 0 && step < 1.0 - tolerance) active.push_back(blocking);
        }

        result.x = x;
        result.primal_residual = primal(x);
        std::vector<double> g = gradient(x);
        std::vector<double> stationarity = g;
        std::vector<double> lambda;
        std::vector<std::vector<double>> kkt;
        std::vector<double> rhs, solution;
        build_kkt(active, g, false, kkt, rhs);
        if (convex_detail::solve_linear(kkt, rhs, solution)) {
            lambda.assign(solution.begin() + n, solution.end());
            for (int i = 0; i < me; ++i) for (int j = 0; j < n; ++j) stationarity[j] += equality[i][j] * lambda[i];
            for (size_t q = 0; q < active.size(); ++q) for (int j = 0; j < n; ++j) stationarity[j] += a[active[q]][j] * lambda[me + static_cast<int>(q)];
        }
        result.dual_residual = norm_inf(stationarity);
        result.kkt_residual = std::max(result.primal_residual, result.dual_residual);
        result.objective = 0.0;
        for (int i = 0; i < n; ++i) {
            result.objective += f[i] * x[i];
            for (int j = 0; j < n; ++j) result.objective += 0.5 * x[i] * h[i][j] * x[j];
        }
        result.success = converged && std::isfinite(result.objective) && result.kkt_residual <= 1e-7;
        if (result.success) result.message = "active-set KKT converged";
        else if (result.message.empty()) result.message = "convex solver did not meet KKT tolerance";
        return result;
    }
};

} // namespace mvo
