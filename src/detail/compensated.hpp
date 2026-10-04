#pragma once
namespace mvo::detail {
struct Compensated {
    double sum = 0, correction = 0;
    void add(double x) {
        double y = x - correction, t = sum + y;
        correction = (t - sum) - y;
        sum = t;
    }
};
} // namespace mvo::detail
