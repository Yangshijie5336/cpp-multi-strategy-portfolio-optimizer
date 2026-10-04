#pragma once
#include "solver.hpp"
namespace mvo {
SolveInfo solve_compatibility(const MomentEstimate &m, const Vector &previous,
                              std::optional<double> risk, bool ewm);
}
