#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chat-llm.h"

static void expect_valid(const char *label, const char *message, int expected)
{
    int got = validate_protocol_request_message("RTSP", message);
    printf("%s => %s\n", label, got ? "valid" : "invalid");
    if (got != expected) {
        fprintf(stderr, "Unexpected validation result for %s\n", label);
        exit(1);
    }
}

int main(void)
{
    const char *valid_play =
        "PLAY rtsp://127.0.0.1:8554/aacAudioTest/ RTSP/1.0\r\n"
        "CSeq: 5\r\n"
        "User-Agent: test\r\n"
        "Session: 000022B8\r\n"
        "\r\n";

    const char *invalid_setup =
        "SETUP rtsp://127.0.0.1:8554/aacAudioTest/track1 RTSP/1.0\r\n"
        "CSeq: 3\r\n"
        "User-Agent: test\r\n"
        "\r\n";

    const char *invalid_response =
        "RTSP/1.0 200 OK\r\n"
        "CSeq: 5\r\n"
        "\r\n";

    expect_valid("valid_play", valid_play, 1);
    expect_valid("invalid_setup_missing_transport", invalid_setup, 0);
    expect_valid("invalid_response_line", invalid_response, 0);

    return 0;
}
