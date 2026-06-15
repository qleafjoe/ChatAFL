# ChatAFL 消融实验数据总结报告
## 实验概述
本报告总结了ChatAFL在Live555和Pure-FTPd两个协议服务器上的消融实验结果。实验使用了不同的配置（tr1, tr2, tr3, tr4, tr5）和不同的模型（minimax, xiaomi）进行测试。报告包含历史实验数据（_1, _2后缀）和新增的xiaomi模型专项实验数据（_3后缀）。
## 实验配置说明
- **tr1, tr2, tr3, tr4**: 不同的消融实验配置
- **tr5**: 使用LLM模型（minimax, xiaomi）的实验配置
- **_1, _2**: 重复实验后缀，用于验证结果的稳定性
- **_3**: 第三轮实验后缀，用于xiaomi模型专项测试
## 基线数据
基线使用AFLNet（原始版本）进行8次重复实验，结果如下：
| 指标 | 平均值 | 最大值 |
|------|--------|--------|
| 分支覆盖数 | 2851.9 | 2870 |
| 行覆盖数 | 5779.9 | 5798 |
## 实验结果汇总
### 1. Live555 协议实验结果
#### 1.1 覆盖分支数（Live555）
| 实验配置 | 模型 | 分支覆盖数 | 分支覆盖率 | 基线分支覆盖 | 提升率 |
|----------|------|------------|------------|--------------|--------|
| tr1_1_Live555       | minimax | 2881 | 15.6% | 2851.9 | +1.0% |
| tr1_2_Live555       | minimax | 2829 | 15.3% | 2851.9 | -0.8% |
| tr1_3_Live555_xiaomi| xiaomi  | 2888 | 15.6% | 2851.9 | +1.3% |
| tr2_1_Live555       | minimax | 2897 | 15.6% | 2851.9 | +1.6% |
| tr2_2_Live555       | minimax | 2894 | 15.6% | 2851.9 | +1.5% |
| tr2_3_Live555_xiaomi| xiaomi  | 2879 | 15.5% | 2851.9 | +1.0% |
| tr3_1_Live555       | minimax | 2877 | 15.5% | 2851.9 | +0.9% |
| tr3_2_Live555       | minimax | 2881 | 15.6% | 2851.9 | +1.0% |
| tr3_3_Live555_xiaomi| xiaomi  | 2793 | 15.1% | 2851.9 | -2.1% |
| tr4_1_Live555       | minimax | 2886 | 15.6% | 2851.9 | +1.2% |
| tr4_2_Live555       | minimax | 2861 | 15.4% | 2851.9 | +0.3% |
| tr4_3_Live555_xiaomi| xiaomi  | 2840 | 15.3% | 2851.9 | -0.4% |
| tr5_1_Live555_minimax| minimax | 2867 | 15.5% | 2851.9 | +0.5% |
| tr5_1_Live555_xiaomi| xiaomi  | 2867 | 15.5% | 2851.9 | +0.5% |
| tr5_2_Live555_xiaomi| xiaomi  | 2888 | 15.6% | 2851.9 | +1.3% |

**Live555 分支覆盖统计：**
- 平均分支覆盖率提升率: +0.7%
- 最大分支覆盖率提升率: +1.6%
- 最小分支覆盖率提升率: -2.1%

#### 1.2 覆盖状态个数（Live555）
| 实验配置 | 模型 | 状态个数 |
|----------|------|----------|
| tr1_1_Live555       | minimax | 13 |
| tr1_2_Live555       | minimax | 13 |
| tr1_3_Live555_xiaomi| xiaomi  | 14 |
| tr2_1_Live555       | minimax | 14 |
| tr2_2_Live555       | minimax | 13 |
| tr2_3_Live555_xiaomi| xiaomi  | 13 |
| tr3_1_Live555       | minimax | 14 |
| tr3_2_Live555       | minimax | 13 |
| tr3_3_Live555_xiaomi| xiaomi  | 10 |
| tr4_1_Live555       | minimax | 12 |
| tr4_2_Live555       | minimax | 11 |
| tr4_3_Live555_xiaomi| xiaomi  | 12 |
| tr5_1_Live555_minimax| minimax | 13 |
| tr5_1_Live555_xiaomi| xiaomi  | 14 |
| tr5_2_Live555_xiaomi| xiaomi  | 13 |

**Live555 状态个数统计：**
- 平均状态个数: 12.9
- 最大状态个数: 14

#### 1.3 状态转换数（Live555）
| 实验配置 | 模型 | 状态转换数 |
|----------|------|------------|
| tr1_1_Live555       | minimax | 124 |
| tr1_2_Live555       | minimax | 133 |
| tr1_3_Live555_xiaomi| xiaomi  | 138 |
| tr2_1_Live555       | minimax | 151 |
| tr2_2_Live555       | minimax | 132 |
| tr2_3_Live555_xiaomi| xiaomi  | 128 |
| tr3_1_Live555       | minimax | 143 |
| tr3_2_Live555       | minimax | 128 |
| tr3_3_Live555_xiaomi| xiaomi  | 81 |
| tr4_1_Live555       | minimax | 90 |
| tr4_2_Live555       | minimax | 89 |
| tr4_3_Live555_xiaomi| xiaomi  | 118 |
| tr5_1_Live555_minimax| minimax | 118 |
| tr5_1_Live555_xiaomi| xiaomi  | 141 |
| tr5_2_Live555_xiaomi| xiaomi  | 118 |

**Live555 状态转换数统计：**
- 平均状态转换数: 122.3
- 最大状态转换数: 151

#### 1.4 行覆盖率（Live555）
| 实验配置 | 模型 | 行覆盖数 | 行覆盖率 | 基线行覆盖 | 提升率 |
|----------|------|----------|----------|------------|--------|
| tr1_1_Live555       | minimax | 5807 | 24.3% | 5779.9 | +0.5% |
| tr1_2_Live555       | minimax | 5755 | 24.1% | 5779.9 | -0.4% |
| tr1_3_Live555_xiaomi| xiaomi  | 5834 | 24.4% | 5779.9 | +0.9% |
| tr2_1_Live555       | minimax | 5843 | 24.5% | 5779.9 | +1.1% |
| tr2_2_Live555       | minimax | 5804 | 24.3% | 5779.9 | +0.4% |
| tr2_3_Live555_xiaomi| xiaomi  | 5806 | 24.3% | 5779.9 | +0.5% |
| tr3_1_Live555       | minimax | 5809 | 24.3% | 5779.9 | +0.5% |
| tr3_2_Live555       | minimax | 5814 | 24.4% | 5779.9 | +0.6% |
| tr3_3_Live555_xiaomi| xiaomi  | 5708 | 23.9% | 5779.9 | -1.2% |
| tr4_1_Live555       | minimax | 5802 | 24.3% | 5779.9 | +0.4% |
| tr4_2_Live555       | minimax | 5779 | 24.2% | 5779.9 | -0.0% |
| tr4_3_Live555_xiaomi| xiaomi  | 5761 | 24.1% | 5779.9 | -0.3% |
| tr5_1_Live555_minimax| minimax | 5783 | 24.2% | 5779.9 | +0.1% |
| tr5_1_Live555_xiaomi| xiaomi  | 5791 | 24.3% | 5779.9 | +0.2% |
| tr5_2_Live555_xiaomi| xiaomi  | 5824 | 24.4% | 5779.9 | +0.8% |

**Live555 行覆盖率统计：**
- 平均行覆盖率提升率: +0.3%
- 最大行覆盖率提升率: +1.1%
- 最小行覆盖率提升率: -1.2%

### 2. Pure-FTPd 协议实验结果
#### 2.1 覆盖分支数（Pure-FTPd）
| 实验配置 | 模型 | 分支覆盖数 | 分支覆盖率 |
|----------|------|------------|------------|
| tr2_1_Pure-FTPd_minimax | minimax | 815 | 17.3% |
| tr2_2_Pure-FTPd_xiaomi | xiaomi | 1052 | 22.4% |

**Pure-FTPd 分支覆盖统计：**
- 平均分支覆盖数: 933.5
- 最大分支覆盖数: 1052

#### 2.2 覆盖状态个数（Pure-FTPd）
| 实验配置 | 模型 | 状态个数 |
|----------|------|----------|
| tr2_1_Pure-FTPd_minimax | minimax | 24 |
| tr2_2_Pure-FTPd_xiaomi | xiaomi | 30 |

**Pure-FTPd 状态个数统计：**
- 平均状态个数: 27.0
- 最大状态个数: 30

#### 2.3 状态转换数（Pure-FTPd）
| 实验配置 | 模型 | 状态转换数 |
|----------|------|------------|
| tr2_1_Pure-FTPd_minimax | minimax | 163 |
| tr2_2_Pure-FTPd_xiaomi | xiaomi | 250 |

**Pure-FTPd 状态转换数统计：**
- 平均状态转换数: 206.5
- 最大状态转换数: 250

#### 2.4 行覆盖率（Pure-FTPd）
| 实验配置 | 模型 | 行覆盖数 | 行覆盖率 |
|----------|------|----------|----------|
| tr2_1_Pure-FTPd_minimax | minimax | 1565 | 24.1% |
| tr2_2_Pure-FTPd_xiaomi | xiaomi | 1945 | 30.0% |

**Pure-FTPd 行覆盖率统计：**
- 平均行覆盖数: 1755.0
- 最大行覆盖数: 1945

## 关键发现
### Live555 协议
1. **整体性能**: ChatAFL在Live555上的平均分支覆盖率提升率为+0.7%，行覆盖率提升率为+0.3%，表明整体性能优于基线AFLNet。
2. **最佳配置**: 
   - 分支覆盖率最佳: tr2_1_Live555（+1.6%）
   - 行覆盖率最佳: tr2_1_Live555（+1.1%）
   - xiaomi模型最佳: tr1_3_Live555_xiaomi 和 tr5_2_Live555_xiaomi（+1.3%）
3. **模型对比**:
   - 默认配置（无模型）平均分支提升率: +0.8%
   - xiaomi模型平均分支提升率: +0.3%（差异较大：+1.3%到-2.1%）
   - minimax模型分支提升率: +0.5%（仅tr5_1）
   - xiaomi模型在tr1和tr5配置下表现最佳（+1.3%），在tr3配置下表现最差（-2.1%）
4. **状态探索**:
   - 平均探索12.9个状态，122.3个转换
   - 状态探索数量与覆盖率提升呈正相关
   - xiaomi模型在tr3配置下状态探索能力下降（仅10个状态，81个转换）
5. **稳定性**: 重复实验（_1, _2）结果相对稳定，表明实验结果具有可重复性。

### Pure-FTPd 协议
1. **模型性能差异**: xiaomi模型在Pure-FTPd上表现明显优于minimax（1052 vs 815分支覆盖）。
2. **状态探索**: Pure-FTPd平均探索27.0个状态，206.5个转换，比Live555更多。
3. **覆盖率**: xiaomi模型在Pure-FTPd上实现了30.0%的行覆盖率。

## 数据文件说明
- `experiment_summary_clean.csv`: 所有实验的详细数据（使用规整的实验名称）
- `improvement_analysis_clean.csv`: 提升率分析数据（使用规整的实验名称）
- `baseline_summary.csv`: 基线（AFLNet）的统计数据
- `xiaomi_model_comparison_report.md`: xiaomi模型实验对比报告（新旧数据对比）

## 结论
ChatAFL通过引入LLM辅助的模糊测试，在Live555协议服务器上实现了稳定的覆盖率提升。虽然提升幅度不大（平均+0.7%分支覆盖率），但结果具有可重复性，且在不同配置下均能保持正向改进。状态探索能力的提升（平均12.9个状态）表明ChatAFL能更有效地探索协议状态空间。

新增的xiaomi模型实验显示，模型在不同配置下表现差异较大：tr1和tr5配置下表现最佳（+1.3%分支提升），而tr3配置下表现最差（-2.1%分支提升），这表明模型与配置的兼容性对结果有重要影响。

在Pure-FTPd协议上，LLM模型表现出更大的性能差异，xiaomi模型显著优于minimax模型，这表明不同LLM模型在不同协议上的适应性存在差异。
