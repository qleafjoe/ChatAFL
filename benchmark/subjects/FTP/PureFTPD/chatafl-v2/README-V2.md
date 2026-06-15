# ChatAFL-V2：Full Validation + Feedback Retry

## 定位

启用完整验证（语法+上下文）+ 反馈重试。通过 `env.sh` 运行时切换，薄覆盖层架构。

## 运行时配置（env.sh）

```bash
AFL_LLM_VALIDATION=1         # 验证启用
AFL_LLM_VALIDATION_STRICT=1  # 完整验证（语法+上下文）
AFL_LLM_POST_GAIN=0          # 执行后增益归因禁用
# AFL_LLM_FEEDBACK 未显式设置 → 代码默认值为 1（启用）
# AFL_LLM_SKIP_STARTUP 未显式设置 → 代码默认值为 0（不跳过）
```

## 构建模型

**薄覆盖层**。`build_targets.sh` 执行两步 rsync：
1. 将 `ChatAFL/` 基线完整源码复制到目标目录
2. 将 `ChatAFL-V2/` 中的 `afl-fuzz.c` 和 `env.sh` 覆盖到目标目录

V2 目录仅包含 2 个文件：`afl-fuzz.c` 和 `env.sh`。其余所有源文件从基线继承。

## 核心特征

- 验证模式为 **Full（语法+上下文）**：完整检查 LLM 输出的格式、语法完整性和协议上下文一致性
- 反馈重试**启用**（代码默认值）：验证失败时，将错误详情反馈给 LLM 并重试（最多 3 次）
- 执行后增益归因**禁用**：不记录 LLM 生成输入是否产生新覆盖
- 种子去重、LLM 种子优先级、动态变异权重均**启用**（从基线继承）

## 验证级别说明

| 验证级别 | V2 状态 | 说明 |
|---------|---------|------|
| Format（格式） | **ON** | 检查消息基本格式合法性 |
| Grammar（语法） | **ON** | 检查协议语法完整性（如必要头字段） |
| Context（上下文） | **ON** | 检查协议状态一致性（如 Session/CSeq） |

## 与 V1 的关键差异

V1 和 V2 共享**完全相同的 `afl-fuzz.c`**（MD5 一致），唯一区别在 `env.sh`：

| 配置 | V1 | V2 |
|------|----|----|
| `AFL_LLM_VALIDATION_STRICT` | **0**（仅格式） | **1**（语法+上下文） |
| `AFL_LLM_POST_GAIN` | 0 | 0 |

V2 相比 V1 增加了语法和上下文级验证，这意味着：
- LLM 生成的 RTSP 消息必须包含必要的头字段（如 CSeq、Session）
- LLM 生成的 FTP 命令必须符合协议状态机（如 PASS 必须在 USER 之后）
- 验证失败的反馈信息更详细，有助于 LLM 生成更正输出

## 与基线 ChatAFL 的关键差异

V2 与基线共享相同的验证级别，差异仅在 `AFL_LLM_POST_GAIN`：

| 配置 | V2 | 基线 ChatAFL |
|------|----| ------------|
| `AFL_LLM_VALIDATION` | 1 | 1 |
| `AFL_LLM_VALIDATION_STRICT` | 1 | 1 |
| `AFL_LLM_POST_GAIN` | **0** | **1** |
| `AFL_LLM_FEEDBACK` | 1（默认） | 1 |

V2 禁用了执行后增益归因，基线启用了该功能。

## 行为总结

V2 是验证框架的**最高配置**（除执行后增益归因外）。完整验证确保 LLM 输出在格式、语法和上下文三个层面均符合协议规范，验证失败后通过反馈重试给 LLM 修正机会。消融实验中用于测量"完整验证+反馈重试"相比"仅格式验证"（V1）和"无验证"（V0）的贡献。
