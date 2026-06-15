#!/usr/bin/env python3
import matplotlib.pyplot as plt
import numpy as np

# Data
fuzzers = ['aflnet\n(baseline)', 'chatafl\n(full)', 'chatafl-v0\n(no validation)', 'chatafl-v1\n(format)', 'chatafl-v2\n(full validation)']
line_coverage = [24.0, 24.2, 24.6, 24.2, 24.3]
branch_coverage = [15.3, 15.3, 15.8, 15.4, 15.5]
queue_size = [1001, 1015, 993, 767, 997]
exec_speed = [12.25, 10.55, 6.33, 11.41, 0.90]
hangs = [53, 24, 72, 78, 55]

# Create figure with subplots
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

# Plot 1: Coverage Comparison
x = np.arange(len(fuzzers))
width = 0.35
axes[0, 0].bar(x - width/2, line_coverage, width, label='Line Coverage', color='skyblue')
axes[0, 0].bar(x + width/2, branch_coverage, width, label='Branch Coverage', color='lightcoral')
axes[0, 0].set_ylabel('Coverage (%)')
axes[0, 0].set_title('Coverage Comparison')
axes[0, 0].set_xticks(x)
axes[0, 0].set_xticklabels(fuzzers, fontsize=8)
axes[0, 0].legend()
axes[0, 0].grid(axis='y', alpha=0.3)

# Plot 2: Queue Size
axes[0, 1].bar(fuzzers, queue_size, color='lightgreen')
axes[0, 1].set_ylabel('Queue Size')
axes[0, 1].set_title('Queue Size (Test Cases)')
axes[0, 1].set_xticklabels(fuzzers, fontsize=8)
axes[0, 1].grid(axis='y', alpha=0.3)

# Plot 3: Execution Speed
axes[1, 0].bar(fuzzers, exec_speed, color='gold')
axes[1, 0].set_ylabel('Executions/second')
axes[1, 0].set_title('Execution Speed')
axes[1, 0].set_xticklabels(fuzzers, fontsize=8)
axes[1, 0].grid(axis='y', alpha=0.3)

# Plot 4: Hangs Detected
axes[1, 1].bar(fuzzers, hangs, color='salmon')
axes[1, 1].set_ylabel('Number of Hangs')
axes[1, 1].set_title('Hangs Detected')
axes[1, 1].set_xticklabels(fuzzers, fontsize=8)
axes[1, 1].grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig('/home/leaf/ChatAFL/ablation_results.png', dpi=150, bbox_inches='tight')
print("Plot saved to /home/leaf/ChatAFL/ablation_results.png")
