#pragma once
#include "types.hpp"
#include "dates.hpp"
#include "data.hpp"
namespace mvo {
SheetData read_xls(const std::string &path,
                   MissingValuePolicy missing = MissingValuePolicy::Reject);
}
