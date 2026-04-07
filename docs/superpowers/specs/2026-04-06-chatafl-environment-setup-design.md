# ChatAFL 环境搭建设计方案

**日期：** 2026-04-06
**状态：** 已批准

## 目标

在 Windows + WSL2 环境下，完整搭建 ChatAFL fuzzing 环境，包含：
- 适配 MiniMax API（而非 OpenAI）
- 选择性构建 3 个协议的 fuzzing 镜像
- 验证环境可用性

## 环境约束

| 项目 | 约束 |
|------|------|
| 磁盘空间 | 中等 (20-50GB) |
| API Key 处理 | 通过 Dockerfile 传入，不硬编码 |
| 测试范围 | 3 个协议：pure-ftpd、live555、kamailio |

## 实施步骤

### Step 1：诊断并解决 Docker 镜像拉取问题

**问题：** `docker pull ubuntu:20.04` 失败，报 `failed size validation` 错误

**解决方案（按顺序尝试）：**
1. `docker pull ubuntu:20.04` — 手动拉取
2. `docker system prune -a` — 清理缓存后重试
3. 重启 Docker Desktop

### Step 2：修改 Dockerfile 支持 API Key 注入

**文件：** `benchmark/subjects/*/Dockerfile`（每个 subject 都有独立 Dockerfile）

**修改内容：**
1. 添加 `ARG MINIMAX_API_KEY` 在 `FROM ubuntu:20.04` 之后
2. 添加 `ENV MINIMAX_API_KEY=${MINIMAX_API_KEY}` 让运行时可用

**说明：** API Key 通过 `--build-arg` 传入，构建时注入，代码中不存储

### Step 3：选择性构建镜像

**目标镜像（3个）：**
- `benchmark/subjects/FTP/PureFTPD/`
- `benchmark/subjects/RTSP/Live555/`
- `benchmark/subjects/SIP/Kamailio/`

**跳过（节省空间）：**
- DAAP, BFTPD, LightFTP, ProFTPD, HTTP, SMTP, DNS, IPP, DICOM, TinyDTLS

**构建命令（修复后）：**
```bash
cd benchmark
MINIMAX_API_KEY=xxx ./scripts/execution/profuzzbench_build_all.sh
```

### Step 4：验证 Python 环境

**检查项：**
```bash
python3 --version  # 期望：Python 3.x
pip3 list | grep -E "pandas|matplotlib"  # 期望：有输出
```

**如缺失：** 运行 `pip3 install pandas matplotlib`

## 验证标准

- `docker images` 显示已构建的镜像
- `python3 --version` 正常输出
- `run.sh 1 5 pure-ftpd chatafl` 能成功启动（无需等待完成）

## 风险与应对

| 风险 | 应对 |
|------|------|
| Docker 镜像持续拉取失败 | 尝试 `docker system prune -a` 或重启 Docker Desktop |
| 磁盘空间不足 | 删除不需要的 subject 目录，只保留 3 个目标 |
| API Key 格式问题 | 确认 MiniMax API Key 格式正确（sk-...） |
