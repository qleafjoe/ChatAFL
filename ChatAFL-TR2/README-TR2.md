# ChatAFL-TR2：TR1 + PromptClean

## 定位

在 TR1 基础上增加语法解析鲁棒性（PromptClean），其余行为与 TR1 一致。

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
| 语法解析鲁棒性 | **ON** | 唯一与 TR1 的区别 |

## 与 TR1 的唯一差异

在 `afl-fuzz.c` 的 `setup_llm_grammars()` 函数中增加了鲁棒性检查：

- 对 `jobj`、`header`、`field_obj` 增加 null/类型检查
- 访问 JSON 数组/字符串前进行防御性验证
- 增加 `printf` 诊断输出，便于定位无效语法条目

**代码量变化**：`afl-fuzz.c` 从 10933 行增至 10946 行（+13 行）

## 与 TR1 共有的特征

- 无验证框架（`llm-validator.c/h` 不存在）
- 无反馈重试、无软接受、无上下文修复
- `chat-llm.c` 使用简单关键词拒绝检测和简单重试
- 状态转换图、协议一致性检查已移除
- 所有 LLM 生成的种子直接接受

## 新增文件（与 TR1 相比）

- `test_llm.c` — LLM 单元测试
- `test/` — 测试目录

## 行为总结

TR2 是 TR1 的防御性增强版本。唯一的行为差异是语法解析阶段的 JSON 鲁棒性检查，用于防止 LLM 返回的无效 JSON 导致崩溃。消融实验中用于隔离"PromptClean"这一改进的贡献。
