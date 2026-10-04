# C++20 多策略组合优化报告

## 1. 工程与运行方式

模块职责和依赖关系见 [MODULES.md](C:/Users/YSJ/Desktop/量化开发工程师笔试题杨世杰/yangshijie/MODULES.md)。入口 `main.cpp` 现在只负责调用 `mvo::run_cli`；命令行、数据读取、日期处理、预处理、矩估计、求解、绩效和输出分别属于独立模块。

工程现在以 C++20、MSVC x64 和 CMake 构建，核心源文件位于 `src/`，公共接口位于 `include/mvo/`。Eigen 负责矩阵运算，FreeXL 负责读取 BIFF `.xls`，HiGHS 负责 LP/QP 主求解，NLopt SLSQP 负责兼容路径和主求解失败后的回退。

```bat
cmake_build.bat
build\mvo.exe data.xls
build\mvo.exe data.xls --compat
ctest --test-dir build --output-on-failure
```

`build.ps1` 已改为调用同一套 CMake/MSVC 构建流程；旧版直接编译一个 `main.cpp` 的脚本已移除，因为它不会链接当前工程依赖。

FreeXL 的 vcpkg Windows 版本是动态库。`freexl-1.dll` 还依赖 `libexpat.dll`、`minizip.dll`、`iconv-2.dll`，而 `minizip.dll` 依赖 `z.dll`。CMake 已在 `mvo` 和 `mvo_tests` 链接后自动把这五个 DLL 复制到目标目录，避免出现“找不到 freexl-1.dll”或随后依赖缺失的启动错误。移动可执行文件时应连同这些 DLL 一起移动。

## 2. 核心算法

程序先把累积 PnL 转换为日增量，然后执行三倍标准差处理、15 日保持期滚动样本和滚动调仓。等权矩估计使用样本均值和无偏协方差；指数加权估计使用递推均值和协方差。高精度模式增加 Bartlett 核 HAC/Newey-West 多期协方差、OAS 收缩和特征值地板 PSD 修复。协方差在进入求解器前强制对称并检查有限值。

均值-方差目标是凸二次规划：

`min 1/2 * risk_averse * w'Σw - μ'w`。

当风险厌恶为零时退化为带线性约束的 LP。约束包括权重和为 1、非负、四组上下限和 L1 换手上限。最大分散策略使用二次变换后交给 QP；在变换模型或 HiGHS 的 KKT 校验失败时使用 NLopt SLSQP。求解结果会重新计算约束残差、互补性和 KKT 残差，失败时保留上次可行权重并记录 `retry/fallback/failure`。

Eigen、HiGHS、NLopt 均为第三方库；兼容模式的 SLSQP 通过现有 legacy 接口保留原 Python 的有限差分和边界处理。固定维度为 8 的问题不需要通用大规模稀疏算法。

可选路线包括 IPOPT（一般非线性约束更强，但部署和运行时依赖更重）、OSQP（适合稀疏凸 QP，但最大分散变换和当前 MSVC 发布链增加额外工作）以及手写主动集法（维数小、速度快，但需要自己承担退化约束和数值稳定性风险）。本实现选择 HiGHS + NLopt，是因为 LP/QP 与 SLSQP 都覆盖了原程序的目标，且可在本地 MSVC 下复现。

## 3. 实测运行时间

测试文件为工作区的 `data.xls`（235 行、8 个时间序列），Release x64 MSVC 构建。程序内部计时包括文件读取、收益转换、矩估计、求解器和组合构造；一次独立进程的端到端耗时小于 1 秒。

最近一次高精度模式输出的关键计时为：

| 项目 | 毫秒 |
|---|---:|
| `time_total_ms` | 56.0 |
| `time_XLS_read_ms` | 18.186 |
| `time_LP_solve_ms` | 6.2 |
| `time_QP_solve_ms` | 35.5 |
| `time_SLSQP_fallback_ms` | 0 |
| `time_covariance_regularization_ms` | 0.25 |

兼容模式最近一次 `time_total_ms=43.456`。`ctest --test-dir build --output-on-failure` 已通过 `numerical_regression` 和 `xls_runtime` 两项测试。

## 4. 输出与 Python 路径对比

高精度模式最近一次输出如下，百分数和指标格式与原程序保持一致：

| 策略 | 年化 PnL | 年化波动 | Sharpe | Calmar | 胜率 | 最大回撤 |
|---|---:|---:|---:|---:|---:|---:|
| 最大分散/等权 | 16.27 | 3.49 | 4.662 | 53.825 | 47.75 | 0.42 |
| 最大分散/指数加权 | 16.32 | 3.23 | 5.050 | 50.233 | 47.30 | 0.45 |
| 最大目标收益率/等权 | 21.70 | 3.92 | 5.542 | 36.245 | 49.54 | 0.81 |
| 最大目标收益率/指数加权 | 21.39 | 3.97 | 5.382 | 40.316 | 48.17 | 0.72 |
| 风险厌恶-20/等权 | 21.71 | 3.92 | 5.541 | 36.197 | 49.54 | 0.82 |
| 风险厌恶-20/指数加权 | 22.13 | 4.14 | 5.343 | 41.717 | 49.08 | 0.72 |

`--compat` 用于复现 Python 的有限差分 SLSQP 路径。当前数据上其均值-方差四组指标与原 Python 表格的显示精度基本一致；最大分散等权分支仍有较明显差距，原因是该目标含平方根和 L1 换手不可导点，且不同 SLSQP 实现的终止点会落在不同的平坦边界。EWMA 的兼容分支保留了原代码直接使用协方差对角线的历史行为，因此不能把它解释为标准差。

默认高精度模式的矩估计与 Python 的简单滚动协方差不同，加入了 HAC、多期缩放、OAS 和 PSD 修复，所以它的权重与指标不应逐位等同于 `algo.py`。修正稀疏矩阵哨兵后，本数据六组策略全部由 HiGHS 主求解器直接得到，`solver_fallback=0 retry=0 failure=0`，最大 KKT 残差为 `7.8e-16`，没有使用 SLSQP 回退。

## 5. 关键隐患与边界

1. SLSQP 是局部算法；最大分散目标不能仅凭一次运行证明全局最优。初值、有限差分步长和容差会改变最后几位。
2. 协方差收缩和特征值地板提高了稳定性，却改变了统计估计；高精度模式与 Python 原始统计量的差异是有意行为。
3. 所有输入都要求有限、日期严格递增、至少有两行且包含 `a..h` 八列。缺失值默认拒绝，只有显式选择 `DropRow` 才会剔除整行。
4. Excel 1900/1904 日期系统和 1900-02-29 兼容规则已在日期工具中处理；跨文件比较前仍应确认日期系统和输入含义（累积 PnL、NAV 或日收益）一致。
5. 若 FreeXL 版本更换，应重新检查 DLL 依赖链；仅复制 `freexl-1.dll` 不足以保证 Windows 程序启动。
