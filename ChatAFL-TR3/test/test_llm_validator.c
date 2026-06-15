/*
 * ChatAFL/test/test_llm_validator.c - Unit tests for LLM Validator
 *
 * Standalone test harness for RTSP, FTP, HTTP validators and
 * sequence validation in the Validation-Driven LLM Fuzzing framework.
 *
 * Copyright 2026 ChatAFL Project
 * Licensed under the Apache License, Version 2.0
 */

#include "../llm-validator.h"
#include "../alloc-inl.h"
#include <stdio.h>
#include <string.h>

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

#define ASSERT_OK(result, name) do { \
    if ((result) == LLM_VALID_OK) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "expected LLM_VALID_OK"); } \
} while (0)

#define ASSERT_FORMAT_FAIL(result, name) do { \
    if ((result) == LLM_VALID_FORMAT_FAIL) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "expected LLM_VALID_FORMAT_FAIL"); } \
} while (0)

#define ASSERT_GRAMMAR_FAIL(result, name) do { \
    if ((result) == LLM_VALID_GRAMMAR_FAIL) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "expected LLM_VALID_GRAMMAR_FAIL"); } \
} while (0)

#define ASSERT_CONTEXT_FAIL(result, name) do { \
    if ((result) == LLM_VALID_CONTEXT_FAIL) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "expected LLM_VALID_CONTEXT_FAIL"); } \
} while (0)

#define ASSERT_FAIL(result, name) do { \
    if ((result) != LLM_VALID_OK) { TEST_PASS(name); } \
    else { TEST_FAIL(name, "expected failure, got LLM_VALID_OK"); } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if ((cond)) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: ASSERT_TRUE(%s) at %s:%d\n", #cond, __FILE__, __LINE__); } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if (!(cond)) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: ASSERT_FALSE(%s) at %s:%d\n", #cond, __FILE__, __LINE__); } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) == (b)) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: ASSERT_EQ(%s, %s) at %s:%d\n", #a, #b, __FILE__, __LINE__); } \
} while (0)

#define ASSERT_NEQ(a, b) do { \
    if ((a) != (b)) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: ASSERT_NEQ(%s, %s) at %s:%d\n", #a, #b, __FILE__, __LINE__); } \
} while (0)

/* ------------------------------------------------------------------ */
/*  RTSP Tests                                                        */
/* ------------------------------------------------------------------ */

static void test_rtsp(void) {

    printf("[RTSP Tests]\n");

    protocol_context_t ctx;
    llm_validation_result_t r;

    /* --- Valid requests --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Valid OPTIONS request");

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "DESCRIBE rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "Accept: application/sdp\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Valid DESCRIBE request");

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "SETUP rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 3\r\n"
        "Transport: RTP/AVP;unicast;client_port=8000-8001\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Valid SETUP request with Transport");

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "PLAY rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 4\r\n"
        "Session: 12345678\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Valid PLAY request with Session");

    /* --- Missing CSeq header --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS rtsp://example.com/media RTSP/1.0\r\n"
        "\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Missing CSeq header");

    /* --- Missing Transport for SETUP --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "SETUP rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 3\r\n"
        "\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Missing Transport for SETUP");

    /* --- Missing Session for PLAY --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "PLAY rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 4\r\n"
        "\r\n",
        &ctx);
    ASSERT_CONTEXT_FAIL(r, "Missing Session for PLAY");

    /* --- Missing Session for PAUSE --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "PAUSE rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 5\r\n"
        "\r\n",
        &ctx);
    ASSERT_CONTEXT_FAIL(r, "Missing Session for PAUSE");

    /* --- Missing Session for TEARDOWN --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "TEARDOWN rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 6\r\n"
        "\r\n",
        &ctx);
    ASSERT_CONTEXT_FAIL(r, "Missing Session for TEARDOWN");

    /* --- Invalid method --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "INVALID rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Invalid RTSP method");

    /* --- Invalid version (not RTSP/1.0) --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS rtsp://example.com/media RTSP/2.0\r\n"
        "CSeq: 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Invalid version (RTSP/2.0)");

    /* --- Invalid URI (not rtsp:// or *) --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS http://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Invalid URI (http:// instead of rtsp://)");

    /* --- Non-printable characters --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\x01\r\n"
        "\r\n",
        &ctx);
    ASSERT_FORMAT_FAIL(r, "Non-printable character in header");

    /* --- Case-insensitive headers (lowercase cseq:) --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS rtsp://example.com/media RTSP/1.0\r\n"
        "cseq: 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Case-insensitive header (cseq: lowercase)");

    /* --- Header line without ':' --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "OPTIONS rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq 1\r\n"
        "\r\n",
        &ctx);
    ASSERT_FORMAT_FAIL(r, "Header line without ':'");
}

/* ------------------------------------------------------------------ */
/*  FTP Tests                                                         */
/* ------------------------------------------------------------------ */

static void test_ftp(void) {

    printf("[FTP Tests]\n");

    protocol_context_t ctx;
    llm_validation_result_t r;

    /* --- Valid FTP commands --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR,
        "USER anonymous\r\n",
        &ctx);
    ASSERT_OK(r, "Valid USER command");

    ctx = (protocol_context_t){0};
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR,
        "PASS guest@\r\n",
        &ctx);
    /* PASS without prior USER should fail (session dependency) */
    ASSERT_FAIL(r, "Valid PASS command (standalone, no prior USER)");

    /* Valid sequence: USER then PASS */
    ctx = (protocol_context_t){0};
    validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "USER alice\r\n", &ctx);
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "PASS secret\r\n", &ctx);
    ASSERT_OK(r, "Valid PASS after USER");

    /* Valid sequence: USER, PASS, then RETR */
    ctx = (protocol_context_t){0};
    validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "USER alice\r\n", &ctx);
    validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "PASS secret\r\n", &ctx);
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "RETR /file.txt\r\n", &ctx);
    ASSERT_OK(r, "Valid RETR after USER+PASS");

    /* --- PASS before USER (should fail) --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR,
        "PASS secret\r\n",
        &ctx);
    ASSERT_FAIL(r, "PASS before USER");

    /* --- RETR without authentication (should fail) --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR,
        "RETR /file.txt\r\n",
        &ctx);
    ASSERT_FAIL(r, "RETR without authentication (no USER/PASS)");

    ctx = (protocol_context_t){0};
    validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "USER alice\r\n", &ctx);
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR,
        "RETR /file.txt\r\n",
        &ctx);
    ASSERT_FAIL(r, "RETR with USER but no PASS");

    /* --- Invalid command --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR,
        "INVALID arg\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Invalid FTP command");

    /* --- Missing \r\n terminator --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("FTP", LLM_STAGE_GRAMMAR,
        "USER anonymous",
        &ctx);
    ASSERT_FORMAT_FAIL(r, "Missing \\r\\n terminator");
}

/* ------------------------------------------------------------------ */
/*  HTTP Tests                                                        */
/* ------------------------------------------------------------------ */

static void test_http(void) {

    printf("[HTTP Tests]\n");

    protocol_context_t ctx;
    llm_validation_result_t r;

    /* --- Valid HTTP GET request --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "GET /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Valid GET request (HTTP/1.1)");

    /* --- Valid HTTP POST request --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "POST /submit HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello",
        &ctx);
    ASSERT_OK(r, "Valid POST request with body");

    /* --- Valid HTTP/1.0 request --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "GET /page HTTP/1.0\r\n"
        "Host: example.com\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Valid GET request (HTTP/1.0)");

    /* --- Invalid method --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "INVALID /page HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Invalid HTTP method");

    /* --- Invalid version --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "GET /page HTTP/2.0\r\n"
        "Host: example.com\r\n"
        "\r\n",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Invalid HTTP version (HTTP/2.0)");

    /* --- Missing header/body separator --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "GET /page HTTP/1.1\r\n"
        "Host: example.com",
        &ctx);
    ASSERT_FORMAT_FAIL(r, "Missing header/body separator (\\r\\n\\r\\n)");

    /* --- Content-Length mismatch --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "POST /submit HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 100\r\n"
        "\r\n"
        "short",
        &ctx);
    ASSERT_GRAMMAR_FAIL(r, "Content-Length mismatch (100 vs 5 bytes)");
}

/* ------------------------------------------------------------------ */
/*  Sequence Tests                                                    */
/* ------------------------------------------------------------------ */

static void test_sequences(void) {

    printf("[Sequence Tests]\n");

    protocol_context_t ctx;
    llm_validation_result_t r;

    /* --- Valid RTSP sequence: DESCRIBE + SETUP + PLAY --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_sequence("RTSP", LLM_STAGE_GRAMMAR,
        "DESCRIBE rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n"
        "SETUP rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "Transport: RTP/AVP;unicast;client_port=8000-8001\r\n"
        "\r\n"
        "PLAY rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 3\r\n"
        "Session: 98765432\r\n"
        "\r\n",
        &ctx);
    ASSERT_OK(r, "Valid RTSP sequence (DESCRIBE + SETUP + PLAY)");

    /* --- Invalid sequence: PLAY without SETUP (no Session header) --- */

    ctx = (protocol_context_t){0};
    r = validate_llm_sequence("RTSP", LLM_STAGE_GRAMMAR,
        "DESCRIBE rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n"
        "PLAY rtsp://example.com/media/stream1 RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "\r\n",
        &ctx);
    ASSERT_CONTEXT_FAIL(r, "Invalid RTSP sequence (PLAY without Session/SETUP)");
}

/* ------------------------------------------------------------------ */
/*  Context Update Tests                                              */
/* ------------------------------------------------------------------ */

static void test_context_updates(void) {

    printf("[Context Update Tests]\n");

    protocol_context_t ctx;

    /* RTSP context: CSeq extraction, has_session, has_transport */
    ctx = (protocol_context_t){0};
    validate_llm_message("RTSP", LLM_STAGE_GRAMMAR,
        "SETUP rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 42\r\n"
        "Transport: RTP/AVP\r\n"
        "Session: abc123\r\n"
        "\r\n",
        &ctx);
    if (ctx.type == PROTOCOL_RTSP && ctx.ctx.rtsp.last_cseq == 42 &&
        ctx.ctx.rtsp.has_session && ctx.ctx.rtsp.has_transport) {
        TEST_PASS("RTSP context updated (cseq=42, session, transport)");
    } else {
        TEST_FAIL("RTSP context updated", "context fields incorrect");
    }

    /* FTP context: USER sets has_user, PASS sets has_pass + is_authed */
    ctx = (protocol_context_t){0};
    validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "USER bob\r\n", &ctx);
    if (ctx.type == PROTOCOL_FTP && ctx.ctx.ftp.has_user &&
        !ctx.ctx.ftp.has_pass && !ctx.ctx.ftp.is_authed) {
        TEST_PASS("FTP context after USER (has_user, not authed)");
    } else {
        TEST_FAIL("FTP context after USER", "context fields incorrect");
    }

    validate_llm_message("FTP", LLM_STAGE_GRAMMAR, "PASS pw\r\n", &ctx);
    if (ctx.ctx.ftp.has_pass && ctx.ctx.ftp.is_authed) {
        TEST_PASS("FTP context after PASS (authed)");
    } else {
        TEST_FAIL("FTP context after PASS", "context fields incorrect");
    }

    /* HTTP context: has_host, has_content_length */
    ctx = (protocol_context_t){0};
    validate_llm_message("HTTP", LLM_STAGE_GRAMMAR,
        "POST /data HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "body",
        &ctx);
    if (ctx.type == PROTOCOL_HTTP && ctx.ctx.http.has_host &&
        ctx.ctx.http.has_content_length) {
        TEST_PASS("HTTP context updated (host, content-length)");
    } else {
        TEST_FAIL("HTTP context updated", "context fields incorrect");
    }
}

/* ------------------------------------------------------------------ */
/*  Normalizer Tests                                                  */
/* ------------------------------------------------------------------ */

static void test_normalizer(void) {

    printf("[Normalizer Tests]\n");

    char *normalized = NULL;
    int ret;

    /* Clean input passes through */
    ret = llm_normalize_candidate("OPTIONS rtsp://x RTSP/1.0\r\n\r\n", &normalized);
    if (ret == 0 && normalized && strcmp(normalized, "OPTIONS rtsp://x RTSP/1.0\r\n\r\n") == 0) {
        TEST_PASS("Normalizer: clean input passes through");
    } else {
        TEST_FAIL("Normalizer: clean input passes through", "unexpected output");
    }
    if (normalized) { ck_free(normalized); normalized = NULL; }

    /* Strips non-printable bytes, keeps \r\n
     * Note: \x01C would be parsed as \x1C by the C compiler (hex greedy),
     * so we use octal \001 to embed 0x01 followed by literal 'C'. */
    ret = llm_normalize_candidate("AB\001C\002\r\n", &normalized);
    if (ret == 0 && normalized && strcmp(normalized, "ABC\r\n") == 0) {
        TEST_PASS("Normalizer: strips non-printable, keeps \\r\\n");
    } else {
        TEST_FAIL("Normalizer: strips non-printable, keeps \\r\\n",
                   normalized ? normalized : "(null)");
    }
    if (normalized) { ck_free(normalized); normalized = NULL; }

    /* NULL inputs return -1 */
    ret = llm_normalize_candidate(NULL, &normalized);
    if (ret == -1) {
        TEST_PASS("Normalizer: NULL raw returns -1");
    } else {
        TEST_FAIL("Normalizer: NULL raw returns -1", "unexpected return");
    }

    ret = llm_normalize_candidate("hello", NULL);
    if (ret == -1) {
        TEST_PASS("Normalizer: NULL output ptr returns -1");
    } else {
        TEST_FAIL("Normalizer: NULL output ptr returns -1", "unexpected return");
    }
}

/* ------------------------------------------------------------------ */
/*  Grammar Pattern Tests                                             */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  FTP Sequence Tests                                                */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/*  HTTP Sequence Tests                                               */
/* ------------------------------------------------------------------ */

static void test_http_sequence(void) {
    printf("[HTTP Sequence Tests]\n");
    protocol_context_t ctx = {0};

    // Valid HTTP sequence: GET + POST (no body to avoid trailing region)
    const char *http_seq = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n"
                           "POST /submit HTTP/1.1\r\nHost: example.com\r\n\r\n";
    llm_validation_result_t result = validate_llm_sequence("HTTP", LLM_STAGE_ENRICHMENT, http_seq, &ctx);
    ASSERT_EQ(result, LLM_VALID_OK);

    // Invalid: missing separator
    ctx = (protocol_context_t){0};
    const char *http_invalid = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n";
    result = validate_llm_sequence("HTTP", LLM_STAGE_ENRICHMENT, http_invalid, &ctx);
    ASSERT_NEQ(result, LLM_VALID_OK);

    printf("  PASS: All HTTP sequence tests\n");
}

/* ------------------------------------------------------------------ */
/*  Validation Mode Tests                                             */
/* ------------------------------------------------------------------ */

static void test_validation_modes(void) {
    printf("[Validation Mode Tests]\n");

    protocol_context_t ctx;
    llm_validation_result_t result;

    ctx = (protocol_context_t){0};
    result = validate_llm_message_with_mode(
        "RTSP",
        LLM_STAGE_STALL,
        "PLAY rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 9\r\n"
        "\r\n",
        &ctx,
        LLM_VALIDATE_FORMAT_ONLY
    );
    ASSERT_OK(result, "Format-only mode accepts RTSP message missing session context");

    ctx = (protocol_context_t){0};
    result = validate_llm_message_with_mode(
        "RTSP",
        LLM_STAGE_STALL,
        "PLAY rtsp://example.com/media RTSP/1.0\r\n"
        "CSeq: 9\r\n"
        "\r\n",
        &ctx,
        LLM_VALIDATE_FULL
    );
    ASSERT_CONTEXT_FAIL(result, "Full mode reports missing RTSP session as context failure");

    ctx = (protocol_context_t){0};
    result = validate_llm_message_with_mode(
        "HTTP",
        LLM_STAGE_STALL,
        "INVALID /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n",
        &ctx,
        LLM_VALIDATE_FORMAT_ONLY
    );
    ASSERT_OK(result, "Format-only mode ignores HTTP grammar errors");

    ctx = (protocol_context_t){0};
    result = validate_llm_message_with_mode(
        "HTTP",
        LLM_STAGE_STALL,
        "INVALID /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n",
        &ctx,
        LLM_VALIDATE_FULL
    );
    ASSERT_GRAMMAR_FAIL(result, "Full mode rejects HTTP grammar errors");

    ctx = (protocol_context_t){0};
    result = validate_llm_sequence_with_mode(
        "FTP",
        LLM_STAGE_ENRICHMENT,
        "USER demo\r\nPASS demo\r\nLIST\r\n",
        &ctx,
        LLM_VALIDATE_FORMAT_ONLY
    );
    ASSERT_OK(result, "Format-only mode accepts valid FTP sequence");

    ctx = (protocol_context_t){0};
    result = validate_llm_sequence_with_mode(
        "FTP",
        LLM_STAGE_ENRICHMENT,
        "PASS demo\r\nLIST\r\n",
        &ctx,
        LLM_VALIDATE_FULL
    );
    ASSERT_CONTEXT_FAIL(result, "Full mode rejects FTP sequence with missing login context");
}

/* ------------------------------------------------------------------ */
/*  Post-Execution Classification Tests                               */
/* ------------------------------------------------------------------ */

static void test_post_execution_classification(void) {
    printf("[Post-Execution Classification Tests]\n");

    ASSERT_EQ(classify_llm_execution_gain(0, 0, 0), LLM_VALID_NO_GAIN);
    ASSERT_EQ(classify_llm_execution_gain(1, 0, 0), LLM_VALID_OK);
    ASSERT_EQ(classify_llm_execution_gain(0, 1, 0), LLM_VALID_OK);
    ASSERT_EQ(classify_llm_execution_gain(0, 0, 1), LLM_VALID_OK);

    printf("  PASS: Post-execution gain classification behaves as expected\n");
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {

    printf("=== LLM Validator Unit Tests ===\n\n");

    test_rtsp();
    printf("\n");

    test_ftp();
    printf("\n");

    test_http();
    printf("\n");

    test_sequences();
    printf("\n");

    test_context_updates();
    printf("\n");

    test_normalizer();
    printf("\n");

    test_grammar_pattern();
    printf("\n");

    test_ftp_sequence();
    printf("\n");

    test_http_sequence();
    printf("\n");

    test_validation_modes();
    printf("\n");

    test_post_execution_classification();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
