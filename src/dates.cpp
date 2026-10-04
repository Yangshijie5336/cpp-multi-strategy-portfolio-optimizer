#include "mvo/dates.hpp"
#include "mvo/types.hpp"
#include <cstdio>
namespace mvo {
namespace {
using namespace std::chrono;
const sys_days epoch = year{1899} / 12 / 31;
double real_day(double s) {
    require(std::isfinite(s) && s >= 1 && s < 2958466, "invalid Excel serial");
    return s - (s >= 60 ? 1 : 0);
}
} // namespace
double date_serial(const std::string &iso) {
    int y = 0, m = 0, d = 0;
    require(sscanf_s(iso.c_str(), "%d-%d-%d", &y, &m, &d) == 3, "invalid date: " + iso);
    if (y == 1900 && m == 2 && d == 29)
        return 60;
    std::chrono::year_month_day date{std::chrono::year{y},
                                     std::chrono::month{static_cast<unsigned>(m)},
                                     std::chrono::day{static_cast<unsigned>(d)}};
    require(date.ok(), "invalid date: " + iso);
    double days = static_cast<double>((std::chrono::sys_days{date} - epoch).count());
    return days + (days >= 60 ? 1 : 0);
}
std::string date_string(double s) {
    const auto date = std::chrono::year_month_day{
        epoch + std::chrono::days{static_cast<int>(std::floor(real_day(s)))}};
    char out[16];
    std::snprintf(out, sizeof(out), "%04d%02u%02u", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
    return out;
}
double elapsed_days(double first, double last_day) {
    return std::floor(real_day(last_day) - real_day(first));
}
void validate_date_serial(double serial) {
    (void)real_day(serial);
}

} // namespace mvo
