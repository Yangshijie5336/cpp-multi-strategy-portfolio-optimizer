#include "mvo/preprocessing.hpp"
#include "detail/compensated.hpp"
namespace mvo {
using detail::Compensated;
Samples preprocess(const Samples &x, int window, double sigma) {
    require(window >= 2 && std::isfinite(sigma) && sigma >= 0, "invalid preprocessing parameters");
    Samples out = x;
    for (const auto &row : x)
        require(row.allFinite(), "nonfinite preprocessing input");
    for (size_t t = static_cast<size_t>(window); t < x.size(); ++t) {
        for (int j = 0; j < assets; ++j) {
            Compensated sum;
            for (size_t k = t - window; k < t; ++k)
                sum.add(out[k][j]);
            double mean = sum.sum / window;
            Compensated ss;
            for (size_t k = t - window; k < t; ++k) {
                double d = out[k][j] - mean;
                ss.add(d * d);
            }
            double sd = std::sqrt(ss.sum / (window - 1));
            out[t][j] = std::clamp(out[t][j], mean - sigma * sd, mean + sigma * sd);
        }
    }
    return out;
}

} // namespace mvo
