#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "chat-llm.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <api_token>\n", argv[0]);
        return 1;
    }

    // 临时替换 MINIMAX_TOKEN
    extern char *MINIMAX_TOKEN;
    MINIMAX_TOKEN = argv[1];

    // 构造简单测试 prompt
    char *test_prompt = "[{\"role\": \"system\", \"content\": \"You are a helpful assistant.\"}, {\"role\": \"user\", \"content\": \"Say 'MiniMax API works!' in one sentence.\"}]";

    printf("[TEST] Calling chat_with_llm with MiniMax API...\n");
    printf("[TEST] Prompt: %s\n", test_prompt);

    char *answer = chat_with_llm(test_prompt, "turbo", 3, 0.5f);

    if (answer != NULL) {
        printf("[TEST] SUCCESS! LLM Response: %s\n", answer);
        free(answer);
        return 0;
    } else {
        printf("[TEST] FAILED! No response from LLM.\n");
        return 1;
    }
}