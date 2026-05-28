# ChatAFL 覆盖率分析工具

本目录包含用于分析和提升 ChatAFL 覆盖率的工具脚本。

## 工具列表

### 1. coverage_analysis.sh

覆盖率分析脚本，使用 gcov/lcov 插桩分析代码覆盖率。

**用法:**
```bash
./coverage_analysis.sh <变体> <时长> <目标>
```

**参数:**
- `变体`: v0, v1, v2 (默认: v0)
- `时长`: 运行时间，秒 (默认: 60)
- `目标`: rtsp, ftp, http (默认: rtsp)

**示例:**
```bash
./coverage_analysis.sh v0 120 rtsp
```

### 2. static_analysis.sh

静态分析脚本，使用 cppcheck 进行代码分析。

**用法:**
```bash
./static_analysis.sh [输出目录]
```

**参数:**
- `输出目录`: 分析结果保存目录 (默认: static_analysis_results)

**示例:**
```bash
./static_analysis.sh my_analysis
```

### 3. dynamic_analysis.sh

动态分析脚本，使用 valgrind 检测内存问题。

**用法:**
```bash
./dynamic_analysis.sh [输出目录] [目标]
```

**参数:**
- `输出目录`: 分析结果保存目录 (默认: dynamic_analysis_results)
- `目标`: rtsp, ftp, http (默认: rtsp)

**示例:**
```bash
./dynamic_analysis.sh my_analysis ftp
```

### 4. run_baseline.sh

基线实验脚本，运行所有变体的基线实验。

**用法:**
```bash
./run_baseline.sh [时长] [目标] [输出目录]
```

**参数:**
- `时长`: 每个变体的运行时间，秒 (默认: 300)
- `目标`: rtsp, ftp, http (默认: rtsp)
- `输出目录`: 结果保存目录 (默认: baseline_results)

**示例:**
```bash
./run_baseline.sh 600 ftp my_results
```

### 5. run_improved.sh

改进后实验脚本，运行改进后的实验并生成对比报告。

**用法:**
```bash
./run_improved.sh [时长] [目标] [基线目录] [改进目录]
```

**参数:**
- `时长`: 运行时间，秒 (默认: 300)
- `目标`: rtsp, ftp, http (默认: rtsp)
- `基线目录`: 基线结果目录 (默认: baseline_results/v0)
- `改进目录`: 改进结果目录 (默认: improved_results)

**示例:**
```bash
./run_improved.sh 600 ftp baseline_results/v1 my_improved
```

### 6. generate_final_report.py

最终报告生成脚本，生成最终的分析报告。

**用法:**
```bash
python3 generate_final_report.py <基线目录> <改进目录> <输出文件>
```

**参数:**
- `基线目录`: 基线结果目录
- `改进目录`: 改进结果目录
- `输出文件`: 输出报告文件

**示例:**
```bash
python3 generate_final_report.py baseline_results improved_results final_report.md
```

## 实验流程

1. **建立基线**
   ```bash
   ./run_baseline.sh 300 rtsp baseline_results
   ```

2. **分析代码**
   ```bash
   ./coverage_analysis.sh v0 60 rtsp
   ./static_analysis.sh
   ./dynamic_analysis.sh
   ```

3. **实现改进**
   - 根据分析结果修改代码
   - 重新编译

4. **运行改进后实验**
   ```bash
   ./run_improved.sh 300 rtsp baseline_results/v0 improved_results
   ```

5. **生成报告**
   ```bash
   python3 generate_final_report.py baseline_results improved_results final_report.md
   ```

## 注意事项

1. 运行实验前请确保已编译所有变体
2. 实验时长建议至少 300 秒以获得可靠结果
3. 建议在相同环境下运行基线和改进实验
4. 定期备份实验结果
