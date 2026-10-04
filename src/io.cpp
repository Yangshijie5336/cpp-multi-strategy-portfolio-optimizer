#include "mvo/io.hpp"
#include "mvo/dates.hpp"
#include "mvo/data.hpp"
#ifdef ENABLE_XLS
#include <freexl.h>
#endif
namespace mvo {
SheetData read_xls(const std::string &path, MissingValuePolicy policy) {
#ifdef ENABLE_XLS
    const void *handle = nullptr;
    const int opened = freexl_open(path.c_str(), &handle);
    struct Guard {
        const void *h;
        ~Guard() {
            if (h)
                freexl_close(h);
        }
    } guard{handle};
    require(opened == FREEXL_OK, "FreeXL open failed: " + std::to_string(opened));
    require(freexl_select_active_worksheet(handle, 0) == FREEXL_OK, "cannot select worksheet");
    unsigned int rows = 0;
    unsigned short cols = 0;
    require(freexl_worksheet_dimensions(handle, &rows, &cols) == FREEXL_OK,
            "cannot read dimensions");
    require(rows >= 3 && cols >= 9, "need date and eight numeric columns");
    auto cell = [&](unsigned int r, unsigned short c) {
        FreeXL_CellValue v{};
        require(freexl_get_cell_value(handle, r, c, &v) == FREEXL_OK, "cannot read cell");
        return v;
    };
    std::array<unsigned short, 8> indices{};
    std::array<bool, 8> found{};
    unsigned short datecol = 0;
    for (unsigned short c = 0; c < cols; ++c) {
        auto v = cell(0, c);
        if (v.type == FREEXL_CELL_TEXT || v.type == FREEXL_CELL_SST_TEXT) {
            std::string s = v.value.text_value;
            if (s == "Date" || s == "Unnamed: 0")
                datecol = c;
            for (int j = 0; j < 8; ++j)
                if (s == std::string(1, static_cast<char>('a' + j))) {
                    indices[j] = c;
                    found[j] = true;
                }
        }
    }
    for (bool f : found)
        require(f, "missing a..h column");
    unsigned int datemode = 0;
    require(freexl_get_info(handle, FREEXL_BIFF_DATEMODE, &datemode) == FREEXL_OK,
            "cannot read Excel date system");
    auto numeric = [](const FreeXL_CellValue &v) {
        if (v.type == FREEXL_CELL_DOUBLE)
            return v.value.double_value;
        if (v.type == FREEXL_CELL_INT)
            return double(v.value.int_value);
        return unavailable();
    };
    SheetData result;
    result.values.reserve(rows - 1);
    result.dates.reserve(rows - 1);
    for (unsigned int r = 1; r < rows; ++r) {
        Vector v;
        for (int j = 0; j < 8; ++j)
            v[j] = numeric(cell(r, indices[j]));
        auto d = cell(r, datecol);
        double serial = unavailable();
        if (d.type == FREEXL_CELL_DATE || d.type == FREEXL_CELL_DATETIME) {
            serial = date_serial(d.value.text_value);
        } else {
            serial = numeric(d);
            if (datemode == FREEXL_BIFF_DATEMODE_1904)
                serial += 1462;
        }
        result.dates.push_back(serial);
        result.values.push_back(v);
    }
    validate_sheet(result, policy);
    return result;
#else
    (void)path;
    (void)policy;
    throw std::runtime_error("reconfigure with ENABLE_XLS=ON");
#endif
}

} // namespace mvo
