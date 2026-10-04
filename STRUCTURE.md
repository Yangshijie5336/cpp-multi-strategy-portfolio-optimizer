# C++20 多策略组合优化项目结构文档

参数、函数签名、返回值和调用示例见 [API.md](API.md)。以下描述以当前实际实现为准。

## 1. 项目定位

本项目使用 C++20 重构 `algo.py` 的时间序列组合优化流程。程序从 Excel/XLS 文件读取 8 个策略时间序列，完成数据校验、收益预处理、统计矩估计、约束建模、凸优化求解、组合评估和结果报告输出。

设计目标按优先级排列为：

1. 数学结果与原 Python 实现保持一致；
2. 保证协方差、标准差和优化约束计算的数值稳定性；
3. 在 MSVC 环境下减少动态分配和重复统计计算，使单次运行保持在 1 秒以内；
4. 让输入、算法、求解器和报告模块可以独立替换或测试。

## 2. 目录结构

```text
.
├─ main.cpp                         # 最小程序入口
├─ CMakeLists.txt                   # 工程、目标和测试配置
├─ cmake/
│  ├─ Dependencies.cmake            # Eigen、HiGHS、NLopt 等依赖配置
│  └─ FreeXL.cmake                  # FreeXL 查找、导入和 DLL 部署
├─ include/mvo/                     # 对外头文件
│  ├─ app.hpp                       # 应用层入口
│  ├─ cli.hpp                       # 命令行参数和运行模式
│  ├─ compatibility.hpp             # legacy/compat 兼容流程
│  ├─ data.hpp                      # 原始表格和时间序列数据结构
│  ├─ dates.hpp                     # 日期解析和交易日辅助函数
│  ├─ evaluation.hpp                # 组合指标和基准评估
│  ├─ io.hpp                        # XLS/文本输出接口
│  ├─ moments.hpp                   # 均值、协方差和持有期矩接口
│  ├─ portfolio.hpp                 # 组合结果和策略组合流程
│  ├─ preprocessing.hpp             # 收益率转换和清洗
│  ├─ solver.hpp                    # 求解器统一接口
│  └─ types.hpp                     # 公共枚举、结构体和计时类型
├─ src/
│  ├─ app.cpp                       # 应用编排
│  ├─ cli.cpp                       # 命令行解析
│  ├─ compatibility.cpp             # 兼容输出和旧流程适配
│  ├─ constraints.cpp               # 线性/二次约束构造
│  ├─ data.cpp                      # 数据对象实现
│  ├─ dates.cpp                     # 日期处理实现
│  ├─ evaluation.cpp                # 结果评价实现
│  ├─ highs_solver.cpp              # HiGHS 线性/二次优化适配
│  ├─ io.cpp                        # FreeXL 输入和结果文件输出
│  ├─ moment_cache.cpp              # 统计量缓存
│  ├─ moments.cpp                   # 稳定统计量计算
│  ├─ optimization_model.cpp        # 优化模型组装
│  ├─ portfolio.cpp                 # 多策略执行流程
│  ├─ preprocessing.cpp             # 预处理实现
│  ├─ reporting.cpp                 # 终端报告和诊断输出
│  ├─ slsqp_solver.cpp               # SLSQP 兼容求解器
│  ├─ solver.cpp                    # 求解器选择、重试和降级策略
│  └─ detail/
│     ├─ compensated.hpp             # 补偿求和等数值工具
│     └─ optimization_model.hpp      # 优化模型内部实现
├─ tests/
│  ├─ test_main.cpp                 # 回归测试入口
│  ├─ compare_output.cmake          # 输出比较脚本
│  └─ fixtures/                     # high/compat 基准输出
├─ build_only.bat                   # MSVC 增量构建
├─ cmake_build.bat                  # 配置并构建
├─ build.ps1                        # PowerShell 构建辅助脚本
├─ MODULES.md                       # 模块职责简表
└─ REPORT.md                        # 算法、性能和结果报告
```

## 3. CMake 目标和依赖方向

```text
mvo.exe
  └─ mvo_app
       └─ mvo_core
            ├─ Eigen3
            ├─ HiGHS
            ├─ NLopt/SLSQP（兼容路径）
            └─ FreeXL
```

- `mvo_core` 只包含领域逻辑，不依赖命令行入口。
- `mvo_app` 负责把 CLI 参数转换成一次完整运行。
- `mvo.exe` 只有 `main.cpp`，便于测试和替换应用入口。
- `mvo_tests` 链接 `mvo_app`，因此测试使用与正式程序相同的代码路径。
- FreeXL 是运行时动态库依赖，MSVC 构建后需要将 `freexl-1.dll` 及其依赖 DLL 放在可执行文件旁边或加入 `PATH`。

依赖方向保持单向：输入层不调用评价层，统计层不读取 Excel，求解器不负责打印报告，评价层不依赖 FreeXL。这样可以单独替换输入格式或优化器而不扩大修改范围。

## 4. 一次运行的数据流

```text
命令行参数
   ↓
mvo::parse_command_line
   ↓
mvo::read_xls ──→ mvo::SheetData
   ↓
dates / data validation
   ↓
mvo::convert_daily / mvo::preprocess
   ↓
mvo::prepare
   └─→ moment_cache（同一数据集复用统计量）
   ↓
mvo::run_portfolio
   ↓
optimization_model + constraints
   ↓
mvo::optimize / mvo::solve_compatibility
   ├─ HiGHS 直接求解
   ├─ 必要时重试/降级
   └─ SLSQP 兼容路径
   ↓
mvo::Evaluator::evaluate
   ↓
mvo::Evaluator::print / 标准输出
```

每个时间序列先统一成内部数值表示，再按原算法约定计算收益。所有策略共享同一份预处理结果和统计量，避免每个策略重复读取和重复计算。

## 5. 核心模块职责

### 5.1 数据和输入层

- `data.hpp/cpp` 校验 `SheetData` 并转换每日增量；数据对象不保存原始列名。
- `dates.hpp/cpp` 处理日期序号转换和自然日间隔；输入顺序由数据校验检查，不自动排序。
- `io.hpp/cpp` 封装 FreeXL 读取；格式化文本输出由 `reporting.cpp` 和应用层负责。
- 输入阶段应尽早检查列数、样本数、空单元格、非有限值和日期顺序。

### 5.2 预处理和统计层

- `mvo::convert_daily` 负责累计损益或净值转换；`preprocessing` 负责滚动截尾。
- `moments` 计算均值向量、协方差矩阵和持有期统计量，不计算高阶矩。
- `detail/compensated.hpp` 用补偿求和降低长序列累加误差。
- `moment_cache` 返回由调用者复用的调仓矩数组，没有键查询和自动失效机制；数据或统计配置变化时必须重新生成。

统计接口返回值对象，`regularize` 原地修改矩估计；统计模块不拥有求解器状态，也不改变原始样本输入。

### 5.3 优化建模层

- `constraints` 生成权重和、上下界、暴露度等约束。
- `optimization_model` 将目标、梯度、约束和边界组装成统一模型。
- 线性目标/约束使用矩阵形式；二次风险目标使用对称协方差矩阵。
- 约束构造集中在一个模块，避免不同策略对权重和边界产生不一致解释。

### 5.4 求解层

- `solver.hpp` 提供与具体求解器无关的请求和结果类型。
- `highs_solver` 优先走 HiGHS 的直接线性/二次优化路径。
- `slsqp_solver` 保留原 Python/NLopt 风格的非线性兼容路径。
- `solver.cpp` 统一处理求解器选择、失败重试、降级和诊断计数。
- `SolveInfo` 返回权重、目标值、迭代信息、成功标志、状态文字及残差；组合层汇总重试和降级次数。

求解完成后应检查：权重有限、权重和残差、上下界违反量、目标值有限，以及 KKT/一阶残差是否在容差内。失败时保留明确的诊断信息，不静默使用不完整结果。

### 5.5 组合、评价和报告层

- `portfolio` 按固定策略顺序执行各优化模型，并生成基准组合。
- `evaluation` 计算收益、波动率、夏普类指标和回撤；应用层分别评价策略与基准，不直接计算基准差异。
- `reporting` 负责终端格式、时间统计和求解器诊断；它不重新计算统计量。
- `compatibility` 实现旧模式有限差分 SLSQP；输出顺序和格式由应用层及报告层控制。

## 6. 公共数据类型

`include/mvo/types.hpp` 是模块之间的稳定边界，主要包含：

- 精度模式、输入类型、缺失值策略和求解器类型枚举；
- `SheetData`、`Samples`、`MomentEstimate` 和 `Options`；
- `SolveInfo`、`ConstraintReport` 和求解器类型；
- 组合评价结果和分阶段计时信息。

结构体应优先使用值语义和只读引用传递。跨模块共享的对象不应携带 FreeXL、HiGHS 或 NLopt 的句柄，以免把第三方库生命周期传播到整个项目。

## 7. 数值稳定性和精度边界

1. 协方差矩阵在建模前应保持对称；必要时使用 `(S + Sᵀ) / 2` 消除浮点非对称。
2. 对接近奇异的矩阵，需要通过模型正则化、边界约束或求解器容差控制风险；不能简单忽略求解失败。
3. 统计量计算使用 `double` 和补偿累加；输入中的非有限值必须在优化前拒绝。
4. 求解器的内部容差与输出比较精度不同，回归测试应比较业务结果和约束残差，而不是要求每个中间迭代点完全相同。
5. 不同求解器可能得到等价但末位不同的权重；最终结果应同时记录目标值、约束残差和求解状态。

## 8. 测试和回归保障

`tests/` 采用固定 fixture 对正式程序进行黑盒回归：

- `numerical_regression` 检查核心数值结果；
- `xls_runtime` 检查真实 XLS 读取和运行时 DLL；
- `output_high` 检查高精度/HiGHS 路径输出；
- `output_compat` 检查兼容模式输出。

输出比较脚本剔除耗时行，逐字比较格式化指标和求解计数；不包含逐日权重和全部内部诊断。修改算法时应先确认差异正确，再更新 fixture，不能以更新基准代替验证。

## 9. 构建、部署和性能边界

推荐在 VS Developer Command Prompt 中执行：

```bat
build_only.bat
ctest --test-dir build --output-on-failure
```

运行时目录至少应包含 `mvo.exe` 和 FreeXL 相关 DLL。性能统计在 `reporting` 中按阶段记录，重点观察：Excel 读取、预处理、矩估计、模型构造和求解时间。当前设计通过统计量缓存、共享矩阵和避免重复 I/O，将优化重点放在求解器调用而非文件解析。

## 10. 扩展规则

### 增加输入格式

在 `io` 层新增实现，将结果转换为现有 `SheetData`；不要让求解器直接读取 CSV、XLSX 或数据库。

### 增加优化策略

当前策略由 `risk_averse` 和 `ewm` 组合表示，没有独立策略枚举或 `SolveRequest`。新增目标需扩展公开参数与模型构造，并更新应用层策略列表及测试。

### 增加求解器

实现 `solver.hpp` 的统一接口，在 `solver.cpp` 注册选择逻辑，并返回统一诊断。第三方句柄只允许存在于具体适配文件中。

### 增加评价指标

在 `evaluation` 中增加纯函数式指标，扩展评价结果结构体和报告格式，同时为该指标补充固定 fixture。

## 11. 已知隐患

- FreeXL DLL 的搜索路径由运行环境决定，复制 DLL 或配置 `PATH` 是部署要求；
- 高相关或样本数较少时协方差矩阵可能病态，结果对正则化和容差敏感；
- HiGHS 与 SLSQP 对边界附近解的数值表现可能不同，因此兼容模式需要单独回归；
- Excel 单元格类型、日期格式和空值语义依赖输入文件，输入校验失败时应停止运行并报告具体列和行；
- 运行时间受磁盘、杀毒软件和 DLL 加载影响，性能报告应记录运行模式、数据规模和硬件环境。
