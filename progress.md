# 进度日志

## 会话 1 — 2026-04-06

### 任务
理解 ChatAFL 项目并尝试跑通环境

### 进展
- [x] 阅读项目 README.md
- [x] 分析项目结构和核心组件
- [x] 创建三个规划文件
- [x] 完成 LLM 集成代码分析（chat-llm.c/h）
- [x] 确认 Docker 可用（v28.3.0）
- [x] 决定使用 MiniMax 作为 LLM 提供商
- [x] 修改 chat-llm.h - API Key 从环境变量读取
- [x] 修改 chat-llm.c - 适配 MiniMax API (endpoint + model)

### 待确认事项
- [x] 宿主机是 Windows + WSL2 ✓
- [x] Docker Desktop 已安装 ✓（Docker version 28.3.0）
- [x] 使用 MiniMax 作为 LLM 提供商

### 遇到的问题
1. ~~Docker CLI 调用位置错误~~ 已解决：现在从 Ubuntu WSL 调用 docker 正常
2. ~~Docker 镜像拉取失败~~ 已解决：清理 Docker 缓存后重试成功

### 新增决策
- **Python 环境**：使用 conda 虚拟环境 `netchat`
- **Docker 路径问题**：Windows Docker Desktop 与 Linux 容器混合环境有兼容性问题

---

## 会话 2 — 2026-04-07

### 任务
完成 ChatAFL 环境搭建并运行测试

### 进展

#### 阶段 3：实施
- [x] ✅ Docker 镜像拉取问题解决（`docker system prune -a`）
- [x] ✅ Dockerfile 添加 `ARG MINIMAX_API_KEY` 支持
- [x] ✅ 构建 3 个协议镜像：pure-ftpd(1.33GB)、live555(1.58GB)、kamailio(2.7GB)
- [x] ✅ 安装 pandas + matplotlib 到 conda netchat 环境
- [x] ✅ 修复所有 Shell 脚本 CRLF 问题（`find ... -exec dos2unix {}`）

#### 阶段 4：测试与验证
- [x] ✅ 在 WSL2 中运行 fuzzing 测试
- [x] ✅ 监控执行过程，确认 AFL 正常运行
- [x] ✅ 成功收集测试结果

### Fuzzing 测试结果
- **测试命令**: `./run.sh 1 5 pure-ftpd chatafl`
- **运行时间**: 5 分钟
- **结果位置**: `benchmark/results-pure-ftpd/`
- **结果内容**:
  - `queue/` — 测试用例队列
  - `protocol-grammars/` — LLM 生成的协议语法
  - `stall-interactions/` — LLM 交互记录
  - `cov_html/` — 覆盖率 HTML 报告
  - `plot_data` — 覆盖率时序数据
  - `fuzzer_stats` — 模糊测试统计

### 遇到的问题及解决
| 问题 | 解决 |
|------|------|
| Docker 镜像拉取失败 | `docker system prune -a` 清理缓存 |
| Windows Git Bash 路径不兼容 | 在 WSL2 Ubuntu 终端运行 |
| Shell 脚本 CRLF 行尾 | `dos2unix` 转换所有脚本 |
| Docker 容器内 CRLF 错误 | 重新构建镜像 |

### 下一步
- ~~阶段 5：整理跑通文档并交付~~ ✅ 已完成！

### 交付物
- **配置指南**: `docs/SETUP-GUIDE.md`
- **任务计划**: `task_plan.md`
- **进度日志**: `progress.md`
- **发现记录**: `findings.md`

---

## 会话 3 — 2026-04-07（下午）

### 任务
根据论文 `output_pdf/pdf_conversion_output_v4/full.md` 修订 USER-GUIDE.md 和 SETUP-GUIDE.md

### 论文关键信息
- **论文名**: 大语言模型引导的协议模糊测试技术研究 (LLMProFuzz/LPF)
- **期刊**: 信息网络安全，2025，25(12)：1847-1862
- **不是 NDSS 2024**（原文档有误）

### 修订内容

#### USER-GUIDE.md
- [x] 修正项目描述：ChatAFL → LLMProFuzz (LPF)
- [x] 修正论文出处：NDSS 2024 → 信息网络安全 2025
- [x] 更新核心能力描述（与论文一致）
- [x] 添加 plateau 阈值 Max=300 说明
- [x] 标注论文 ProFuzzBench 评估的协议
- [x] 添加论文实验结果数据（代码覆盖率 +4.59%/+5.44%，状态覆盖率 +19.78%/+3.32%）

#### SETUP-GUIDE.md
- [x] 添加论文信息附录（作者、单位、期刊）
- [x] 标注镜像与论文评估协议的差异
- [x] 添加论文实验环境信息

#### findings.md
- [x] 全面更新项目概述（与论文一致）
- [x] 更新三个 LLM 策略描述
- [x] 更新 LLM 集成配置（MiniMax 适配后）
- [x] 添加论文实验结果数据

### 后续修订（会话3补充）
- [x] 修复术语不一致：统一使用"种子富集"（论文标准术语）
- [x] 更新 findings.md 中的术语
