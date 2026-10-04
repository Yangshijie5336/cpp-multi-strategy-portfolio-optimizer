#include "mvo/data.hpp"
#include "mvo/dates.hpp"
namespace mvo {
void validate_sheet(SheetData &sheet, MissingValuePolicy policy) {
    require(sheet.values.size() == sheet.dates.size(), "dates/data dimension mismatch");
    SheetData cleaned;
    cleaned.values.reserve(sheet.values.size());
    cleaned.dates.reserve(sheet.dates.size());
    for (size_t i = 0; i < sheet.values.size(); ++i) {
        if (!sheet.values[i].allFinite() || !std::isfinite(sheet.dates[i])) {
            require(policy == MissingValuePolicy::DropRow,
                    "missing/nonfinite input at row " + std::to_string(i + 2));
            ++cleaned.dropped_rows;
            continue;
        }
        validate_date_serial(sheet.dates[i]);
        require(cleaned.dates.empty() || sheet.dates[i] > cleaned.dates.back(),
                "dates must be strictly increasing");
        cleaned.values.push_back(sheet.values[i]);
        cleaned.dates.push_back(sheet.dates[i]);
    }
    require(cleaned.values.size() >= 2, "need at least two valid rows");
    sheet = std::move(cleaned);
}
Samples convert_daily(const Samples &values, InputType input, std::optional<double> capital) {
    require(!values.empty(), "empty input");
    if (capital)
        require(std::isfinite(*capital) && *capital > 0, "capital must be finite and positive");
    Samples daily(values.size(), Vector::Zero());
    for (size_t t = 0; t < values.size(); ++t) {
        require(values[t].allFinite(), "nonfinite data before conversion");
        if (input == InputType::DailyReturn)
            daily[t] = values[t];
        else if (t) {
            if (input == InputType::NAV) {
                require((values[t - 1].array() > 0).all() && (values[t].array() > 0).all(),
                        "NAV must be positive");
                daily[t] = (values[t].array() / values[t - 1].array() - 1).matrix();
            } else
                daily[t] = (values[t] - values[t - 1]) / (capital ? *capital : 1.0);
        }
        require(daily[t].allFinite(), "overflow converting daily data");
    }
    return daily;
}
} // namespace mvo
