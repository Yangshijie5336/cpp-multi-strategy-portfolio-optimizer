#pragma once
#include "types.hpp"
#include "preprocessing.hpp"
namespace mvo {
MomentEstimate ordinary_moments(const Samples &x, size_t begin, size_t end);
MomentEstimate ewm_moments(const Samples &x, size_t begin, size_t end, int span);
void regularize(MomentEstimate &moments, double alpha, double relative_floor,
                Timing *timing = nullptr);
double oas_alpha(const Covariance &mle, double n_eff);
MomentEstimate horizon_moments(const Samples &x, size_t begin, size_t end, int horizon, int span,
                               bool ewm, double floor, Timing *timing = nullptr);
std::vector<RebalanceMoments> prepare(const Samples &daily, const Options &options,
                                      Timing *timing = nullptr);
} // namespace mvo
