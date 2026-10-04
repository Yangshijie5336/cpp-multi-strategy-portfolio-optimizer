# Multi-Strategy Portfolio Optimizer

基于 C++20/MSVC 的 8 个时间序列组合优化程序，按 `algo.py` 的计算流程实现数据读取、收益预处理、协方差估计、约束优化和组合评价。

## 文档

- [API 接口文档](API.md)
- [项目结构文档](STRUCTURE.md)
- [模块简表](MODULES.md)
- [算法、性能和结果报告](REPORT.md)
- [GitHub 开源项目复用调研](REUSE_REPORT.md)

## 构建运行

在 Visual Studio Developer Command Prompt 中执行：

```bat
cmake_build.bat
mvo.exe data.xls
mvo.exe data.xls --compat
ctest --test-dir build --output-on-failure
```

程序依赖 Eigen、HiGHS、NLopt 和 FreeXL。Windows 运行时需要将 FreeXL 及其依赖 DLL 放在可执行文件目录或加入 `PATH`。完整配置说明见 `cmake/Dependencies.cmake` 和 `cmake/FreeXL.cmake`。

## 输入格式

输入为第一个工作表，首行包含日期列及 `a`～`h` 八个数值列。默认按累计损益差值处理，也支持 `--nav` 和 `--daily-return`。
