/*
 * ChatAFL/test/test_tr5_stall_recovery.c - TR5 Stall Recovery Integration Tests
 *
 * Tests the full TR5 stall recovery pipeline:
 *   extract_runtime_context_from_history -> contextualize_candidate -> revalidate
 *
 * Covers:
 *   - PLAY missing Session + history has Session -> contextualize -> revalidate OK
 *   - SETUP missing Transport + history has Transport -> contextualize -> revalidate OK
 *   - PAUSE/TEARDOWN missing Session -> contextualize -> revalidate OK
 *   - Edge cases: empty history, missing CSeq, etc.
 *
 * Copyright 2026 ChatAFL Project
 * Licensed under the Apache License, Version 2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "llm-validator.h"
#include "alloc-inl.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_PASS(name) do { \
    tests_passed++; \
    printf("  PASS: %s\n", (name)); \
} while (0)

#define TEST_FAIL(name, detail) do { \
    tests_failed++; \
    printf("  FAIL: %s -- %s\n", (name), (detail)); \
} while (0)

#define ASSERT_TRUE(cond, name) do { \
    if ((cond)) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "condition was false"); } \
} while (0)

#define ASSERT_FALSE(cond, name) do { \
    if (!(cond)) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "condition was true"); } \
} while (0)

#define ASSERT_EQ(a, b, name) do { \
    if ((a) == (b)) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "values not equal"); } \
} while (0)

#define ASSERT_STR_CONTAINS(haystack, needle, name) do { \
    if (strstr((haystack), (needle))) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "string not found"); } \
} while (0)

/* ------------------------------------------------------------------ */
/*  Protocol Runtime Context (from afl-fuzz.c)                        */
/* ------------------------------------------------------------------ */

typedef struct {
  /* RTSP fields */
  u32 last_cseq;
  u8 has_cseq;
  char *last_session;
  u8 has_session;
  char *last_transport;
  u8 has_transport;
  char *last_uri;
  u8 has_uri;
  char *last_method;
  u8 has_method;

  /* FTP fields */
  u8 ftp_has_user;
  u8 ftp_has_pass;
  u8 ftp_is_authed;

  /* HTTP fields */
  u8 http_has_host;
  u8 http_has_content_length;
} protocol_runtime_context_t;

/* ------------------------------------------------------------------ */
/*  Helper functions (copied from afl-fuzz.c for testing)              */
/* ------------------------------------------------------------------ */

static void free_runtime_context(protocol_runtime_context_t *ctx) {
  if (!ctx) return;
  if (ctx->last_session) { free(ctx->last_session); ctx->last_session = NULL; }
  if (ctx->last_transport) { free(ctx->last_transport); ctx->last_transport = NULL; }
  if (ctx->last_uri) { free(ctx->last_uri); ctx->last_uri = NULL; }
  if (ctx->last_method) { free(ctx->last_method); ctx->last_method = NULL; }
  ctx->has_cseq = 0;
  ctx->has_session = 0;
  ctx->has_transport = 0;
  ctx->has_uri = 0;
  ctx->has_method = 0;
}

static const char *find_header_local(const char *message, const char *header) {
  const char *p = message;
  while ((p = strcasestr(p, header)) != NULL) {
    if (p == message || p[-1] == '\n') return p;
    p++;
  }
  return NULL;
}

static void extract_runtime_context_from_history(
    const char *protocol,
    const char *history,
    protocol_runtime_context_t *ctx)
{
  if (!protocol || !history || !ctx) return;
  memset(ctx, 0, sizeof(*ctx));

  if (strcmp(protocol, "RTSP") != 0) return;

  /* Scan history for CSeq, Session, Transport values */
  const char *line = history;
  while (*line) {
    const char *line_end = strstr(line, "\n");
    size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);

    /* Check for CSeq */
    if (strncasecmp(line, "CSeq:", 5) == 0) {
      const char *val = line + 5;
      while (*val == ' ' || *val == '\t') val++;
      ctx->last_cseq = (u32)atoi(val);
      ctx->has_cseq = 1;
    }

    /* Check for Session */
    if (strncasecmp(line, "Session:", 8) == 0) {
      const char *val = line + 8;
      while (*val == ' ' || *val == '\t') val++;
      if (ctx->last_session) free(ctx->last_session);
      ctx->last_session = strndup(val, line_len - 8);
      /* Trim trailing whitespace */
      char *end = ctx->last_session + strlen(ctx->last_session) - 1;
      while (end > ctx->last_session && (*end == '\r' || *end == '\n' || *end == ' '))
        *end-- = '\0';
      ctx->has_session = 1;
    }

    /* Check for Transport */
    if (strncasecmp(line, "Transport:", 10) == 0) {
      const char *val = line + 10;
      while (*val == ' ' || *val == '\t') val++;
      if (ctx->last_transport) free(ctx->last_transport);
      ctx->last_transport = strndup(val, line_len - 10);
      /* Trim trailing whitespace */
      char *end = ctx->last_transport + strlen(ctx->last_transport) - 1;
      while (end > ctx->last_transport && (*end == '\r' || *end == '\n' || *end == ' '))
        *end-- = '\0';
      ctx->has_transport = 1;
    }

    /* Check for request line (URI and Method) */
    char method_buf[64] = {0};
    char uri_buf[2048] = {0};
    if (sscanf(line, "%63s %2047s", method_buf, uri_buf) == 2) {
      if (strcasecmp(method_buf, "RTSP/1.0") != 0) {
        if (ctx->last_uri) free(ctx->last_uri);
        ctx->last_uri = strdup(uri_buf);
        ctx->has_uri = 1;

        if (ctx->last_method) free(ctx->last_method);
        ctx->last_method = strdup(method_buf);
        ctx->has_method = 1;
      }
    }

    line = line_end ? line_end + 1 : NULL;
  }
}

/* Contextualize a candidate message with runtime context.
 * Returns a newly allocated string (caller must free), or NULL on failure. */
static char *contextualize_candidate(
    const char *protocol,
    const char *candidate,
    const protocol_runtime_context_t *ctx)
{
  if (!protocol || !candidate || !ctx) return NULL;
  if (strcmp(protocol, "RTSP") != 0) return NULL;

  /* Parse method from candidate */
  char method_buf[64] = {0};
  if (sscanf(candidate, "%63s", method_buf) != 1) return NULL;

  /* Determine what needs to be injected */
  int needs_cseq = !find_header_local(candidate, "cseq:") && ctx->has_cseq;
  int needs_session = 0;
  int needs_transport = 0;

  if (strcasecmp(method_buf, "SETUP") == 0) {
    /* SETUP needs CSeq and Transport, but NOT Session */
    needs_transport = !find_header_local(candidate, "transport:") && ctx->has_transport;
    needs_session = 0;
  } else {
    /* PLAY/PAUSE/TEARDOWN need CSeq and Session, but NOT Transport */
    needs_session = !find_header_local(candidate, "session:") && ctx->has_session;
    needs_transport = 0;
  }

  if (!needs_cseq && !needs_session && !needs_transport) return NULL;

  /* Allocate patched message */
  size_t patch_size = strlen(candidate) + 256;
  char *patched = (char *)malloc(patch_size);
  if (!patched) return NULL;

  /* Find the end of the first line */
  const char *first_line_end = strstr(candidate, "\r\n");
  if (!first_line_end) { free(patched); return NULL; }

  /* Copy first line */
  size_t first_line_len = (size_t)(first_line_end - candidate);
  memcpy(patched, candidate, first_line_len);
  patched[first_line_len] = '\0';

  /* Inject CSeq after first line if needed */
  if (needs_cseq) {
    char cseq_buf[64];
    snprintf(cseq_buf, sizeof(cseq_buf), "\r\nCSeq: %u", ctx->last_cseq);
    strcat(patched, cseq_buf);
  }

  /* Inject Session after first line if needed */
  if (needs_session && ctx->last_session) {
    char session_buf[256];
    snprintf(session_buf, sizeof(session_buf), "\r\nSession: %s", ctx->last_session);
    strcat(patched, session_buf);
  }

  /* Inject Transport after first line if needed */
  if (needs_transport && ctx->last_transport) {
    char transport_buf[512];
    snprintf(transport_buf, sizeof(transport_buf), "\r\nTransport: %s", ctx->last_transport);
    strcat(patched, transport_buf);
  }

  /* Append remaining headers (skip first line) */
  strcat(patched, first_line_end);

  return patched;
}

/* Check if candidate is a transition-critical method */
static int is_transition_critical_candidate(
    const char *protocol,
    const char *candidate)
{
  if (!protocol || !candidate) return 0;
  if (strcmp(protocol, "RTSP") != 0) return 0;

  char method_buf[64] = {0};
  if (sscanf(candidate, "%63s", method_buf) != 1) return 0;

  return (strcasecmp(method_buf, "SETUP") == 0 ||
          strcasecmp(method_buf, "PLAY") == 0 ||
          strcasecmp(method_buf, "PAUSE") == 0 ||
          strcasecmp(method_buf, "TEARDOWN") == 0);
}

/* Check if runtime context has sufficient fields for the given candidate method */
static int has_sufficient_context_for_method(
    const char *protocol,
    const char *candidate,
    const protocol_runtime_context_t *ctx)
{
  if (!protocol || !candidate || !ctx) return 0;
  if (!ctx->has_cseq) return 0;

  if (strcmp(protocol, "RTSP") == 0) {
    char method_buf[64] = {0};
    if (sscanf(candidate, "%63s", method_buf) != 1) return 0;

    if (strcasecmp(method_buf, "SETUP") == 0) {
      return ctx->has_transport;
    } else if (strcasecmp(method_buf, "PLAY") == 0 ||
               strcasecmp(method_buf, "PAUSE") == 0 ||
               strcasecmp(method_buf, "TEARDOWN") == 0) {
      return ctx->has_session;
    }
  }

  return 1;
}

/* ------------------------------------------------------------------ */
/*  Test Cases                                                        */
/* ------------------------------------------------------------------ */

static void test_play_missing_session_recovery(void) {
    printf("[Test: PLAY missing Session recovery]\n");

    /* Simulated history: SETUP response provided Session */
    const char *history =
        "RTSP/1.0 200 OK\r\n"
        "CSeq: 1\r\n"
        "Session: abc123\r\n"
        "Transport: RTP/AVP;unicast;client_port=5000-5001\r\n"
        "\r\n";

    /* Candidate: PLAY missing Session */
    const char *candidate =
        "PLAY rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "\r\n";

    /* Extract context */
    protocol_runtime_context_t ctx;
    extract_runtime_context_from_history("RTSP", history, &ctx);

    ASSERT_TRUE(ctx.has_cseq, "History has CSeq");
    ASSERT_TRUE(ctx.has_session, "History has Session");
    ASSERT_TRUE(ctx.has_transport, "History has Transport");
    ASSERT_EQ(ctx.last_cseq, 1, "CSeq is 1");
    ASSERT_STR_CONTAINS(ctx.last_session, "abc123", "Session is abc123");

    /* Check if candidate is transition-critical */
    ASSERT_TRUE(is_transition_critical_candidate("RTSP", candidate),
                "PLAY is transition-critical");

    /* Check if context is sufficient for PLAY */
    ASSERT_TRUE(has_sufficient_context_for_method("RTSP", candidate, &ctx),
                "Context sufficient for PLAY (has CSeq + Session)");

    /* Contextualize */
    char *contextualized = contextualize_candidate("RTSP", candidate, &ctx);
    ASSERT_TRUE(contextualized != NULL, "Contextualization succeeded");

    if (contextualized) {
        ASSERT_STR_CONTAINS(contextualized, "Session: abc123",
                            "Contextualized has Session injected");
        ASSERT_STR_CONTAINS(contextualized, "CSeq: 2",
                            "Contextualized preserves original CSeq");

        /* Revalidate */
        protocol_context_t pctx;
        memset(&pctx, 0, sizeof(pctx));
        llm_validation_result_t result = validate_llm_message(
            "RTSP", LLM_STAGE_STALL, contextualized, &pctx);
        ASSERT_EQ(result, LLM_VALID_OK, "Revalidation passes after contextualize");

        free(contextualized);
    }

    free_runtime_context(&ctx);
}

static void test_setup_missing_transport_recovery(void) {
    printf("[Test: SETUP missing Transport recovery]\n");

    /* Simulated history: prior SETUP provided Transport */
    const char *history =
        "SETUP rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "Transport: RTP/AVP;unicast;client_port=5000-5001\r\n"
        "\r\n"
        "RTSP/1.0 200 OK\r\n"
        "CSeq: 1\r\n"
        "Session: def456\r\n"
        "Transport: RTP/AVP;unicast;client_port=5000-5001\r\n"
        "\r\n";

    /* Candidate: SETUP missing Transport */
    const char *candidate =
        "SETUP rtsp://example.com/media/stream2 RTSP/1.0\r\n"
        "CSeq: 3\r\n"
        "\r\n";

    /* Extract context */
    protocol_runtime_context_t ctx;
    extract_runtime_context_from_history("RTSP", history, &ctx);

    ASSERT_TRUE(ctx.has_cseq, "History has CSeq");
    ASSERT_TRUE(ctx.has_session, "History has Session");
    ASSERT_TRUE(ctx.has_transport, "History has Transport");

    /* Check if candidate is transition-critical */
    ASSERT_TRUE(is_transition_critical_candidate("RTSP", candidate),
                "SETUP is transition-critical");

    /* Check if context is sufficient for SETUP */
    ASSERT_TRUE(has_sufficient_context_for_method("RTSP", candidate, &ctx),
                "Context sufficient for SETUP (has CSeq + Transport)");

    /* Contextualize */
    char *contextualized = contextualize_candidate("RTSP", candidate, &ctx);
    ASSERT_TRUE(contextualized != NULL, "Contextualization succeeded");

    if (contextualized) {
        ASSERT_STR_CONTAINS(contextualized, "Transport:",
                            "Contextualized has Transport injected");
        ASSERT_FALSE(strstr(contextualized, "Session: def456"),
                     "Contextualized does NOT inject Session for SETUP");

        /* Revalidate */
        protocol_context_t pctx;
        memset(&pctx, 0, sizeof(pctx));
        llm_validation_result_t result = validate_llm_message(
            "RTSP", LLM_STAGE_STALL, contextualized, &pctx);
        ASSERT_EQ(result, LLM_VALID_OK, "Revalidation passes after contextualize");

        free(contextualized);
    }

    free_runtime_context(&ctx);
}

static void test_pause_missing_session_recovery(void) {
    printf("[Test: PAUSE missing Session recovery]\n");

    const char *history =
        "RTSP/1.0 200 OK\r\n"
        "CSeq: 2\r\n"
        "Session: xyz789\r\n"
        "\r\n";

    const char *candidate =
        "PAUSE rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 3\r\n"
        "\r\n";

    protocol_runtime_context_t ctx;
    extract_runtime_context_from_history("RTSP", history, &ctx);

    ASSERT_TRUE(ctx.has_session, "History has Session");
    ASSERT_TRUE(has_sufficient_context_for_method("RTSP", candidate, &ctx),
                "Context sufficient for PAUSE");

    char *contextualized = contextualize_candidate("RTSP", candidate, &ctx);
    ASSERT_TRUE(contextualized != NULL, "Contextualization succeeded");

    if (contextualized) {
        ASSERT_STR_CONTAINS(contextualized, "Session: xyz789",
                            "Contextualized has Session injected");

        protocol_context_t pctx;
        memset(&pctx, 0, sizeof(pctx));
        llm_validation_result_t result = validate_llm_message(
            "RTSP", LLM_STAGE_STALL, contextualized, &pctx);
        ASSERT_EQ(result, LLM_VALID_OK, "Revalidation passes");

        free(contextualized);
    }

    free_runtime_context(&ctx);
}

static void test_teardown_missing_session_recovery(void) {
    printf("[Test: TEARDOWN missing Session recovery]\n");

    const char *history =
        "RTSP/1.0 200 OK\r\n"
        "CSeq: 3\r\n"
        "Session: sess999\r\n"
        "\r\n";

    const char *candidate =
        "TEARDOWN rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 4\r\n"
        "\r\n";

    protocol_runtime_context_t ctx;
    extract_runtime_context_from_history("RTSP", history, &ctx);

    ASSERT_TRUE(ctx.has_session, "History has Session");
    ASSERT_TRUE(has_sufficient_context_for_method("RTSP", candidate, &ctx),
                "Context sufficient for TEARDOWN");

    char *contextualized = contextualize_candidate("RTSP", candidate, &ctx);
    ASSERT_TRUE(contextualized != NULL, "Contextualization succeeded");

    if (contextualized) {
        ASSERT_STR_CONTAINS(contextualized, "Session: sess999",
                            "Contextualized has Session injected");

        protocol_context_t pctx;
        memset(&pctx, 0, sizeof(pctx));
        llm_validation_result_t result = validate_llm_message(
            "RTSP", LLM_STAGE_STALL, contextualized, &pctx);
        ASSERT_EQ(result, LLM_VALID_OK, "Revalidation passes");

        free(contextualized);
    }

    free_runtime_context(&ctx);
}

static void test_setup_insufficient_context(void) {
    printf("[Test: SETUP with insufficient context (no Transport)]\n");

    /* History has CSeq and Session but NO Transport */
    const char *history =
        "RTSP/1.0 200 OK\r\n"
        "CSeq: 1\r\n"
        "Session: abc123\r\n"
        "\r\n";

    const char *candidate =
        "SETUP rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "\r\n";

    protocol_runtime_context_t ctx;
    extract_runtime_context_from_history("RTSP", history, &ctx);

    ASSERT_TRUE(ctx.has_cseq, "History has CSeq");
    ASSERT_TRUE(ctx.has_session, "History has Session");
    ASSERT_FALSE(ctx.has_transport, "History does NOT have Transport");

    /* Context should be insufficient for SETUP (needs Transport) */
    ASSERT_FALSE(has_sufficient_context_for_method("RTSP", candidate, &ctx),
                 "Context insufficient for SETUP (no Transport)");

    free_runtime_context(&ctx);
}

static void test_play_insufficient_context(void) {
    printf("[Test: PLAY with insufficient context (no Session)]\n");

    /* History has CSeq and Transport but NO Session */
    const char *history =
        "SETUP rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "Transport: RTP/AVP;unicast;client_port=5000-5001\r\n"
        "\r\n";

    const char *candidate =
        "PLAY rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "\r\n";

    protocol_runtime_context_t ctx;
    extract_runtime_context_from_history("RTSP", history, &ctx);

    ASSERT_TRUE(ctx.has_cseq, "History has CSeq");
    ASSERT_FALSE(ctx.has_session, "History does NOT have Session");
    ASSERT_TRUE(ctx.has_transport, "History has Transport");

    /* Context should be insufficient for PLAY (needs Session) */
    ASSERT_FALSE(has_sufficient_context_for_method("RTSP", candidate, &ctx),
                 "Context insufficient for PLAY (no Session)");

    free_runtime_context(&ctx);
}

static void test_empty_history(void) {
    printf("[Test: Empty history]\n");

    const char *history = "";
    const char *candidate =
        "PLAY rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n";

    protocol_runtime_context_t ctx;
    extract_runtime_context_from_history("RTSP", history, &ctx);

    ASSERT_FALSE(ctx.has_cseq, "Empty history has no CSeq");
    ASSERT_FALSE(ctx.has_session, "Empty history has no Session");
    ASSERT_FALSE(ctx.has_transport, "Empty history has no Transport");
    ASSERT_FALSE(has_sufficient_context_for_method("RTSP", candidate, &ctx),
                 "Context insufficient with empty history");

    free_runtime_context(&ctx);
}

static void test_non_transition_critical_method(void) {
    printf("[Test: Non-transition-critical method (OPTIONS)]\n");

    const char *candidate =
        "OPTIONS rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n";

    ASSERT_FALSE(is_transition_critical_candidate("RTSP", candidate),
                 "OPTIONS is NOT transition-critical");
}

static void test_validator_context_fail_classification(void) {
    printf("[Test: Validator CONTEXT_FAIL classification]\n");

    protocol_context_t ctx;

    /* SETUP missing Transport -> CONTEXT_FAIL */
    ctx = (protocol_context_t){0};
    llm_validation_result_t r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "SETUP rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_EQ(r, LLM_VALID_CONTEXT_FAIL, "SETUP missing Transport is CONTEXT_FAIL");

    /* PLAY missing Session -> CONTEXT_FAIL */
    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "PLAY rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_EQ(r, LLM_VALID_CONTEXT_FAIL, "PLAY missing Session is CONTEXT_FAIL");

    /* Missing CSeq -> GRAMMAR_FAIL (not recoverable) */
    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS rtsp://example.com/media RTSP/1.0\r\n"
        "\r\n",
        &ctx);
    ASSERT_EQ(r, LLM_VALID_GRAMMAR_FAIL, "Missing CSeq is GRAMMAR_FAIL (not recoverable)");
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== TR5 Stall Recovery Integration Tests ===\n\n");

    test_play_missing_session_recovery();
    printf("\n");

    test_setup_missing_transport_recovery();
    printf("\n");

    test_pause_missing_session_recovery();
    printf("\n");

    test_teardown_missing_session_recovery();
    printf("\n");

    test_setup_insufficient_context();
    printf("\n");

    test_play_insufficient_context();
    printf("\n");

    test_empty_history();
    printf("\n");

    test_non_transition_critical_method();
    printf("\n");

    test_validator_context_fail_classification();
    printf("\n");

    printf("=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
