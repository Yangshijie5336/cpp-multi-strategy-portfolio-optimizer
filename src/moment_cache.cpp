#include "mvo/moments.hpp"
#include "mvo/preprocessing.hpp"
namespace mvo {
std::vector<RebalanceMoments> prepare(const Samples &daily, const Options &options,
                                      Timing *timing) {
    require(options.window >= 2 && options.keep >= 1 && options.keep < options.window,
            "require 1<=keep<window");
    Samples processed;
    {
        ScopedTimer t(timing, "preprocess");
        processed = preprocess(daily, options.window, options.sigma);
    }
    const double reg_before = timing ? timing->ms["covariance_regularization"] : 0;
    const auto start = Timing::Clock::now();
    std::vector<RebalanceMoments> moments;
    moments.reserve(daily.size() / options.keep);
    for (size_t t = static_cast<size_t>(options.window); t < daily.size(); t += options.keep) {
        if (options.accuracy == AccuracyMode::HighAccuracy) {
            moments.push_back(
                {t,
                 horizon_moments(processed, t - options.window, t, options.keep, options.window,
                                 false, options.eigenvalue_relative_floor, timing),
                 horizon_moments(processed, t - options.window, t, options.keep, options.window,
                                 true, options.eigenvalue_relative_floor, timing)});
        } else {
            Samples rolled;
            for (size_t k = t - options.window + options.keep - 1; k < t; ++k) {
                Vector v = Vector::Zero();
                for (size_t q = k - options.keep + 1; q <= k; ++q)
                    v += processed[q];
                rolled.push_back(v);
            }
            moments.push_back({t, ordinary_moments(rolled, 0, rolled.size()),
                               ewm_moments(rolled, 0, rolled.size(), options.window)});
        }
    }
    if (timing)
        timing->ms["moment_estimation"] +=
            std::chrono::duration<double, std::milli>(Timing::Clock::now() - start).count() -
            (timing->ms["covariance_regularization"] - reg_before);
    return moments;
}
} // namespace mvo
