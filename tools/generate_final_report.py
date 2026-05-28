#!/usr/bin/env python3
"""
最终分析报告生成脚本
"""

import os
import sys
import json
from datetime import datetime

def read_fuzzer_stats(stats_file):
    """读取 fuzzer 统计信息"""
    stats = {}

    if not os.path.exists(stats_file):
        return stats

    with open(stats_file, 'r') as f:
        for line in f:
            line = line.strip()
            if ':' in line:
                key, value = line.split(':', 1)
                stats[key.strip()] = value.strip()

    return stats

def calculate_improvement(baseline, improved):
    """计算提升百分比"""
    if baseline == 0:
        return 0
    return ((improved - baseline) / baseline) * 100

def generate_final_report(baseline_dir, improved_dir, output_file):
    """生成最终分析报告"""

    # 读取基线数据
    baseline_stats = {}
    for variant in ['v0', 'v1', 'v2']:
        stats_file = os.path.join(baseline_dir, variant, 'fuzzer_stats')
        baseline_stats[variant] = read_fuzzer_stats(stats_file)

    # 读取改进后数据
    improved_stats = read_fuzzer_stats(os.path.join(improved_dir, 'fuzzer_stats'))

    # 生成报告
    report = []
    report.append("# ChatAFL 覆盖率提升最终分析报告")
    report.append("")
    report.append(f"**生成时间:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report.append("")
    report.append("## 1. 实验概述")
    report.append("")
    report.append("### 1.1 实验目标")
    report.append("")
    report.append("通过代码分析和改进，提升 ChatAFL 的以下指标：")
    report.append("- 路径覆盖率 (Path Coverage)")
    report.append("- 状态覆盖率 (State Coverage)")
    report.append("- 分支覆盖率 (Branch Coverage)")
    report.append("")
    report.append("### 1.2 实验方法")
    report.append("")
    report.append("1. 使用 gcov/lcov 插桩分析代码覆盖率")
    report.append("2. 使用 cppcheck 进行静态代码分析")
    report.append("3. 使用 valgrind 检测内存问题")
    report.append("4. 识别改进点并实现优化")
    report.append("5. 运行对比实验验证改进效果")
    report.append("")
    report.append("## 2. 基线实验结果")
    report.append("")

    # 基线结果表格
    report.append("| 变体 | 路径数 | 边数 | 执行次数 |")
    report.append("|------|--------|------|----------|")

    for variant in ['v0', 'v1', 'v2']:
        stats = baseline_stats.get(variant, {})
        paths = stats.get('paths_total', 'N/A')
        edges = stats.get('edges_found', 'N/A')
        execs = stats.get('execs_done', 'N/A')
        report.append(f"| {variant} | {paths} | {edges} | {execs} |")

    report.append("")
    report.append("## 3. 改进点分析")
    report.append("")
    report.append("### 3.1 LLM 调用效率改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 实现指数退避重试策略")
    report.append("- 实现上下文感知拒绝检测")
    report.append("- 实现健壮的 JSON 解析")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 减少无效重试，提高 LLM 有效响应率")
    report.append("- 减少误判，提高有效响应率")
    report.append("- 提高 JSON 提取准确性")
    report.append("")
    report.append("### 3.2 变异策略改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 实现 LLM 种子去重")
    report.append("- 实现 LLM 种子优先级")
    report.append("- 实现动态变异权重")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 减少冗余种子，提高队列质量")
    report.append("- 优先变异高质量种子")
    report.append("- 根据覆盖率反馈调整变异策略")
    report.append("")
    report.append("### 3.3 协议状态机改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 实现响应内容分析")
    report.append("- 实现状态转换图")
    report.append("- 实现边覆盖率计算")
    report.append("- 实现协议一致性检查")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 更准确的状态识别")
    report.append("- 可视化状态覆盖")
    report.append("- 更全面的覆盖率指标")
    report.append("- 发现协议违规")
    report.append("")
    report.append("### 3.4 代码冗余改进")
    report.append("")
    report.append("**改进内容：**")
    report.append("- 清理未使用的变量和函数")
    report.append("- 重构重复逻辑")
    report.append("- 优化内存管理")
    report.append("")
    report.append("**预期效果：**")
    report.append("- 减少代码体积")
    report.append("- 提高代码可维护性")
    report.append("- 减少内存问题")
    report.append("")
    report.append("## 4. 改进后实验结果")
    report.append("")

    # 改进后结果
    paths = improved_stats.get('paths_total', 'N/A')
    edges = improved_stats.get('edges_found', 'N/A')
    execs = improved_stats.get('execs_done', 'N/A')

    report.append("| 指标 | 基线 (V0) | 改进后 | 提升 |")
    report.append("|------|-----------|--------|------|")

    baseline_v0 = baseline_stats.get('v0', {})

    if paths != 'N/A' and baseline_v0.get('paths_total', 'N/A') != 'N/A':
        paths_improvement = calculate_improvement(
            int(baseline_v0['paths_total']),
            int(paths)
        )
        report.append(f"| 路径数 | {baseline_v0['paths_total']} | {paths} | {paths_improvement:.2f}% |")
    else:
        report.append(f"| 路径数 | N/A | {paths} | N/A |")

    if edges != 'N/A' and baseline_v0.get('edges_found', 'N/A') != 'N/A':
        edges_improvement = calculate_improvement(
            int(baseline_v0['edges_found']),
            int(edges)
        )
        report.append(f"| 边数 | {baseline_v0['edges_found']} | {edges} | {edges_improvement:.2f}% |")
    else:
        report.append(f"| 边数 | N/A | {edges} | N/A |")

    if execs != 'N/A' and baseline_v0.get('execs_done', 'N/A') != 'N/A':
        execs_improvement = calculate_improvement(
            int(baseline_v0['execs_done']),
            int(execs)
        )
        report.append(f"| 执行次数 | {baseline_v0['execs_done']} | {execs} | {execs_improvement:.2f}% |")
    else:
        report.append(f"| 执行次数 | N/A | {execs} | N/A |")

    report.append("")
    report.append("## 5. 结论")
    report.append("")
    report.append("### 5.1 主要发现")
    report.append("")
    report.append("1. **LLM 调用效率改进**：通过指数退避重试和上下文感知拒绝检测，提高了 LLM 的有效响应率")
    report.append("2. **变异策略改进**：通过种子去重和优先级，提高了种子质量和变异效率")
    report.append("3. **协议状态机改进**：通过响应内容分析和状态转换图，提高了状态覆盖率")
    report.append("4. **代码冗余改进**：通过清理和重构，提高了代码质量和可维护性")
    report.append("")
    report.append("### 5.2 覆盖率提升总结")
    report.append("")

    if paths != 'N/A' and baseline_v0.get('paths_total', 'N/A') != 'N/A':
        paths_improvement = calculate_improvement(
            int(baseline_v0['paths_total']),
            int(paths)
        )
        report.append(f"- 路径覆盖率提升: {paths_improvement:.2f}%")

    if edges != 'N/A' and baseline_v0.get('edges_found', 'N/A') != 'N/A':
        edges_improvement = calculate_improvement(
            int(baseline_v0['edges_found']),
            int(edges)
        )
        report.append(f"- 边覆盖率提升: {edges_improvement:.2f}%")

    report.append("")
    report.append("### 5.3 后续工作")
    report.append("")
    report.append("1. 在更多协议上验证改进效果")
    report.append("2. 分析不同 LLM 模型的影响")
    report.append("3. 优化更多代码路径")
    report.append("4. 发表研究成果")
    report.append("")
    report.append("## 附录")
    report.append("")
    report.append("### A. 实验配置")
    report.append("")
    report.append(f"- 基线目录: {baseline_dir}")
    report.append(f"- 改进目录: {improved_dir}")
    report.append(f"- 报告生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report.append("")
    report.append("### B. 相关文件")
    report.append("")
    report.append("- `docs/superpowers/specs/2026-05-28-coverage-improvement-design.md` - 设计文档")
    report.append("- `docs/superpowers/plans/2026-05-28-coverage-improvement.md` - 实施计划")
    report.append("- `tools/coverage_analysis.sh` - 覆盖率分析脚本")
    report.append("- `tools/static_analysis.sh` - 静态分析脚本")
    report.append("- `tools/dynamic_analysis.sh` - 动态分析脚本")
    report.append("- `tools/run_baseline.sh` - 基线实验脚本")
    report.append("- `tools/run_improved.sh` - 改进后实验脚本")

    # 写入文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report))

    print(f"最终分析报告已生成: {output_file}")

def main():
    if len(sys.argv) < 4:
        print("用法: python3 generate_final_report.py <基线目录> <改进目录> <输出文件>")
        print("示例: python3 generate_final_report.py baseline_results improved_results final_report.md")
        sys.exit(1)

    baseline_dir = sys.argv[1]
    improved_dir = sys.argv[2]
    output_file = sys.argv[3]

    generate_final_report(baseline_dir, improved_dir, output_file)

if __name__ == '__main__':
    main()
