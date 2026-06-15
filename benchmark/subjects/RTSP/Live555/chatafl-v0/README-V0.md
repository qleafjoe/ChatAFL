# ChatAFL-V0：LLM 基线（无验证）

## 定位

LLM 辅助模糊测试基线，通过 `env.sh` 禁用所有验证和质量控制功能。独立完整源码树，非薄覆盖层。

## 运行时配置（env.sh）

```bash
AFL_LLM_VALIDATION=0         # 验证禁用
AFL_LLM_VALIDATION_STRICT=0  # 不适用
AFL_LLM_POST_GAIN=0          # 执行后增益归因禁用
AFL_LLM_FEEDBACK=0           # 反馈重试禁用
```

## 构建模型

**独立完整源码树**。V0 包含完整的 AFL/AFLNet 源码副本，`build_targets.sh` 直接从 `ChatAFL-V0/` 目录 rsync。

## 核心特征

V0 本质上是一个**较旧的代码分支**，早于验证框架、种子管理改进和状态转换图功能。与基线的差异不仅是 `env.sh` 配置，源码本身也缺少大量特性。

### 缺失的组件（与基线相比）

- `llm-validator.c` / `llm-validator.h` — 验证框架不存在
- 种子去重哈希表（`seed_hash_table`、`is_seed_duplicate()`）
- 动态变异阶段权重（`adjust_stage_weights()`）
- LLM 种子优先级系统（`is_llm_seed`、`llm_priority`）
- 状态转换图（`add_state_transition()`、`calculate_edge_coverage()`、`check_rtsp_consistency()`）
- RTSP 响应解析（`parse_rtsp_response()`、`response_info_t`）
- 执行后增益日志（`classify_llm_execution_gain()`）
- 反馈重试函数（`llm_feedback_retry_stall()`、`llm_feedback_retry_enrichment()`）
- 上下文感知拒绝检测、LF→CRLF 转换、指数退避重试

### chat-llm.c 简化（1348 行 vs 基线 1647 行）

- 缺失 `enrich_sequence_with_prompt()`、`parse_or_create_messages()`、`ensure_curl_init()`、`lf_to_crlf()`
- 简单关键词拒绝检测（无协议感知）
- 简单重试（`sleep(1)`/`sleep(2)`），无指数退避
- 硬编码回退 API token

### afl-fuzz.c 简化（10946 行 vs 基线 11594 行，-648 行）

- 无 `#include "llm-validator.h"`
- 无任何 `AFL_LLM_*` 环境变量读取
- 无验证步骤：LLM 生成的停滞恢复消息直接进入 `common_fuzz_stuff()`
- 无验证步骤：丰富种子直接写入磁盘
- 使用旧版 `enrich_sequence()`（不捕获提示输出）

## 与 TR 变体的关系

V0 的行为特征与 **TR1/TR2** 高度相似：
- 无验证、无反馈重试、无软接受
- `chat-llm.c` 和 `aflnet.c` 的简化程度与 TR1/TR2 一致
- 主要区别在于构建模型：V0 是独立源码树，TR1/TR2 也是独立源码树但通过不同的构建流程

## 行为总结

纯 LLM 辅助模糊测试，无任何质量控制。所有 LLM 生成的种子直接接受，所有 LLM 生成的停滞恢复消息直接执行。作为消融实验的基线，用于测量引入验证框架后的整体改进幅度。
