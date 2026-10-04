# 模块划分

完整的目录结构、依赖关系、数据流、构建部署和扩展规则见 [STRUCTURE.md](STRUCTURE.md)。本文保留为快速索引。

函数签名、参数约束、错误处理和使用示例见 [API.md](API.md)。

程序按数据流和职责拆分为以下层次：

```text
main.cpp
  └─ mvo_app
       ├─ cli.cpp       命令行参数解析
       ├─ io.cpp        FreeXL 工作表读取
       ├─ dates.cpp     Excel 日期序列与日期格式
       ├─ data.cpp      数据校验与输入类型转换
       ├─ preprocessing.cpp  三倍标准差处理
       ├─ moments.cpp   均值、协方差、EWMA、HAC、OAS、PSD 修复
       ├─ moment_cache.cpp  滚动调仓日矩估计缓存
       ├─ constraints.cpp   权重与换手约束
       ├─ optimization_model.cpp  LP/QP 变量和稀疏矩阵模型
       ├─ highs_solver.cpp  HiGHS LP/QP 后端
       ├─ slsqp_solver.cpp  NLopt SLSQP 后端
       ├─ solver.cpp    主求解、重试和回退策略
       ├─ evaluation.cpp  PnL、波动、Sharpe、Calmar、回撤指标
       ├─ reporting.cpp   结果格式化输出
       └─ portfolio.cpp   滚动调仓和组合收益
```

`types.hpp` 只保存跨模块的数据结构、枚举和计时工具。头文件按能力提供接口：`io.hpp` 负责文件读取，`data.hpp` 负责数据语义转换，`moments.hpp` 负责统计量，`solver.hpp` 负责优化，`evaluation.hpp` 负责指标。

求解器内部的 HiGHS 模型放在 `src/detail/optimization_model.hpp`，避免把变量布局、行约束和 KKT 辅助函数暴露给业务层。`mvo_app` 只负责组织一次运行；核心库可以独立用于单元测试或其他前端。

拆分约束：

- 模块通过头文件接口通信，不直接访问其他模块的私有实现。
- 统计模块只输出 `MomentEstimate`，不负责选择求解器。
- 求解器只接收矩估计和约束参数，不读取 Excel，也不计算绩效。
- 绩效模块只接收组合 PnL 和日期，不依赖 FreeXL。
- 回归测试固定检查拆分前的高精度和兼容模式输出。
