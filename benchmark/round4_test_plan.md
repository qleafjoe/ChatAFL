# 第4轮实验计划 - 多协议消融对比

## 实验目标

统一运行时间（800分钟），公平对比 AFLNet 基线与 ChatAFL 各消融配置（tr1-tr5）+ xiaomi 模型，覆盖三个协议（RTSP/FTP/HTTP）。

## 实验设计

### Phase 1: Live555 (RTSP 协议)

| # | 实验 | Fuzzer | 结果目录 | 模型 |
|---|------|--------|----------|------|
| 1 | AFLNet 基线 | aflnet | `results-live555-aflnet_4` | 无 |
| 2 | tr1 + xiaomi | chatafl-tr1 | `results-live555-tr1_xiaomi_4` | xiaomi |
| 3 | tr2 + xiaomi | chatafl-tr2 | `results-live555-tr2_xiaomi_4` | xiaomi |
| 4 | tr3 + xiaomi | chatafl-tr3 | `results-live555-tr3_xiaomi_4` | xiaomi |
| 5 | tr4 + xiaomi | chatafl-tr4 | `results-live555-tr4_xiaomi_4` | xiaomi |
| 6 | tr5 + xiaomi | chatafl-tr5 | `results-live555-tr5_xiaomi_4` | xiaomi |

**参数**: `-P RTSP -D 10000 -q 3 -s 3 -E -K -R -m none`

### Phase 2: Pure-FTPd (FTP 协议)

| # | 实验 | Fuzzer | 结果目录 | 模型 |
|---|------|--------|----------|------|
| 7 | AFLNet 基线 | aflnet | `results-pure-ftpd-aflnet_4` | 无 |
| 8 | tr1 + xiaomi | chatafl-tr1 | `results-pure-ftpd-tr1_xiaomi_4` | xiaomi |
| 9 | tr2 + xiaomi | chatafl-tr2 | `results-pure-ftpd-tr2_xiaomi_4` | xiaomi |
| 10 | tr3 + xiaomi | chatafl-tr3 | `results-pure-ftpd-tr3_xiaomi_4` | xiaomi |
| 11 | tr4 + xiaomi | chatafl-tr4 | `results-pure-ftpd-tr4_xiaomi_4` | xiaomi |
| 12 | tr5 + xiaomi | chatafl-tr5 | `results-pure-ftpd-tr5_xiaomi_4` | xiaomi |

**参数**: `-m none -P FTP -D 10000 -q 3 -s 3 -E -K -t ${TEST_TIMEOUT}+`

### Phase 3: Lighttpd1 (HTTP 协议)

| # | 实验 | Fuzzer | 结果目录 | 模型 |
|---|------|--------|----------|------|
| 13 | AFLNet 基线 | aflnet | `results-lighttpd1-aflnet_4` | 无 |
| 14 | tr1 + xiaomi | chatafl-tr1 | `results-lighttpd1-tr1_xiaomi_4` | xiaomi |
| 15 | tr2 + xiaomi | chatafl-tr2 | `results-lighttpd1-tr2_xiaomi_4` | xiaomi |
| 16 | tr3 + xiaomi | chatafl-tr3 | `results-lighttpd1-tr3_xiaomi_4` | xiaomi |
| 17 | tr4 + xiaomi | chatafl-tr4 | `results-lighttpd1-tr4_xiaomi_4` | xiaomi |
| 18 | tr5 + xiaomi | chatafl-tr5 | `results-lighttpd1-tr5_xiaomi_4` | xiaomi |

**参数**: `-P HTTP -D 200000 -m none -q 3 -s 3 -E -K -R -t ${TEST_TIMEOUT}+`

## 实验参数

```bash
NUM_CONTAINERS=1       # 每个实验1个容器
TIMEOUT=48000          # 800分钟 = 48000秒
SKIPCOUNT=1            # 每个测试用例后计算覆盖率
TEST_TIMEOUT=20000     # 测试超时 20000ms
```

## xiaomi 模型配置

```bash
LLM_URL="https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
LLM_TOKEN="tp-c0dk3e4p9i6sevv1jnlfzcqrfp7xerjggdo5bl0342j9vcxv"
LLM_MODEL="mimo-v2.5-pro"
```

## 执行命令

### Step 1: 构建 Docker 镜像（首次运行或代码更新后）

```bash
cd /home/leaf/ChatAFL
./build_targets.sh
```

这将构建三个 Docker 镜像：
- `pure-ftpd` (PureFTPD + aflnet/chatafl/tr1-tr5)
- `live555` (Live555 + aflnet/chatafl/tr1-tr5)
- `lighttpd1` (Lighttpd1 + aflnet/chatafl/cl1/cl2/tr1-tr5)

### Step 2: 运行实验

```bash
cd /home/leaf/ChatAFL/benchmark
./run_round4_all.sh
```

## 执行顺序

1. Phase 1 (Live555) 6个实验**并行**运行，全部完成后
2. Phase 2 (Pure-FTPd) 6个实验**并行**运行，全部完成后
3. Phase 3 (Lighttpd1) 6个实验**并行**运行

总实验数: 18 (6×3)，每个实验1个容器，总计18个 Docker 容器，总预期时长: ~2400分钟 (~40小时)

## 为什么需要第4轮实验

之前实验的问题：
1. **基线AFLNet跑了24小时**（results.csv中8次运行），而ChatAFL实验只跑了141-802分钟
2. **比较不公平**：用更短时间的ChatAFL去对比24小时的AFLNet基线
3. **实验时长不一致**：tr1_1跑了423分钟，tr4_2跑了802分钟

第4轮实验统一800分钟，确保公平对比。

## 数据提取

实验完成后执行：

```bash
cd /home/leaf/ChatAFL/benchmark

# Live555 结果
echo "=== Live555 ==="
for dir in results-live555-aflnet_4 results-live555-tr*_xiaomi_4; do
    csv=$(find "$dir" -name "cov_over_time.csv" | head -1)
    if [ -n "$csv" ]; then
        last=$(tail -1 "$csv")
        name=$(basename "$dir")
        echo "$name: $last"
    fi
done

# Pure-FTPd 结果
echo ""
echo "=== Pure-FTPd ==="
for dir in results-pure-ftpd-aflnet_4 results-pure-ftpd-tr*_xiaomi_4; do
    csv=$(find "$dir" -name "cov_over_time.csv" | head -1)
    if [ -n "$csv" ]; then
        last=$(tail -1 "$csv")
        name=$(basename "$dir")
        echo "$name: $last"
    fi
done

# Lighttpd1 结果
echo ""
echo "=== Lighttpd1 ==="
for dir in results-lighttpd1-aflnet_4 results-lighttpd1-tr*_xiaomi_4; do
    csv=$(find "$dir" -name "cov_over_time.csv" | head -1)
    if [ -n "$csv" ]; then
        last=$(tail -1 "$csv")
        name=$(basename "$dir")
        echo "$name: $last"
    fi
done
```

## 修改记录

- **Lighttpd1 Dockerfile**: 添加了 chatafl-tr1 到 chatafl-tr5 的编译支持
- **build_targets.sh**: 将 Lighttpd1 添加到 TARGETS 列表，自动复制 tr1-tr5 源码
- **profuzzbench_exec_all.sh**: 为 Lighttpd1 添加了 chatafl-tr1 到 chatafl-tr5 的执行支持
- **run_round4_all.sh**: Phase 3 从 LightFTP 改为 Lighttpd1，包含完整的 tr1-tr5 实验
