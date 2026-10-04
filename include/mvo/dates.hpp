#pragma once
#include <string>

namespace mvo {
double date_serial(const std::string &iso);
std::string date_string(double serial);
double elapsed_days(double first, double last);
void validate_date_serial(double serial);
} // namespace mvo
