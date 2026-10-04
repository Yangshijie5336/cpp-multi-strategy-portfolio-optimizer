#pragma once
#include "io.hpp"
#include "moments.hpp"
#include "solver.hpp"
#include "evaluation.hpp"
namespace mvo {
PortfolioResult run_portfolio(const Samples &daily, const std::vector<RebalanceMoments> &moments,
                              const Options &options, Timing *timing = nullptr);
}
