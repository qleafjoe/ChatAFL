#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "chat-llm.h"

int main() {
    printf("Testing clean_llm_response...\n");

    // Test 1: Markdown JSON
    const char *case1 = "Here is the grammar: ```json\n{\"key\": \"value\"}\n``` Hope this helps!";
    char *res1 = clean_llm_response(case1);
    printf("Case 1 (Markdown JSON):\nInput: %s\nOutput: %s\n\n", case1, res1);
    
    // Test 2: Refusal
    const char *case2 = "I'm sorry, I cannot fulfill this request as it violates safety policies.";
    char *res2 = clean_llm_response(case2);
    printf("Case 2 (Refusal):\nInput: %s\nOutput: %s\n\n", case2, res2 ? res2 : "NULL (Expected)");

    // Test 3: Raw Protocol with markdown
    const char *case3 = "```rtsp\nPLAY rtsp://127.0.0.1/ RTSP/1.0\nCSeq: 4\n\n```";
    char *res3 = clean_llm_response(case3);
    printf("Case 3 (Markdown RTSP):\nInput: %s\nOutput: %s\n\n", case3, res3);

    // Test 4: Nested JSON
    const char *case4 = "Sure! [{\"msg\": \"hello\"}]";
    char *res4 = clean_llm_response(case4);
    printf("Case 4 (Nested Array):\nInput: %s\nOutput: %s\n\n", case4, res4);

    if (res1) free(res1);
    if (res2) free(res2);
    if (res3) free(res3);
    if (res4) free(res4);

    return 0;
}
