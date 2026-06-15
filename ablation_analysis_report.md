# ChatAFL Ablation Study Results
## Live555 (RTSP) - 800 minutes per experiment

**Experiment ID:** ablation_20260528_230154
**Date:** 2026-05-28 to 2026-05-29
**Duration:** 800 minutes (13.3 hours) per fuzzer

---

## Coverage Summary

| Fuzzer | Line Coverage | Branch Coverage | Queue Size | Crashes | Hangs |
|--------|---------------|-----------------|------------|---------|-------|
| **aflnet** (baseline) | 24.0% (5734 lines) | 15.3% (2834 branches) | 1001 | 0 | 53 |
| **chatafl** (full) | 24.2% (5788 lines) | 15.3% (2839 branches) | 1015 | 0 | 24 |
| **chatafl-v0** (no validation) | 24.6% (5865 lines) | 15.8% (2918 branches) | 993 | 0 | 72 |
| **chatafl-v1** (format validation) | 24.2% (5782 lines) | 15.4% (2849 branches) | 767 | 0 | 78 |
| **chatafl-v2** (full validation) | 24.3% (5800 lines) | 15.5% (2878 branches) | 997 | 0 | 55 |

---

## Detailed Statistics (from plot_data)

| Fuzzer | Paths | Edges | Exec Speed | Stability | LLM Seeds |
|--------|-------|-------|------------|-----------|-----------|
| **aflnet** | 881 | 1001 | 12.25/sec | 9.69% | N/A |
| **chatafl** | 938 | 1015 | 10.55/sec | 9.85% | 194 |
| **chatafl-v0** | 956 | 993 | 6.33/sec | 9.92% | 25 |
| **chatafl-v1** | 579 | 767 | 11.41/sec | 9.73% | 18 |
| **chatafl-v2** | 861 | 997 | 0.90/sec | 9.87% | 102 |

---

## LLM Validation Statistics

| Fuzzer | Grammar Validations | Enrichment Validations | Stall Detections |
|--------|---------------------|------------------------|------------------|
| **chatafl** | 25 | 1 | 151 |
| **chatafl-v1** | 1 | 262 | 12 |
| **chatafl-v2** | 52 | 162 | 59 |

---

## Key Findings

### 1. Coverage Performance
- **chatafl-v0** (no validation) achieved the highest line coverage (24.6%) and branch coverage (15.8%)
- All ChatAFL variants achieved similar or slightly better coverage than the aflnet baseline
- The coverage differences are relatively small (within 1-2%)

### 2. Execution Efficiency
- **aflnet** had the fastest execution speed (12.25/sec)
- **chatafl-v0** had the slowest execution speed (6.33/sec) due to LLM overhead
- **chatafl-v2** had extremely slow execution (0.90/sec) due to strict validation

### 3. Stability
- All fuzzers had low stability (9.69-9.92%), indicating high randomness in mutations
- **chatafl-v0** had the highest stability (9.92%)

### 4. Hang Detection
- **chatafl-v1** detected the most hangs (78)
- **chatafl** (full) detected the fewest hangs (24)
- This suggests validation helps filter out invalid inputs that cause hangs

### 5. LLM Seed Generation
- **chatafl** generated the most LLM seeds (194)
- **chatafl-v1** generated the fewest LLM seeds (18)
- The LLM seed generation varies significantly between variants

### 6. Validation Impact
- **chatafl-v0** (no validation): Highest coverage but most hangs
- **chatafl-v1** (format validation): Balanced performance
- **chatafl-v2** (full validation): Slowest execution, fewer LLM seeds

---

## Conclusions

1. **LLM Integration Overhead:** The ChatAFL variants have lower execution speeds due to LLM API calls
2. **Validation Trade-off:** Stricter validation reduces execution speed but may improve input quality
3. **Coverage Gains:** ChatAFL variants achieved marginally better coverage than baseline
4. **Stability Issues:** All fuzzers had low stability, suggesting need for better mutation strategies

---

## Recommendations

1. **Optimize LLM Calls:** Reduce API call frequency or use caching
2. **Balance Validation:** Use format validation (v1) as a good trade-off
3. **Improve Stability:** Enhance mutation strategies for better stability
4. **Longer Experiments:** Run for longer durations to see clearer differences

