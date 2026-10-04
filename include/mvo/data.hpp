#pragma once
#include "types.hpp"

namespace mvo {
void validate_sheet(SheetData &sheet, MissingValuePolicy missing);
Samples convert_daily(const Samples &values, InputType input, std::optional<double> capital = {});
} // namespace mvo
