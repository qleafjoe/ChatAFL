# ChatAFL-TR3：Hard Validation + Feedback Retry

## 定位

引入严格验证框架和反馈重试机制。LLM 输出必须通过协议级验证，失败后可重试。

## 消融配置

| 标志 | 值 | 说明 |
|------|----|------|
| Hard Validation | **ON** | 严格验证（格式+语法+上下文） |
| Feedback Retry | **ON (3次)** | 验证失败时反馈错误信息并重试 |
| Soft Accept | OFF | 无概率软接受 |
| Contextual Recovery | OFF | 无上下文修复 |
| 状态转换图 | 保留 | 协议一致性检查启用 |
| 上下文感知拒绝检测 | 保留 | 协议指标预检查 |
| LF→CRLF 转换 | 保留 | 协议格式修正 |
| 指数退避重试 | 保留 | 指数退避（上限 30s） |
| 语法解析鲁棒性 | 保留 | JSON 防御性检查 |

## 编译时常量

```c
TR_LLM_VALIDATION        = 1   // 启用验证
TR_LLM_VALIDATION_STRICT  = 1   // 严格模式（格式+语法+上下文）
TR_LLM_POST_GAIN          = 0   // 禁用执行后增益归因
TR_LLM_FEEDBACK           = 1   // 启用反馈重试
TR_LLM_FEEDBACK_MAX_RETRIES = 3 // 最大重试次数
```

## 核心特征

- LLM 输出必须通过 `validate_llm_message()` / `validate_llm_sequence()` 验证
- 验证失败时，将错误详情反馈给 LLM 并请求修正（最多 3 次）
- 重试仍失败则**拒绝**该种子，不进入模糊测试循环
- 无软接受机制：验证不通过就是不通过
- 无种子去重、无动态变异权重、无 LLM 种子优先级

## 与 TR1/TR2 的关键差异

### 新增组件
- `llm-validator.c` / `llm-validator.h` — 完整验证框架
- `aflnet.c/h` 恢复状态转换图和协议一致性检查
- `chat-llm.c` 恢复至与基线几乎一致（1644 行）

### afl-fuzz.c（11454 行 vs 基线 11594 行，-140 行）
- 验证标志硬编码为编译时常量（不依赖 env.sh）
- 移除了种子去重哈希表和去重逻辑
- 移除了动态变异阶段权重和 `adjust_stage_weights()`
- 移除了 LLM 种子优先级系统
- 移除了运行时环境变量解析（`env_flag_enabled()` 辅助函数）

## 行为总结

TR3 是第一个引入验证层的变体。LLM 输出经过严格的协议级验证，失败后通过反馈重试给 LLM 修正机会。消融实验中用于隔离"验证+反馈重试"这一组合的贡献，排除软接受和上下文修复的干扰。
