# ChatAFL-TR1：基线 LLM 辅助模糊测试

## 定位

最简版本，原始 ChatAFL 行为 + MiniMax LLM 适配器。无任何 LLM 输出质量控制。

## 消融配置

| 标志 | 值 | 说明 |
|------|----|------|
| Hard Validation | OFF | 无验证框架 |
| Feedback Retry | OFF | 无反馈重试 |
| Soft Accept | OFF | 无概率软接受 |
| Contextual Recovery | OFF | 无上下文修复 |
| 状态转换图 | 移除 | 无协议一致性检查 |
| 上下文感知拒绝检测 | 移除 | 仅关键词匹配 |
| LF→CRLF 转换 | 移除 | 无协议格式修正 |
| 指数退避重试 | 移除 | 简单 sleep(1)/sleep(2) |

## 核心特征

- 调用 LLM 进行语法提取、种子丰富和停滞恢复，但**无任何质量控制**
- 所有 LLM 生成的种子直接接受并 fuzz，无拒绝机制
- `chat-llm.c` 使用简单的关键词拒绝检测（无协议感知）
- `chat-llm.c` 使用简单重试（`sleep(1)`/`sleep(2)`），无指数退避
- 移除了种子去重、动态变异权重、LLM 种子优先级

## 与基线的关键差异

### 移除的组件
- `llm-validator.c` / `llm-validator.h` — 验证框架完全移除
- `test_llm.c` / `test/` — 测试基础设施移除
- `aflnet.c` 中的状态转换图、`parse_rtsp_response()`、`check_rtsp_consistency()` 等函数

### chat-llm.c 简化（1348 行 vs 基线 1647 行）
- 移除 `parse_or_create_messages()`、`ensure_curl_init()`、`lf_to_crlf()`
- 移除上下文感知拒绝检测（无协议指标预检查）
- JSON 提取跳过 `json_tokener_parse()`，直接用正则
- 每次请求后调用 `curl_global_cleanup()`（基线延迟到进程退出）

### afl-fuzz.c 简化（10933 行 vs 基线 11594 行，-661 行）
- 移除所有 `AFL_LLM_*` 标志声明和运行时环境变量解析
- 移除种子去重哈希表（`seed_hash_table`、`is_seed_duplicate()`）
- 移除动态变异阶段权重（`stage_weights[]`、`adjust_stage_weights()`）
- 移除 LLM 种子优先级系统（`is_llm_seed`、`llm_priority`）
- 移除执行后增益跟踪（`last_llm_exec_fault` 等）
- 移除所有验证相关代码和反馈重试调用

## 行为总结

纯 LLM 辅助模糊测试，无质量控制层。LLM 输出直接进入模糊测试循环，无法拒绝格式错误或协议不兼容的生成结果。
