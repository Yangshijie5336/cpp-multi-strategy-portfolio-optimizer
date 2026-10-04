# 接口文档

本文按当前 `include/mvo/*.hpp` 和实现编写。所有 C++ 接口位于 `mvo` 命名空间，使用 C++20 和 Eigen；这是源码级接口说明，不承诺跨编译器二进制 ABI 兼容。结构说明见 [STRUCTURE.md](STRUCTURE.md)。

## 1. 命令行接口

```bat
mvo.exe
mvo.exe data.xls --compat
mvo.exe data.xls --debug
mvo.exe nav.xls --nav
mvo.exe returns.xls --daily-return
```

形式：`mvo.exe [输入路径 [选项...]]`。无参数时读取当前目录的 `data.xls`。第一个参数始终作为路径；不能省略路径直接写 `--compat`。当前没有 `--help` 接口。

| 选项 | 行为 |
|---|---|
| `--compat` | Python 兼容统计与 legacy SLSQP 求解 |
| `--debug` | 向标准错误输出各策略最后一次调仓诊断 |
| `--nav` | 输入按正净值计算相邻比值收益 |
| `--daily-return` | 输入直接作为每日收益/损益增量 |

默认高精度模式、累计损益输入。重复或冲突选项按从左到右覆盖（例如最后一个输入类型选项生效）。其他参数如窗口和本金只能通过 C++ `Options` 设置。

标准输出包含三种目标分别使用普通/指数加权统计的六行指标、一行基准指标、求解计数和 `time_*_ms` 耗时；正常结束返回 `0`，捕获 `std::exception` 后输出 `error: ...` 并返回 `1`。返回 `0` 不代表所有调仓求解成功，还需检查 `failure` 和 `fallback`。

## 2. 数据类型与配置

定义文件：`include/mvo/types.hpp`。

```cpp
constexpr int assets = 8;
using Vector = Eigen::Matrix<double, assets, 1>;
using Covariance = Eigen::Matrix<double, assets, assets>;
using Samples = std::vector<Vector>;
```

`Samples[t][j]` 表示第 t 行、第 j 个资产，资产顺序固定为 a～h。日期和样本按行对应。

| `Options` 字段 | 默认值 | 含义/使用限制 |
|---|---|---|
| `window` | 60 | 回看行数，至少 2 |
| `keep` | 15 | 调仓间隔和持有期，`1 <= keep < window` |
| `yr_dates` | 250 | 评价年化天数，必须为正 |
| `sigma` | 3 | 预处理截尾标准差倍数，有限且非负 |
| `turnover` | 0.1 | 高精度路径的 L1 换手上限，不除以 2 |
| `feasibility_tolerance` | 1e-9 | 高精度路径结果约束验收容差 |
| `optimality_tolerance` | 1e-9 | HiGHS 缩放模型 KKT 验收容差 |
| `eigenvalue_relative_floor` | 1e-12 | 正则化特征值下限参数，应有限且为正 |
| `accuracy` | `HighAccuracy` | 另一值为 `PythonCompatible` |
| `input` | `CumulativePnL` | 另有 `NAV`、`DailyReturn` |
| `missing` | `Reject` | 另有 `DropRow` |
| `capital` | 空 | 累计损益转增量的除数；指定时必须有限且为正 |
| `risk_averse` | 空 | 空：最大分散；0：最大收益；正数：均值方差目标 |
| `ewm` | false | 从缓存选择普通或指数加权矩 |
| `initial` | `initial_weights()` | 初始 8 维权重 |
| `debug` | false | CLI 最后一次调仓诊断开关 |

初始权重为 `{.089, .1025, .0635, .0645, .2572, .1655, .1272, .1306}`。调用者应提供有限、非负、和为 1 且满足分组约束的权重。兼容求解器固定使用换手上限 0.1 和自身容差，不读取 `Options` 中的换手、最优性与可行性容差。

| 结构体 | 字段与语义 |
|---|---|
| `SheetData` | `dates`：Excel 日期序号；`values`：样本；`dropped_rows`：本次校验删除行数 |
| `MomentEstimate` | `mean`、`volatility`、`covariance`；`shrinkage_alpha`、`effective_sample_size`、`bandwidth`；正则化前后最小特征值和条件数 |
| `RebalanceMoments` | `day`：调仓行下标；`ordinary`、`ewm`：同一窗口的两类统计量 |
| `Timing` | `ms`：阶段名到累计毫秒数的映射；`Clock` 使用 `steady_clock` |
| `ScopedTimer` | 构造记录开始时刻，析构累加指定计时项；空计时指针不写入结果 |

`MomentEstimate` 中未由对应计算路径填充的诊断字段保留初始零值，不能据此认定已做正则化。返回容器拥有自己的数据；只读引用在调用期间需有效。`Timing*` 是非拥有指针，多线程共享时由调用者同步。

## 3. 输入与日期接口

```cpp
SheetData read_xls(const std::string& path,
                   MissingValuePolicy missing = MissingValuePolicy::Reject);
void validate_sheet(SheetData& sheet, MissingValuePolicy missing);
Samples convert_daily(const Samples& values, InputType input,
                      std::optional<double> capital = {});

double date_serial(const std::string& iso);
std::string date_string(double serial);
double elapsed_days(double first, double last);
void validate_date_serial(double serial);
```

- `read_xls`（`io.hpp`）读取第一个工作表。首行必须包含 a～h 数值列的列名；日期列查找 `Date` 或 `Unnamed: 0`，未找到时使用第一列。非数值资产单元格作为缺失值处理。原生日期/日期时间单元格按日期转换，数值日期支持 1900/1904 系统；普通文本日期未作为日期字符串解析。至少保留两行有效数据。未启用 `ENABLE_XLS` 时抛出异常。
- `validate_sheet`（`data.hpp`）原地替换清洗后的数据，要求日期/数值等长、日期严格递增。`DropRow` 仅删除非有限日期或数值行，不自动修复倒序、重复或越界日期；删除发生在收益转换之前，因此跨缺失行的差值会跨越时间间隔。
- `convert_daily` 返回等长新序列。累计损益：首行为零，其后为相邻差值除以本金（缺省除以 1）；NAV：首行为零，其后为 `value[t]/value[t-1]-1`；DailyReturn：原样复制，包括第一行。检查空输入、非有限值和转换溢出；NAV 相邻值须为正。本金在 NAV/DailyReturn 模式仍校验，但不参与计算。
- 日期接口（`dates.hpp`）使用 Excel 1900 日期序号。有效序号为 `[1, 2958466)`；`date_string` 输出 `YYYYMMDD`，`elapsed_days` 返回校正 Excel 虚构闰日后的间隔并向下取整。`date_serial` 读取 `年-月-日` 前缀，不是严格的完整 ISO 时间戳解析器；后续使用前可调用 `validate_date_serial`。序号 60 接受为历史兼容值，格式化不是虚构日期的无损往返。

## 4. 预处理与矩估计

```cpp
Samples preprocess(const Samples& input, int window, double sigma);
MomentEstimate ordinary_moments(const Samples& x, size_t begin, size_t end);
MomentEstimate ewm_moments(const Samples& x, size_t begin, size_t end, int span);
double oas_alpha(const Covariance& mle, double n_eff);
void regularize(MomentEstimate& moments, double alpha, double relative_floor,
                Timing* timing = nullptr);
MomentEstimate horizon_moments(const Samples& x, size_t begin, size_t end,
                               int horizon, int span, bool ewm, double floor,
                               Timing* timing = nullptr);
std::vector<RebalanceMoments> prepare(const Samples& daily, const Options& options,
                                      Timing* timing = nullptr);
```

`preprocess` 声明于 `preprocessing.hpp`，其余声明于 `moments.hpp`。

| 接口 | 输入约定和结果 |
|---|---|
| `preprocess` | 返回副本；前 `window` 行不变，其后逐列截断到历史均值 ± sigma×样本标准差。历史窗口使用已经截断的输出，而非始终使用原始数据 |
| `ordinary_moments` | `[begin,end)`，至少 2 个有限样本；均值与分母 n−1 的协方差 |
| `ewm_moments` | 同样的区间约定；递归权重 alpha=2/(span+1)，协方差做权重偏差修正。虽初步允许 span=1，但有效样本检查会拒绝该情形，调用时使用 span>1 |
| `oas_alpha` | 输入 MLE 协方差与有效样本数，返回 [0,1] 收缩强度；调用方保证有限、有效数据，接口无完整参数校验 |
| `regularize` | 原地对称化、向标量单位阵收缩、特征值截断，并更新波动率和诊断。alpha 在 [0,1]；实际下限为 relative_floor×max(1, 最大特征值) |
| `horizon_moments` | `1 <= horizon <= end-begin`；指数加权时 span>1。计算持有期均值、Bartlett 滞后协方差和 OAS 插入式收缩；带自相关/EWM 时不宣称满足独立同分布 OAS 理论条件 |
| `prepare` | 先预处理，然后在 `window, window+keep, ... < daily.size()` 生成两类矩。高精度使用 `horizon_moments`；兼容模式使用重叠 keep 日累加样本。样本不超过 window 时返回空缓存 |

缓存是调用者持有的 `vector`，没有自动数据键或失效检测。改变数据、窗口、keep、sigma、accuracy 或特征值下限后必须重新 `prepare`；同一缓存可供不同 risk/ewm 策略复用。

## 5. 求解器接口

```cpp
ConstraintReport validate_weight(const Vector& w, const Vector& previous,
                                  double turnover = .1);
double objective(const Vector& w, const MomentEstimate& m,
                 std::optional<double> risk, bool legacy_ewm_div = false);
SolveInfo solve_primary(const MomentEstimate& m, const Vector& previous,
                        std::optional<double> risk, const Options& options);
SolveInfo solve_slsqp(const MomentEstimate& m, const Vector& previous,
                      std::optional<double> risk, const Options& options,
                      bool legacy_ewm_div = false);
SolveInfo optimize(const MomentEstimate& m, const Vector& previous,
                   std::optional<double> risk, const Options& options,
                   Timing* timing = nullptr);
SolveInfo solve_compatibility(const MomentEstimate& m, const Vector& previous,
                              std::optional<double> risk, bool ewm);
```

前五项位于 `solver.hpp`；最后一项位于 `compatibility.hpp`。常规高精度调用入口是 `optimize`。独立调用时显式 `risk` 参数生效，不自动读取 `options.risk_averse`。

目标统一按最小化解释：risk 有值时为 `-mean·w + risk/2 × wᵀcovariance w`；risk 为空时为 `-volatility·w / sqrt(wᵀcovariance w)`。`objective(...,true)` 将分散目标分子改成协方差对角线；分散目标方差非正返回 NaN。该函数只计算数值，不验证权重可行性。

共同约束：权重和为 1、非负；四组 `{a,b,c,d}`、`{e}`、`{f}`、`{g,h}` 各占 0.15～0.35；相对 previous 的 L1 换手不超过上限。`validate_weight` 返回各项违反量，不修改权重；`ConstraintReport::max_violation()` 返回最大违反量，`turnover` 字段为实际换手。

- `solve_primary`：risk 必须为空或有限非负数；协方差须为半正定，previous 须可行。risk=0 使用 LP，正 risk 使用 QP，分散目标经变换后使用 QP。仅调用一次主求解器。
- `optimize`：主求解器失败状态 → 修复协方差后重试（LP 不修复）→ NLopt SLSQP → 仍失败则保留 previous。输入校验等抛出的异常会向外传播，不自动进入降级链。
- `solve_slsqp`：对变换后的模型使用 NLopt，`legacy_ewm_div` 参数目前被忽略；它不是 legacy 兼容入口。返回的 `fallback` 固定标识该降级路径，`iterations` 实际为函数评估次数。
- `solve_compatibility`：使用内嵌 legacy SLSQP、有限差分和固定容差；ewm=true 且 risk 为空时保留原方差分子。不要用它代替高精度路径的严格验收。

### `SolveInfo` 返回字段

| 字段 | 含义 |
|---|---|
| `weight` | 候选或最终权重；必须结合 success/fallback 判断 |
| `solver` | LP、QP、SLSQP 或 PreviousWeight；`solver_name` 转为显示名称 |
| `success` | 当前路径是否通过其验收；不同路径容差与验证能力不同 |
| `retry`, `fallback` | 是否发生重试或降级，不等价于最终失败 |
| `iterations` | HiGHS 迭代总数、legacy 迭代数或 NLopt 评估次数 |
| `objective` | 返回权重对应的目标值；修复后可能基于修复矩计算 |
| `constraints` | 原权重空间约束报告 |
| `primal_residual`, `dual_residual`, `complementarity`, `kkt_residual` | HiGHS 对缩放/变换模型计算的残差；SLSQP 仅提供原权重可行性残差，未提供的字段为 NaN |
| `message` | 求解器状态及重试/降级说明，不是稳定机器协议 |

NaN 表示诊断不可用，不能按零残差解释。保留旧权重时 `success` 仍为 false。兼容路径可能返回 `success=false` 但 `fallback=false` 的候选解，调用者若需要严格接受策略，应自行检查。

## 6. 组合构建与评价

```cpp
PortfolioResult run_portfolio(const Samples& daily,
                              const std::vector<RebalanceMoments>& moments,
                              const Options& options, Timing* timing = nullptr);
Metrics Evaluator::evaluate(const std::vector<double>& pnl,
                            const std::vector<double>& dates, int annual_days,
                            AccuracyMode mode = AccuracyMode::PythonCompatible);
void Evaluator::print(const Metrics& metrics, const std::vector<double>& dates,
                      std::ostream& out);
```

头文件为 `portfolio.hpp` 和 `evaluation.hpp`。

`run_portfolio` 要求矩缓存来自对应 daily/config，day 严格递增且有效；函数没有完整检查缓存顺序，乱序可能导致后续调仓被跳过。调仓日使用不含当日数据的历史窗口，求出的权重立即用于当日损益。空缓存时全程使用初始权重。

返回的 `PortfolioResult`：

- `weight`、`daily_pnl`、`pnl` 长度均为 daily.size()；`daily_pnl[t]=weight[t]·daily[t]`，`pnl[t]=1+累计 daily_pnl`，不是复利净值。
- `solves` 每次实际调仓一项；`fallback_count`、`retry_count`、`solver_failure_count` 分别统计相应状态，`max_fallback_streak` 为连续调仓降级最大次数。

`evaluate` 要求 pnl/dates 等长且至少两行、pnl 有限、日期递增、annual_days>0；调用方应保证所有日期序号有效。以相邻 pnl 差为收益，不以百分比变化率为收益。兼容模式排除零差值，高精度模式保留零差值。

| `Metrics` 字段 | 口径 |
|---|---|
| `annual_pnl` | 有效差值均值×annual_days |
| `volatility` | 有效差值样本标准差×sqrt(annual_days) |
| `sharpe` | annual_pnl/volatility，不扣无风险利率 |
| `drawdown` | 先前峰值减后续值的最大绝对差；当前实现不强制截为非负 |
| `calmar` | 首尾 pnl 差 /（实际日期间隔/365 × drawdown） |
| `win_rate` | 正差值数/有效差值数 |
| `peak`, `trough` | 最大回撤对应的行下标 |

零波动、零回撤或有效差值不足时可能得到 NaN/Inf。`print` 按年化损益、波动率、Sharpe、Calmar、胜率、回撤、峰日期、谷日期输出，以 ` & ` 分隔；损益/波动/胜率/回撤乘 100。dates 必须覆盖 peak/trough 下标；流的 fixed/precision 设置会被修改。

注意：当前 `run_cli` 调用 `evaluate` 时未传 mode，因此即使高精度求解，CLI 指标仍采用默认兼容评价口径。库用户可显式选择高精度评价。

## 7. 应用入口

```cpp
struct CommandLine { std::string input_path = "data.xls"; Options options; };
CommandLine parse_command_line(int argc, char** argv); // cli.hpp
int run_cli(int argc, char** argv);                    // app.hpp
```

argv 按常规 main 参数传入，字符串在调用期间有效。未知选项抛出 `std::invalid_argument`；底层 `require` 校验抛出 `std::runtime_error`。应用入口统一捕获标准异常；库调用者需自行处理异常以及非异常的求解失败状态。

## 8. 最小库调用示例

```cpp
#include "mvo/portfolio.hpp"
#include <exception>
#include <iostream>

int main() {
    try {
        mvo::Options options;
        options.risk_averse = 20.0;
        mvo::Timing timing;
        auto sheet = mvo::read_xls("data.xls", options.missing);
        auto daily = mvo::convert_daily(sheet.values, options.input, options.capital);
        auto cache = mvo::prepare(daily, options, &timing);
        auto result = mvo::run_portfolio(daily, cache, options, &timing);
        auto metrics = mvo::Evaluator::evaluate(
            result.pnl, sheet.dates, options.yr_dates,
            mvo::AccuracyMode::PythonCompatible); // 与当前 CLI 评价口径一致
        mvo::Evaluator::print(metrics, sheet.dates, std::cout);
        if (result.solver_failure_count != 0) {
            std::cerr << "存在未通过验收的调仓，请检查 result.solves\n";
            return 2;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
```

在现有 CMake 工程内增加可执行目标并 `target_link_libraries(your_target PRIVATE mvo_core)`。使用 `run_cli` 时链接 `mvo_app`。自建可执行目标还需部署 FreeXL 依赖 DLL；现有自动复制规则只覆盖 `mvo` 和 `mvo_tests`。

## 9. 文档验证范围

本说明依据头文件、各模块实现及 CLI 调用点核对；没有新增接口或改变计算逻辑。输出回归测试只逐字比较格式化指标和求解计数（剔除耗时行），不能据此宣称逐日权重或全部内部统计量完全一致。修改接口时应同步更新本文、调用示例及相关测试。
