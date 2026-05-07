# Phase 2: Validation-Driven LLM Fuzzing - Completion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the remaining tasks from the Validation-Driven LLM Fuzzing implementation (Tasks 10-11, fixes, and test coverage).

**Architecture:** Extend the existing `llm-validator` framework with grammar pattern validation, add `AFL_LLM_VALIDATION_STRICT` environment variable, fix CSV logging fields, and supplement test coverage for FTP/HTTP sequences.

**Tech Stack:** C, AFL/AFLNet, pcre2, llm-validator framework

---

## Current Status

**Completed (10/14 tasks):**
- ✅ Task 1-2: Validator framework (llm-validator.h/c)
- ✅ Task 3-5: RTSP/FTP/HTTP validators
- ✅ Task 6: Unit tests (41 tests)
- ✅ Task 7: Makefile integration
- ✅ Task 8-9: Stall Breaking + Seed Enrichment integration
- ✅ Task 12: Validation log initialization

**Remaining:**
- ❌ Task 10: Grammar Extraction integration
- ❌ Task 11: Integration tests
- ❌ Fix: CSV logging fields incomplete
- ❌ Fix: Missing `AFL_LLM_VALIDATION_STRICT` env var
- ❌ Fix: FTP/HTTP sequence tests missing

---

## File Structure

```
ChatAFL/
├── llm-validator.h          # Modify: add validate_grammar_pattern() declaration
├── llm-validator.c          # Modify: add validate_grammar_pattern() implementation
├── afl-fuzz.c               # Modify: add AFL_LLM_VALIDATION_STRICT, integrate grammar validation
├── test/
│   ├── test_llm_validator.c # Modify: add grammar pattern tests, sequence tests
│   └── test_llm_integration.c # Create: integration tests (optional)
└── docs/superpowers/plans/
    └── 2026-05-07-phase2-completion.md  # This plan
```

---

### Task 1: Add `validate_grammar_pattern()` Function

**Files:**
- Modify: `ChatAFL/llm-validator.h:150-155`
- Modify: `ChatAFL/llm-validator.c:425-430`

- [ ] **Step 1: Add declaration to header**

In `llm-validator.h`, add after `validate_http_request_message()`:

```c
/*
 * validate_grammar_pattern - Validate a grammar pattern (message type)
 * @message_type: The message type string (e.g., "OPTIONS", "USER", "GET")
 * @protocol: Protocol name ("RTSP", "FTP", "HTTP")
 *
 * Return: 1 if valid, 0 if invalid
 *
 * Checks whether the extracted message type is in the protocol's whitelist.
 * RTSP/FTP: case-insensitive (RFC 2326, RFC 959)
 * HTTP: case-sensitive (RFC 7230)
 */
int validate_grammar_pattern(const char *message_type, const char *protocol);
```

- [ ] **Step 2: Add implementation**

In `llm-validator.c`, add after `validate_http_request_message()`:

```c
int validate_grammar_pattern(const char *message_type, const char *protocol) {
    if (!message_type || !protocol) return 0;

    if (strcmp(protocol, "RTSP") == 0) {
        for (int i = 0; rtsp_methods[i]; i++) {
            if (strcasecmp(message_type, rtsp_methods[i]) == 0) return 1;
        }
    } else if (strcmp(protocol, "FTP") == 0) {
        for (int i = 0; ftp_commands[i]; i++) {
            if (strcasecmp(message_type, ftp_commands[i]) == 0) return 1;
        }
    } else if (strcmp(protocol, "HTTP") == 0) {
        for (int i = 0; http_methods[i]; i++) {
            if (strcmp(message_type, http_methods[i]) == 0) return 1;
        }
    }
    return 0;
}
```

- [ ] **Step 3: Verify compilation**

Run: `cd /home/leaf/ChatAFL/ChatAFL && gcc -c -o llm-validator.o llm-validator.c -I.`
Expected: No errors

- [ ] **Step 4: Commit**

```bash
git add llm-validator.h llm-validator.c
git commit -m "feat: add validate_grammar_pattern() for grammar extraction validation"
```

---

### Task 2: Add `AFL_LLM_VALIDATION_STRICT` Environment Variable

**Files:**
- Modify: `ChatAFL/afl-fuzz.c:432-433`
- Modify: `ChatAFL/afl-fuzz.c:10702-10705`

- [ ] **Step 1: Add global variable**

In `afl-fuzz.c`, after line 433 (`u8 afl_llm_validation_permissive = 0;`):

```c
u8 afl_llm_validation_strict = 0;     // AFL_LLM_VALIDATION_STRICT=0/1
```

- [ ] **Step 2: Read environment variable**

In `afl-fuzz.c`, after the `AFL_LLM_VALIDATION_PERMISSIVE` check (around line 10705):

```c
if (getenv("AFL_LLM_VALIDATION_STRICT")) afl_llm_validation_strict = 1;
```

- [ ] **Step 3: Verify compilation**

Run: `cd /home/leaf/ChatAFL/ChatAFL && make afl-fuzz`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add afl-fuzz.c
git commit -m "feat: add AFL_LLM_VALIDATION_STRICT environment variable"
```

---

### Task 3: Integrate Grammar Extraction Validation

**Files:**
- Modify: `ChatAFL/afl-fuzz.c:547-553`

- [ ] **Step 1: Read current code**

Read `afl-fuzz.c` lines 540-560 to understand the integration point.

- [ ] **Step 2: Add validation gate**

In `afl-fuzz.c`, replace lines 547-553:

```c
      char *message_type = extract_message_pattern(header_str, field_table, patterns, pattern_fd, pattern_path);
      if (message_type != NULL)
      {
        /* Grammar validation: check if message type is in protocol whitelist */
        if (afl_llm_validation && !validate_grammar_pattern(message_type, protocol_name)) {
          llm_validation_record_t record = {0};
          record.stage = LLM_STAGE_GRAMMAR;
          snprintf(record.reason, sizeof(record.reason), "grammar_pattern_fail:%s", message_type);
          log_llm_validation_record(&record);

          if (!afl_llm_validation_permissive) {
            ck_free(message_type);
            // Free patterns (pcre2_code pointers)
            if (patterns[0]) pcre2_code_free(patterns[0]);
            if (patterns[1]) pcre2_code_free(patterns[1]);
            ck_free(patterns);
            json_object_put(header_v);
            close(pattern_fd);
            ck_free(pattern_path);
            continue;
          }
        }

        int discard;
        kh_put(strSet, message_types_set, message_type, &discard);
        *kl_pushp(rang, protocol_patterns) = patterns;
      }
```

- [ ] **Step 3: Verify compilation**

Run: `cd /home/leaf/ChatAFL/ChatAFL && make afl-fuzz`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add afl-fuzz.c
git commit -m "feat: integrate grammar extraction validation (Task 10)"
```

---

### Task 4: Fix CSV Logging Fields

**Files:**
- Modify: `ChatAFL/llm-validator.h:85-94` (extend struct)
- Modify: `ChatAFL/llm-validator.c:56-86` (update logging)

- [ ] **Step 1: Extend `llm_validation_record_t` struct**

In `llm-validator.h`, update the struct:

```c
typedef struct {
  llm_generation_stage_t stage;
  llm_validation_result_t result;
  char reason[128];
  u32 protocol_type;        // PROTOCOL_RTSP, PROTOCOL_FTP, PROTOCOL_HTTP
  u32 seed_id;              // Seed index (0 if unknown)
  u32 input_bytes;          // Raw input size
  u32 normalized_bytes;     // Normalized input size
  u32 region_count;
  u32 state_count;
  u8 has_new_cov;
  u8 has_new_state;
  u8 has_new_transition;
} llm_validation_record_t;
```

- [ ] **Step 2: Update logging function**

In `llm-validator.c`, update `log_llm_validation_record()`:

```c
void log_llm_validation_record(const llm_validation_record_t *record) {
    FILE *log = NULL;
    switch (record->stage) {
        case LLM_STAGE_GRAMMAR: log = grammar_log; break;
        case LLM_STAGE_ENRICHMENT: log = enrichment_log; break;
        case LLM_STAGE_STALL: log = stall_log; break;
    }

    if (log) {
        fprintf(log, "%u,%d,%u,%u,%u,%d,%s,%u,%u,%u,%u,%s,%u,%u,%u,%u,%u\n",
                (unsigned)time(NULL),
                record->stage,
                record->protocol_type,
                record->seed_id,
                0, // llm_call_id (not tracked yet)
                record->result,
                record->reason,
                record->input_bytes,
                record->normalized_bytes,
                record->region_count,
                record->state_count,
                "", // response_code_seq (not tracked yet)
                record->has_new_cov,
                record->has_new_state,
                record->has_new_transition,
                0, // fault (not tracked yet)
                0  // exec_us (not tracked yet)
        );
    }
}
```

- [ ] **Step 3: Update integration code in afl-fuzz.c**

Update the validation blocks in stall breaking (line 7024-7044) and seed enrichment (line 2759-2782) to set the new fields:

For stall breaking:
```c
if (stall_message != NULL && afl_llm_validation) {
    llm_validation_record_t record = {0};
    record.stage = LLM_STAGE_STALL;
    record.input_bytes = strlen(stall_message);

    protocol_context_t ctx = {0};
    if (strcmp(protocol_name, "RTSP") == 0) { ctx.type = PROTOCOL_RTSP; record.protocol_type = PROTOCOL_RTSP; }
    else if (strcmp(protocol_name, "FTP") == 0) { ctx.type = PROTOCOL_FTP; record.protocol_type = PROTOCOL_FTP; }
    else if (strcmp(protocol_name, "HTTP") == 0) { ctx.type = PROTOCOL_HTTP; record.protocol_type = PROTOCOL_HTTP; }

    // ... rest of validation
}
```

- [ ] **Step 4: Verify compilation**

Run: `cd /home/leaf/ChatAFL/ChatAFL && make afl-fuzz`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add llm-validator.h llm-validator.c afl-fuzz.c
git commit -m "fix: extend CSV logging fields for ablation analysis"
```

---

### Task 5: Add Grammar Pattern Unit Tests

**Files:**
- Modify: `ChatAFL/test/test_llm_validator.c`

- [ ] **Step 1: Add grammar pattern tests**

In `test_llm_validator.c`, add after the Normalizer tests:

```c
static void test_grammar_pattern(void) {
    printf("[Grammar Pattern Tests]\n");

    // RTSP patterns (case-insensitive)
    ASSERT_TRUE(validate_grammar_pattern("OPTIONS", "RTSP"));
    ASSERT_TRUE(validate_grammar_pattern("options", "RTSP"));
    ASSERT_TRUE(validate_grammar_pattern("PLAY", "RTSP"));
    ASSERT_TRUE(validate_grammar_pattern("SETUP", "RTSP"));
    ASSERT_FALSE(validate_grammar_pattern("INVALID", "RTSP"));
    ASSERT_FALSE(validate_grammar_pattern("", "RTSP"));

    // FTP patterns (case-insensitive)
    ASSERT_TRUE(validate_grammar_pattern("USER", "FTP"));
    ASSERT_TRUE(validate_grammar_pattern("user", "FTP"));
    ASSERT_TRUE(validate_grammar_pattern("PASS", "FTP"));
    ASSERT_TRUE(validate_grammar_pattern("RETR", "FTP"));
    ASSERT_FALSE(validate_grammar_pattern("HACK", "FTP"));

    // HTTP patterns (case-sensitive)
    ASSERT_TRUE(validate_grammar_pattern("GET", "HTTP"));
    ASSERT_TRUE(validate_grammar_pattern("POST", "HTTP"));
    ASSERT_FALSE(validate_grammar_pattern("get", "HTTP"));  // case-sensitive
    ASSERT_FALSE(validate_grammar_pattern("INVALID", "HTTP"));

    // NULL inputs
    ASSERT_FALSE(validate_grammar_pattern(NULL, "RTSP"));
    ASSERT_FALSE(validate_grammar_pattern("OPTIONS", NULL));

    printf("  PASS: All grammar pattern tests\n");
}
```

- [ ] **Step 2: Add to main()**

In `main()`, add `test_grammar_pattern();` after `test_normalizer();`

- [ ] **Step 3: Run tests**

Run: `cd /home/leaf/ChatAFL/ChatAFL && ./test/test_llm_validator`
Expected: All tests pass including new grammar pattern tests

- [ ] **Step 4: Commit**

```bash
git add test/test_llm_validator.c
git commit -m "test: add grammar pattern validation tests"
```

---

### Task 6: Add FTP/HTTP Sequence Tests

**Files:**
- Modify: `ChatAFL/test/test_llm_validator.c`

- [ ] **Step 1: Add FTP sequence tests**

In `test_llm_validator.c`, add after the existing sequence tests:

```c
static void test_ftp_sequence(void) {
    printf("[FTP Sequence Tests]\n");
    protocol_context_t ctx = {0};

    // Valid FTP sequence: USER + PASS + RETR
    const char *ftp_seq = "USER admin\r\nPASS secret\r\nRETR file.txt\r\n";
    llm_validation_result_t result = validate_llm_sequence("FTP", LLM_STAGE_ENRICHMENT, ftp_seq, &ctx);
    ASSERT_EQ(result, LLM_VALID_OK);
    ASSERT_TRUE(ctx.ctx.ftp.is_authed);

    // Invalid: PASS before USER
    ctx = (protocol_context_t){0};
    const char *ftp_invalid = "PASS secret\r\nUSER admin\r\n";
    result = validate_llm_sequence("FTP", LLM_STAGE_ENRICHMENT, ftp_invalid, &ctx);
    ASSERT_NEQ(result, LLM_VALID_OK);

    // Invalid: RETR without auth
    ctx = (protocol_context_t){0};
    const char *ftp_no_auth = "RETR file.txt\r\n";
    result = validate_llm_sequence("FTP", LLM_STAGE_ENRICHMENT, ftp_no_auth, &ctx);
    ASSERT_NEQ(result, LLM_VALID_OK);

    printf("  PASS: All FTP sequence tests\n");
}
```

- [ ] **Step 2: Add HTTP sequence tests**

```c
static void test_http_sequence(void) {
    printf("[HTTP Sequence Tests]\n");
    protocol_context_t ctx = {0};

    // Valid HTTP sequence: GET + POST
    const char *http_seq = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n"
                           "POST /submit HTTP/1.1\r\nHost: example.com\r\nContent-Length: 5\r\n\r\nhello";
    llm_validation_result_t result = validate_llm_sequence("HTTP", LLM_STAGE_ENRICHMENT, http_seq, &ctx);
    ASSERT_EQ(result, LLM_VALID_OK);

    // Invalid: missing separator
    ctx = (protocol_context_t){0};
    const char *http_invalid = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n";
    result = validate_llm_sequence("HTTP", LLM_STAGE_ENRICHMENT, http_invalid, &ctx);
    ASSERT_NEQ(result, LLM_VALID_OK);

    printf("  PASS: All HTTP sequence tests\n");
}
```

- [ ] **Step 3: Add to main()**

In `main()`, add:
```c
test_ftp_sequence();
test_http_sequence();
```

- [ ] **Step 4: Run tests**

Run: `cd /home/leaf/ChatAFL/ChatAFL && ./test/test_llm_validator`
Expected: All tests pass

- [ ] **Step 5: Commit**

```bash
git add test/test_llm_validator.c
git commit -m "test: add FTP and HTTP sequence validation tests"
```

---

### Task 7: Final Build Verification

- [ ] **Step 1: Clean build**

Run: `cd /home/leaf/ChatAFL/ChatAFL && make clean && make afl-fuzz`
Expected: Build succeeds

- [ ] **Step 2: Run all tests**

Run: `cd /home/leaf/ChatAFL/ChatAFL && ./test/test_llm_validator`
Expected: All tests pass (50+ tests)

- [ ] **Step 3: Verify environment variables**

Run: `AFL_LLM_VALIDATION=1 AFL_LLM_VALIDATION_STRICT=1 AFL_LLM_VALIDATION_PERMISSIVE=1 ./afl-fuzz --help 2>&1 | head -5`
Expected: No crash, help text displayed

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "chore: phase 2 completion - grammar validation, tests, fixes"
```

---

## Summary

**Total tasks:** 7
**Estimated time:** 30-45 minutes
**Key deliverables:**
1. `validate_grammar_pattern()` function
2. `AFL_LLM_VALIDATION_STRICT` environment variable
3. Grammar extraction validation integration
4. Extended CSV logging fields
5. Grammar pattern unit tests
6. FTP/HTTP sequence tests
7. Final build verification

**After completion:**
- 14/14 tasks will be done
- 3-layer ablation experiment will be fully supported
- Test coverage will be comprehensive
- CSV logging will be useful for analysis
