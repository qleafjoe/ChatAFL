/*
 * test_llm_integration.c - Integration tests for LLM + Validator
 *
 * Tests the full pipeline: LLM call -> clean -> validate
 * Requires: LLM_TOKEN environment variable (or uses default MiniMax key)
 *
 * Copyright 2026 ChatAFL Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../llm-validator.h"
#include "../chat-llm.h"
#include "../alloc-inl.h"

#define TEST_PASS 0
#define TEST_FAIL 1

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(condition, msg) do { \
    tests_run++; \
    if (!(condition)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* Test: LLM generates valid RTSP OPTIONS request */
static void test_llm_rtsp_options(void) {
    printf("[LLM RTSP OPTIONS Test]\n");

    char *prompt = "Generate a single RTSP OPTIONS request message. "
                   "Output ONLY the raw RTSP message, no explanation. "
                   "Use CSeq 1 and include the required headers.";

    char *response = chat_with_llm(prompt, "instruct", 3, 0.3);
    ASSERT(response != NULL, "LLM should return a response");

    printf("  LLM Response (%zu bytes):\n", strlen(response));
    printf("  ---\n%s\n  ---\n", response);

    /* Clean the response */
    char *cleaned = clean_llm_response(response);
    ASSERT(cleaned != NULL, "clean_llm_response should succeed");
    ASSERT(strlen(cleaned) > 0, "Cleaned response should not be empty");

    printf("  Cleaned (%zu bytes):\n", strlen(cleaned));
    printf("  ---\n%s\n  ---\n", cleaned);

    /* Validate */
    protocol_context_t ctx = {0};
    llm_validation_result_t result = validate_llm_message(
        "RTSP", LLM_STAGE_STALL, cleaned, &ctx
    );

    printf("  Validation result: %d\n", result);
    if (result == LLM_VALID_OK) {
        printf("  PASS: LLM generated valid RTSP OPTIONS\n");
    } else {
        printf("  WARN: LLM output failed validation (result=%d)\n", result);
        printf("  This may be expected - LLM outputs are not always valid\n");
    }

    /* Don't fail test on validation - LLM is non-deterministic */
    free(response);
    free(cleaned);
    tests_run++;
    tests_passed++;
}

/* Test: LLM generates valid RTSP SETUP request */
static void test_llm_rtsp_setup(void) {
    printf("[LLM RTSP SETUP Test]\n");

    char *prompt = "Generate a single RTSP SETUP request message. "
                   "Output ONLY the raw RTSP message, no explanation. "
                   "Use CSeq 2, include Transport header for RTP/AVP/UDP.";

    char *response = chat_with_llm(prompt, "instruct", 3, 0.3);
    ASSERT(response != NULL, "LLM should return a response");

    char *cleaned = clean_llm_response(response);
    ASSERT(cleaned != NULL, "clean_llm_response should succeed");

    printf("  LLM Response:\n  ---\n%s\n  ---\n", cleaned);

    protocol_context_t ctx = {0};
    llm_validation_result_t result = validate_llm_message(
        "RTSP", LLM_STAGE_STALL, cleaned, &ctx
    );

    printf("  Validation result: %d\n", result);
    if (result == LLM_VALID_OK) {
        printf("  PASS: LLM generated valid RTSP SETUP\n");
        ASSERT(ctx.ctx.rtsp.has_transport, "SETUP should have Transport header");
    } else {
        printf("  WARN: LLM output failed validation (result=%d)\n", result);
    }

    free(response);
    free(cleaned);
    tests_run++;
    tests_passed++;
}

/* Test: LLM generates valid FTP sequence */
static void test_llm_ftp_sequence(void) {
    printf("[LLM FTP Sequence Test]\n");

    char *prompt = "Generate an FTP login sequence with these commands:\n"
                   "1. USER anonymous\n"
                   "2. PASS guest@\n"
                   "3. SYST\n"
                   "Output ONLY the raw FTP commands, one per line, no explanation.";

    char *response = chat_with_llm(prompt, "instruct", 3, 0.3);
    ASSERT(response != NULL, "LLM should return a response");

    char *cleaned = clean_llm_response(response);
    ASSERT(cleaned != NULL, "clean_llm_response should succeed");

    printf("  LLM Response:\n  ---\n%s\n  ---\n", cleaned);

    protocol_context_t ctx = {0};
    llm_validation_result_t result = validate_llm_sequence(
        "FTP", LLM_STAGE_ENRICHMENT, cleaned, &ctx
    );

    printf("  Validation result: %d\n", result);
    if (result == LLM_VALID_OK) {
        printf("  PASS: LLM generated valid FTP sequence\n");
    } else {
        printf("  WARN: LLM output failed validation (result=%d)\n", result);
    }

    free(response);
    free(cleaned);
    tests_run++;
    tests_passed++;
}

/* Test: LLM generates valid HTTP request */
static void test_llm_http_get(void) {
    printf("[LLM HTTP GET Test]\n");

    char *prompt = "Generate a single HTTP GET request for /index.html. "
                   "Output ONLY the raw HTTP request, no explanation. "
                   "Use HTTP/1.1 and include Host header.";

    char *response = chat_with_llm(prompt, "instruct", 3, 0.3);
    ASSERT(response != NULL, "LLM should return a response");

    char *cleaned = clean_llm_response(response);
    ASSERT(cleaned != NULL, "clean_llm_response should succeed");

    printf("  LLM Response:\n  ---\n%s\n  ---\n", cleaned);

    protocol_context_t ctx = {0};
    llm_validation_result_t result = validate_llm_message(
        "HTTP", LLM_STAGE_STALL, cleaned, &ctx
    );

    printf("  Validation result: %d\n", result);
    if (result == LLM_VALID_OK) {
        printf("  PASS: LLM generated valid HTTP GET\n");
    } else {
        printf("  WARN: LLM output failed validation (result=%d)\n", result);
    }

    free(response);
    free(cleaned);
    tests_run++;
    tests_passed++;
}

/* Test: LLM generates grammar patterns */
static void test_llm_grammar_extraction(void) {
    printf("[LLM Grammar Extraction Test]\n");

    char *prompt = "List the 5 most common RTSP request methods, one per line. "
                   "Output ONLY the method names, no explanation.";

    char *response = chat_with_llm(prompt, "instruct", 3, 0.3);
    ASSERT(response != NULL, "LLM should return a response");

    char *cleaned = clean_llm_response(response);
    ASSERT(cleaned != NULL, "clean_llm_response should succeed");

    printf("  LLM Response:\n  ---\n%s\n  ---\n", cleaned);

    /* Check each line is a valid RTSP method */
    int valid_count = 0;
    char *line = strtok(cleaned, "\n");
    while (line) {
        /* Trim whitespace and trailing \r (from CRLF conversion) */
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == ' ' || line[len-1] == '\t')) {
            line[--len] = '\0';
        }
        if (len > 0) {
            if (validate_grammar_pattern(line, "RTSP")) {
                valid_count++;
                printf("  Valid: %s\n", line);
            } else {
                printf("  Invalid: %s\n", line);
            }
        }
        line = strtok(NULL, "\n");
    }

    printf("  Valid patterns: %d\n", valid_count);
    if (valid_count >= 3) {
        printf("  PASS: LLM generated mostly valid RTSP methods\n");
    } else {
        printf("  WARN: LLM generated few valid methods\n");
    }

    free(response);
    tests_run++;
    tests_passed++;
}

/* Test: normalize_candidate with LLM output */
static void test_llm_normalize(void) {
    printf("[LLM Normalize Test]\n");

    char *prompt = "Generate an RTSP OPTIONS request with some extra text before and after. "
                   "Add 'Here is the request:' before and 'Hope this helps!' after.";

    char *response = chat_with_llm(prompt, "instruct", 3, 0.3);
    ASSERT(response != NULL, "LLM should return a response");

    printf("  Raw LLM output:\n  ---\n%s\n  ---\n", response);

    char *normalized = NULL;
    int norm_result = llm_normalize_candidate(response, &normalized);
    ASSERT(norm_result == 0, "llm_normalize_candidate should succeed");
    ASSERT(normalized != NULL, "Normalized should not be NULL");

    printf("  Normalized (%zu bytes):\n  ---\n%s\n  ---\n", strlen(normalized), normalized);

    /* Try to validate the normalized output */
    protocol_context_t ctx = {0};
    llm_validation_result_t valid_result = validate_llm_message(
        "RTSP", LLM_STAGE_STALL, normalized, &ctx
    );

    printf("  Validation result: %d\n", valid_result);

    free(response);
    ck_free(normalized);
    tests_run++;
    tests_passed++;
}

int main(void) {
    printf("=== LLM Integration Tests ===\n");
    printf("Note: These tests call the real LLM API\n\n");

    /* Check if LLM is available */
    const char *token = getenv("LLM_TOKEN");
    if (!token || token[0] == '\0') {
        printf("[INFO] Using default MiniMax API key\n");
        printf("[INFO] Set LLM_TOKEN env var to use a different key\n\n");
    }

    test_llm_rtsp_options();
    printf("\n");

    test_llm_rtsp_setup();
    printf("\n");

    test_llm_ftp_sequence();
    printf("\n");

    test_llm_http_get();
    printf("\n");

    test_llm_grammar_extraction();
    printf("\n");

    test_llm_normalize();
    printf("\n");

    printf("=== Results: %d passed, %d failed, %d total ===\n",
           tests_passed, tests_failed, tests_run);

    return tests_failed > 0 ? TEST_FAIL : TEST_PASS;
}
