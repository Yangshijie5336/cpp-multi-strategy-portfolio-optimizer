#include "mvo/moments.hpp"
#include "detail/compensated.hpp"
namespace mvo {
using detail::Compensated;
namespace {
void finish(MomentEstimate &m) {
    require(m.mean.allFinite() && m.covariance.allFinite(), "nonfinite moments");
    m.covariance = (.5 * (m.covariance + m.covariance.transpose())).eval();
    m.volatility = m.covariance.diagonal().cwiseMax(0).cwiseSqrt();
}
void range(const Samples &x, size_t b, size_t e) {
    require(b < e && e <= x.size() && e - b >= 2, "covariance requires >=2 samples");
    for (size_t i = b; i < e; ++i)
        require(x[i].allFinite(), "nonfinite sample");
}
} // namespace
MomentEstimate ordinary_moments(const Samples &x, size_t b, size_t e) {
    range(x, b, e);
    MomentEstimate m;
    double n = static_cast<double>(e - b);
    for (int j = 0; j < assets; ++j) {
        Compensated sum;
        for (size_t t = b; t < e; ++t)
            sum.add(x[t][j]);
        m.mean[j] = sum.sum / n;
    }
    for (int i = 0; i < assets; ++i)
        for (int j = 0; j <= i; ++j) {
            Compensated ss;
            for (size_t t = b; t < e; ++t)
                ss.add((x[t][i] - m.mean[i]) * (x[t][j] - m.mean[j]));
            m.covariance(i, j) = m.covariance(j, i) = ss.sum / (n - 1);
        }
    m.effective_sample_size = n;
    finish(m);
    return m;
}
MomentEstimate ewm_moments(const Samples &x, size_t b, size_t e, int span) {
    range(x, b, e);
    require(span >= 1, "invalid EWM span");
    MomentEstimate m;
    m.mean = x[b];
    double a = 2.0 / (span + 1), d = 1 - a, sumw2 = 1;
    for (size_t t = b + 1; t < e; ++t) {
        Vector diff = x[t] - m.mean;
        m.mean = d * m.mean + a * x[t];
        m.covariance = d * (m.covariance + a * diff * diff.transpose()).eval();
        sumw2 = d * d * sumw2 + a * a;
    }
    require(sumw2 < 1, "EWM effective sample size must exceed 1");
    m.covariance /= 1 - sumw2;
    m.effective_sample_size = 1 / sumw2;
    finish(m);
    return m;
}
double oas_alpha(const Covariance &mle, double n_eff) {
    // Chen et al., OAS eq. (23): finite-dimensional (2/p) correction retained.
    const double p = assets, tr = mle.trace(), tr2 = mle.squaredNorm();
    const double numerator = (1 - 2 / p) * tr2 + tr * tr;
    const double denominator = (n_eff + 1 - 2 / p) * (tr2 - tr * tr / p);
    if (denominator <= 0)
        return 1;
    return std::clamp(numerator / denominator, 0.0, 1.0);
}
void regularize(MomentEstimate &m, double alpha, double relative_floor, Timing *timing) {
    ScopedTimer timer(timing, "covariance_regularization");
    require(m.covariance.allFinite() && std::isfinite(alpha) && alpha >= 0 && alpha <= 1 &&
                relative_floor > 0,
            "invalid covariance regularization");
    m.covariance = (.5 * (m.covariance + m.covariance.transpose())).eval();
    Eigen::SelfAdjointEigenSolver<Covariance> before(m.covariance);
    require(before.info() == Eigen::Success, "covariance eigensolver failed");
    m.min_eigen_before = before.eigenvalues().minCoeff();
    double top = before.eigenvalues().maxCoeff();
    m.condition_before =
        m.min_eigen_before > 0 ? top / m.min_eigen_before : std::numeric_limits<double>::infinity();
    double mu = std::max(0.0, m.covariance.trace() / assets);
    m.covariance = (1 - alpha) * m.covariance + alpha * mu * Covariance::Identity();
    m.shrinkage_alpha = alpha;
    Eigen::SelfAdjointEigenSolver<Covariance> eig(m.covariance);
    require(eig.info() == Eigen::Success, "shrunk covariance eigensolver failed");
    const double floor = relative_floor * std::max(1.0, eig.eigenvalues().maxCoeff());
    Vector eigen = eig.eigenvalues().cwiseMax(floor);
    m.covariance =
        (eig.eigenvectors() * eigen.asDiagonal() * eig.eigenvectors().transpose()).eval();
    finish(m);
    Eigen::SelfAdjointEigenSolver<Covariance> check(m.covariance);
    require(check.info() == Eigen::Success && check.eigenvalues().minCoeff() > 0,
            "PSD repair failed");
    m.min_eigen_after = check.eigenvalues().minCoeff();
    m.condition_after = check.eigenvalues().maxCoeff() / m.min_eigen_after;
}
MomentEstimate horizon_moments(const Samples &x, size_t b, size_t e, int h, int span, bool ewm,
                               double floor, Timing *timing) {
    range(x, b, e);
    require(h >= 1 && h <= static_cast<int>(e - b), "invalid horizon");
    const size_t n = e - b;
    std::vector<double> weights(n, 1.0 / static_cast<double>(n));
    if (ewm) {
        require(span > 1, "EWM span must exceed 1");
        const double a = 2.0 / (span + 1), d = 1 - a;
        weights[0] = std::pow(d, static_cast<double>(n - 1));
        for (size_t t = 1; t < n; ++t)
            weights[t] = a * std::pow(d, static_cast<double>(n - 1 - t));
    }
    double sum = 0, sum2 = 0;
    for (double v : weights)
        sum += v;
    for (double &v : weights) {
        v /= sum;
        sum2 += v * v;
    }
    MomentEstimate m;
    m.effective_sample_size = 1 / sum2;
    m.bandwidth = h - 1;
    for (size_t t = 0; t < n; ++t)
        m.mean += weights[t] * x[b + t];
    Samples centered(n);
    Covariance daily = Covariance::Zero();
    for (size_t t = 0; t < n; ++t) {
        centered[t] = std::sqrt(weights[t]) * (x[b + t] - m.mean);
        daily.noalias() += centered[t] * centered[t].transpose();
    }
    // Common normalization for ALL lags; Bartlett kernel gives a PSD estimate.
    m.covariance = h * daily;
    for (int lag = 1; lag < h; ++lag) {
        Covariance gamma = Covariance::Zero();
        for (size_t t = static_cast<size_t>(lag); t < n; ++t)
            gamma.noalias() += centered[t] * centered[t - lag].transpose();
        m.covariance += (h - lag) * (gamma + gamma.transpose());
    }
    m.covariance /= 1 - sum2;
    // OAS intensity estimated on daily observations, then applied to horizon
    // covariance. With autocorrelation/EWM this is a documented plug-in rule,
    // not an assertion of the iid OAS theorem for dependent observations.
    const double alpha = oas_alpha(daily, m.effective_sample_size);
    m.mean *= h;
    regularize(m, alpha, floor, timing);
    return m;
}

} // namespace mvo
