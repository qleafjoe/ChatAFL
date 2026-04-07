# 任务计划：跑通 ChatAFL 项目运行环境

## 目标
理解 ChatAFL 项目架构与依赖，成功搭建并运行 fuzzing 环境

## 当前阶段
阶段 5（交付）

## 设计文档
- `docs/superpowers/specs/2026-04-06-chatafl-environment-setup-design.md`

## 各阶段

### 阶段 1：需求与发现
- [x] 阅读项目 README，理解整体架构
- [x] 分析依赖项（Docker、Bash、Python3、pandas、matplotlib）
- [x] 梳理项目目录结构
- [x] 分析各脚本功能（setup.sh、run.sh、analyze.sh、deps.sh、clean.sh）
- [x] 分析 LLM 集成代码（chat-llm.c/h）
- [x] 将发现记录到 findings.md
- **状态：** complete ✅

### 阶段 2：规划与结构
- [x] 确定宿主机环境（Windows + WSL2 ✓）
- [x] 识别环境依赖差距（Docker 已确认，LLM 需要适配 MiniMax）
- [x] 制定安装计划
- [x] 记录决策及理由
- **状态：** complete ✅

### 阶段 3：实施（方案1）
- [x] Step 1: 诊断并解决 Docker 镜像拉取问题
- [x] Step 2: 修改 Dockerfile 支持 API Key 注入
- [x] Step 3: 选择性构建镜像（3个协议）— pure-ftpd/live555/kamailio
- [x] Step 4: 验证 Python 环境（conda netchat）
- [x] Step 5: 修复 Shell 脚本 CRLF 行尾问题
- **状态：** complete ✅

### 阶段 4：测试与验证
- [x] 在 WSL2 环境中运行 fuzzing 测试
- [x] 监控 fuzzing 执行过程
- [x] 成功收集测试结果
- **状态：** complete ✅

### 阶段 5：交付
- [x] 整理跑通文档 (`docs/SETUP-GUIDE.md`)
- [x] 梳理关键步骤和注意事项
- [x] 交付给用户
- **状态：** complete ✅

## 关键问题
1. [已解决] 宿主机 Windows + WSL2 ✓
2. [已解决] Docker Desktop 已安装（v28.3.0）✓
3. [已解决] 使用 **MiniMax** 作为 LLM 提供商 ✓
4. [已解决] Shell 脚本 CRLF 行尾问题 ✓

## 已做决策
| 决策 | 理由 |
|------|------|
| WSL2 环境 | README 推荐 Linux 环境，WSL2 最适合 Windows |
| MiniMax LLM | 用户选择，国内可用 |
| MiniMax API | OpenAI 兼容格式，endpoint: api.minimax.io/v1 |
| Python 环境 | conda 虚拟环境 `netchat` |
| API Key 注入 | 通过 Dockerfile ARG 传入，不硬编码 |
| 构建协议 | 只构建 pure-ftpd、live555、kamailio（节省空间）|

## 遇到的错误
| 错误 | 尝试次数 | 解决方案 |
|------|---------|---------|
| Docker 镜像拉取失败 (ubuntu:20.04) | 1 | 执行 `docker system prune -a` 清理缓存后成功 |
| Windows Git Bash 路径不兼容 | 1 | 在 WSL2 Ubuntu 终端中运行脚本 |
| Shell 脚本 CRLF 行尾 | 1 | `find ... -exec dos2unix {} \;` 转换 |
| Docker 容器内脚本 CRLF | 1 | 重新构建镜像（使用 Unix LF 格式的脚本）|

## 备注
- 所有 Shell 脚本已转换为 Unix LF 格式
- Docker 镜像需要用 LF 格式的脚本重新构建
- fuzzing 测试结果：`benchmark/results-pure-ftpd/`
