#pragma once
#include "types.hpp"

namespace mvo {
Samples preprocess(const Samples &input, int window, double sigma);
}
