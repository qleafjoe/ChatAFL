# Validation-Driven ChatAFL Paper Roadmap

## Summary

This roadmap is the single source of truth for the validation-driven ChatAFL paper track.

The immediate goal is not to expand protocol coverage or add new LLM features. The immediate goal is to make the ablation matrix real, Docker-compatible, and publication-ready:

- `AFLNet`: external baseline
- `ChatAFL-V0`: LLM-enabled baseline with no validation
- `ChatAFL-V1`: format-only validation
- `ChatAFL-V2`: full pre-execution validation
- `ChatAFL`: `V2` plus post-execution gain attribution

If `V1` and `V2` do not differ at runtime, formal experiments must not start.

## Docker Compatibility Contract

All implementation and experiment changes must preserve the current one-command workflow:

`run.sh -> profuzzbench_exec_all.sh -> profuzzbench_exec_common.sh -> target run.sh`

Hard requirements:

- Keep the external command shape unchanged: `./run.sh NUM_CONTAINERS TIMEOUT TARGET FUZZER`
- Keep protocol target startup logic unchanged
- Keep `FUZZER`-selected binary layout unchanged, including `/home/ubuntu/${FUZZER}/afl-fuzz`
- Keep Docker-based execution as the default experiment environment

Allowed script-side adjustment:

- Before executing `run ${FUZZER} ...` inside the container, optionally source `${WORKDIR}/${FUZZER}/env.sh`
- If `${WORKDIR}/${FUZZER}/env.sh` does not exist, behavior must remain identical to the current workflow

Environment precedence:

- Explicit environment variables passed from the outer shell remain authoritative
- `${WORKDIR}/${FUZZER}/env.sh` only provides variant-local defaults
- No new top-level CLI arguments are introduced for this phase

## Frozen Experiment Semantics

The experiment matrix is fixed as follows.

### AFLNet

- Traditional baseline
- No LLM assistance

### ChatAFL-V0

- LLM enabled
- No validator
- No validation-driven filtering
- No format-level gate beyond the existing baseline behavior

### ChatAFL-V1

- `AFL_LLM_VALIDATION=1`
- `AFL_LLM_VALIDATION_STRICT=0`
- Format-only validation

`V1` may do:

- response cleanup
- markdown / code-fence stripping
- line-ending normalization
- minimal structural sanity checks

`V1` must not do:

- grammar validation
- context validation
- post-execution gain attribution

### ChatAFL-V2

- `AFL_LLM_VALIDATION=1`
- `AFL_LLM_VALIDATION_STRICT=1`
- Full pre-execution validation

`V2` includes everything in `V1`, plus:

- grammar validation
- context validation

`V2` does not include:

- post-execution gain attribution
- `NO_GAIN` classification

### ChatAFL

- Inherits `V2` pre-execution behavior
- Adds post-execution gain attribution
- Adds `NO_GAIN` as a first-class result for accepted-but-useless candidates

## Validator Behavior Contract

`AFL_LLM_VALIDATION_STRICT` must be a real behavior switch, not a passive flag.

Runtime contract:

- if `AFL_LLM_VALIDATION=0`: no validation path
- if `AFL_LLM_VALIDATION=1` and `AFL_LLM_VALIDATION_STRICT=0`: use format-only validation
- if `AFL_LLM_VALIDATION=1` and `AFL_LLM_VALIDATION_STRICT=1`: use full validation

This dispatch rule must be applied consistently to all LLM-assisted paths:

- grammar extraction
- seed enrichment
- stall breaking

It must no longer be possible for `V1` and `V2` to differ only by directory name or nominal `env.sh` contents.

## Validation Taxonomy

The validator result taxonomy is fixed:

- `PASS`
- `FORMAT_FAIL`
- `GRAMMAR_FAIL`
- `CONTEXT_FAIL`
- `NO_GAIN`

Variant constraints:

- `V1` may only emit `PASS` or `FORMAT_FAIL`
- `V2` may emit `PASS`, `FORMAT_FAIL`, `GRAMMAR_FAIL`, `CONTEXT_FAIL`
- `V2` must not emit `NO_GAIN`
- `ChatAFL` is the only variant allowed to emit `NO_GAIN`

Implementation constraint:

- context-related failures must not be collapsed into `GRAMMAR_FAIL`

Minimum grammar-extraction gates required in this phase:

- mandatory-field gate
- seed-match or known-message gate

Do not expand this phase with additional advanced gates unless they are needed to restore correctness.

## Execution Phases

### Phase 1: Variant Activation Fix

Goal:

- make `V0/V1/V2/ChatAFL` receive distinct runtime configuration inside Docker

Exit criteria:

- one-click Docker workflow still starts successfully
- `V1` and `V2` show different effective validation configuration at runtime

### Phase 2: Pre-Execution Validation Separation

Goal:

- cleanly separate format-only validation from full validation

Exit criteria:

- `V1` rejects format failures only
- `V2` rejects grammar and context failures in addition to format failures
- the same candidate set produces different failure distributions for `V1` and `V2`

### Phase 3: Full-System Gain Attribution

Goal:

- make `ChatAFL` a true full system rather than another pre-validation variant

Required post-execution fields:

- `response_code_seq`
- `has_new_cov`
- `has_new_state`
- `has_new_transition`
- `fault`
- `exec_us`

Exit criteria:

- accepted-but-useless candidates can be classified as `NO_GAIN`
- `ChatAFL` logs are sufficient to compute effective acceptance and per-call gain

### Phase 4: Pilot Experiments

Scope:

- `PureFTPD`
- `Live555`

Exit criteria:

- Docker smoke runs succeed for `V1`, `V2`, and `ChatAFL`
- pilot curves show meaningful separation between at least `V0`, `V1`, and `V2`
- if `V1` and `V2` remain nearly identical, stop and debug before scaling experiments

## Test Plan

### Docker Compatibility Smoke Tests

For both `PureFTPD` and `Live555`:

- launch `chatafl-v1`
- launch `chatafl-v2`
- launch `chatafl`

Checks:

- container starts successfully
- target service starts successfully
- fuzzing loop begins successfully
- sourcing variant `env.sh` does not break shell execution

### Validation Separation Tests

Prepare representative LLM candidates for:

- malformed format
- format-correct but protocol-invalid content
- format-correct but context-invalid content
- pre-validation pass but post-execution no-gain content

Expected behavior:

- `V1` rejects only malformed format
- `V2` rejects malformed, grammar-invalid, and context-invalid inputs
- `ChatAFL` additionally labels accepted-but-useless inputs as `NO_GAIN`

### Paper-Readiness Checks

Before formal experiments, verify that logs support:

- pre-acceptance rate
- effective acceptance rate
- failure-type distribution
- per-call gain
- useless-input ratio

## Assumptions and Defaults

- This roadmap replaces prior ambiguity and is the authoritative plan for the paper track
- The current mainline paper scope is limited to `PureFTPD` and `Live555`
- The external experiment interface remains unchanged in this phase
- The first priority is to make the ablation valid, not to widen protocol coverage
