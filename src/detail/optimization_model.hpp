#pragma once
#include "mvo/solver.hpp"
namespace mvo::detail {
struct Model {
    Eigen::MatrixXd h, a;
    Eigen::VectorXd c, lower, upper, start;
    double scale = 1;
    bool div = false, lp = false;
};
Model build_model(const MomentEstimate &, const Vector &, std::optional<double>, const Options &);
bool finite_bound(double);
void residuals(const Model &, const Eigen::VectorXd &, const Eigen::VectorXd &,
               const Eigen::VectorXd &, SolveInfo &);
void extract(const Model &, const Eigen::VectorXd &, const MomentEstimate &, const Vector &,
             std::optional<double>, const Options &, SolveInfo &);
} // namespace mvo::detail
