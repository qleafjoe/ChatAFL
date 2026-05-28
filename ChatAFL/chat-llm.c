#define _GNU_SOURCE // asprintf
#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>

#include "chat-llm.h"
#include "alloc-inl.h"
#include "hash.h"

// -lcurl -ljson-c -lpcre2-8
// apt install libcurl4-openssl-dev libjson-c-dev libpcre2-dev libpcre2-8-0

#define MAX_TOKENS 4096
#define CONFIDENT_TIMES 3

/* Parse prompt as a JSON array of messages. If it's not a valid JSON array,
   wrap it as a single user message. Returns a new json_object array. */
static json_object *parse_or_create_messages(const char *prompt) {
    json_object *messages_array = json_tokener_parse(prompt);

    if (!messages_array || json_object_get_type(messages_array) != json_type_array) {
        if (messages_array) json_object_put(messages_array);

        messages_array = json_object_new_array();
        json_object *msg_obj = json_object_new_object();
        json_object_object_add(msg_obj, "role", json_object_new_string("user"));
        json_object_object_add(msg_obj, "content", json_object_new_string(prompt));
        json_object_array_add(messages_array, msg_obj);
    }

    return messages_array;
}

struct MemoryStruct
{
    char *memory;
    size_t size;
};

static size_t chat_with_llm_helper(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL)
    {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/* Convert LF (\n) to CRLF (\r\n) for protocol messages.
 * LLM often generates Unix line endings, but RTSP/HTTP require CRLF.
 * Returns a new string (caller must free), or NULL on alloc failure. */
static char *lf_to_crlf(const char *input)
{
    if (!input) return NULL;

    size_t newlines = 0;
    for (const char *p = input; *p; p++) {
        if (*p == '\n' && (p == input || *(p - 1) != '\r')) {
            newlines++;
        }
    }

    if (newlines == 0) return strdup(input);

    size_t old_len = strlen(input);
    size_t new_len = old_len + newlines + 1;
    char *result = malloc(new_len);
    if (!result) return NULL;

    char *dst = result;
    for (const char *src = input; *src; src++) {
        if (*src == '\n' && (src == input || *(src - 1) != '\r')) {
            *dst++ = '\r';
        }
        *dst++ = *src;
    }
    *dst = '\0';
    return result;
}

char *chat_with_llm(char *prompt, char *model, int tries, float temperature)
{
    CURL *curl;
    CURLcode res = CURLE_OK;
    char *answer = NULL;
    
    // Read environments (treat both NULL and empty string as unset)
    const char *url_env = getenv("LLM_URL");
    const char *token_env = getenv("LLM_TOKEN");
    const char *model_env = getenv("LLM_MODEL");

    if (!url_env || url_env[0] == '\0') url_env = "https://api.minimaxi.com/v1/text/chatcompletion_v2";
    if (!token_env || token_env[0] == '\0') token_env = "sk-api-adJ3ML-ux_Ary01UQr8ehTDCoex9QhDJSln-9qQC49PvINgkw77-Vgtm7BZQSx3hHVzeQCr3K3FWD3hx-2uoG9S2kKdoYS4Q0akTfhMXDzSJR7cY08LiEJs";
    if (!model_env || model_env[0] == '\0') model_env = "MiniMax-M2.7";

    // Debug: print which LLM endpoint is being used
    printf("[LLM] Using URL: %s\n", url_env);
    printf("[LLM] Using Model: %s\n", model_env);

    char *auth_header = NULL;
    asprintf(&auth_header, "Authorization: Bearer %s", token_env);
    
    const char *content_header = "Content-Type: application/json";
    const char *accept_header = "Accept: application/json";
    char *data = NULL;

    json_object *root_obj = json_object_new_object();
    json_object *messages_array = parse_or_create_messages(prompt);

    json_object_object_add(root_obj, "model", json_object_new_string(model_env));
    json_object_object_add(root_obj, "messages", messages_array);
    json_object_object_add(root_obj, "max_tokens", json_object_new_int(MAX_TOKENS));
    json_object_object_add(root_obj, "temperature", json_object_new_double(temperature));

    data = strdup(json_object_to_json_string(root_obj));
    json_object_put(root_obj);


    curl_global_init(CURL_GLOBAL_DEFAULT);

    int retry_count = 0;
    int max_retries = tries;
    int base_delay = 1; /* base delay 1 second for exponential backoff */

    do
    {
        /* Exponential backoff delay: skip on first attempt */
        if (retry_count > 0) {
            int delay = base_delay * (1 << (retry_count - 1));
            if (delay > 30) delay = 30; /* cap at 30 seconds */
            printf("[LLM] Retry %d/%d, delaying %d seconds\n", retry_count, max_retries, delay);
            sleep(delay);
        }

        struct MemoryStruct chunk;

        chunk.memory = malloc(1); /* will be grown as needed by the realloc above */
        chunk.size = 0;           /* no data at this point */

        curl = curl_easy_init();
        if (curl)
        {
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, auth_header);
            headers = curl_slist_append(headers, content_header);
            headers = curl_slist_append(headers, accept_header);

            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
            curl_easy_setopt(curl, CURLOPT_URL, url_env);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chat_with_llm_helper);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

            res = curl_easy_perform(curl);

            if (res == CURLE_OK)
            {
                printf("[LLM] Raw Response: %s\n", chunk.memory);
                json_object *jobj = json_tokener_parse(chunk.memory);

                if (jobj && json_object_get_type(jobj) == json_type_object)
                {
                    json_object *choices = NULL;
                    if (json_object_object_get_ex(jobj, "choices", &choices) &&
                        json_object_get_type(choices) == json_type_array &&
                        json_object_array_length(choices) > 0)
                    {
                        json_object *first_choice = json_object_array_get_idx(choices, 0);
                        if (first_choice && json_object_get_type(first_choice) == json_type_object)
                        {
                            json_object *msg_obj = NULL;
                            if (json_object_object_get_ex(first_choice, "message", &msg_obj) &&
                                json_object_get_type(msg_obj) == json_type_object)
                            {
                                json_object *content_obj = NULL;
                                if (json_object_object_get_ex(msg_obj, "content", &content_obj) &&
                                    json_object_get_type(content_obj) == json_type_string)
                                {
                                    const char *data_str = json_object_get_string(content_obj);
                                    if (data_str) {
                                        answer = clean_llm_response(data_str);
                                    }
                                }
                            }
                        }
                    }

                    if (!answer)
                    {
                        printf("LLM Response parsed but no valid answer found (refused or malformed). Raw: %s\n", chunk.memory);
                    }
                }
                else
                {
                    printf("Error: LLM returned invalid JSON or error: %s\n", chunk.memory);
                }
                if (jobj) json_object_put(jobj);
            }
            else
            {
                printf("Error: %s\n", curl_easy_strerror(res));
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }

        free(chunk.memory);

        /* Success: exit retry loop */
        if (res == CURLE_OK && answer != NULL) {
            break;
        }

        retry_count++;
    } while (retry_count < max_retries);

    if (data != NULL)
    {
        free(data);
    }
    if (auth_header != NULL)
    {
        free(auth_header);
    }

    curl_global_cleanup();
    return answer;
}

char *clean_llm_response(const char *raw_response) {
    if (!raw_response) return NULL;

    /* 1. Context-aware refusal detection:
     *    Check if response contains protocol indicators. If it does,
     *    skip refusal detection to avoid false positives on valid protocol data. */
    int is_protocol_response = 0;
    const char *protocol_indicators[] = {
        "RTSP/1.0", "HTTP/1.1", "FTP", "SIP/2.0",
        "SETUP", "PLAY", "DESCRIBE", "OPTIONS",
        "200 OK", "400 Bad Request", "404 Not Found",
        NULL
    };

    for (int i = 0; protocol_indicators[i]; i++) {
        if (strcasestr(raw_response, protocol_indicators[i])) {
            is_protocol_response = 1;
            break;
        }
    }

    if (!is_protocol_response) {
        const char *refusals[] = {"sorry", "As an AI", "cannot fulfill", "can't help", "unable to", "policy", NULL};
        for (int i = 0; refusals[i]; i++) {
            if (strcasestr(raw_response, refusals[i])) {
                printf("[LLM] Refusal detected: %s\n", refusals[i]);
                return NULL;
            }
        }
    }

    /* 2. Robust JSON extraction: try json-c parsing first */
    json_object *json_obj = json_tokener_parse(raw_response);
    if (json_obj) {
        /* Successfully parsed as JSON (handles arrays, objects, etc.) */
        const char *json_str = json_object_to_json_string(json_obj);
        char *cleaned = lf_to_crlf(json_str);
        json_object_put(json_obj);
        if (cleaned) return cleaned;
    }

    /* 3. Fallback: simple JSON extraction by finding outermost braces/brackets */
    char *json_start = strpbrk(raw_response, "{[");
    char *json_end = NULL;
    if (json_start) {
        if (*json_start == '{') json_end = strrchr(json_start, '}');
        else json_end = strrchr(json_start, ']');
    }

    if (json_start && json_end && json_end > json_start) {
        size_t len = json_end - json_start + 1;
        char *cleaned = malloc(len + 1);
        if (cleaned) {
            memcpy(cleaned, json_start, len);
            cleaned[len] = '\0';

            /* Validate the extracted string is valid JSON */
            json_object *test_obj = json_tokener_parse(cleaned);
            if (test_obj) {
                json_object_put(test_obj);
                char *crlf_cleaned = lf_to_crlf(cleaned);
                free(cleaned);
                if (crlf_cleaned) return crlf_cleaned;
            } else {
                free(cleaned);
            }
        }
    }

    /* 4. For raw protocol data (stall breaking/enrichment), strip markdown and noise */
    char *res = strdup(raw_response);
    char *ptr = res;

    /* Skip leading markdown code blocks (e.g., ```rtsp, ```text) */
    if (strncmp(ptr, "```", 3) == 0) {
        ptr += 3;
        while (*ptr && isalpha(*ptr)) ptr++; /* skip language tag like 'rtsp' */
        while (*ptr && isspace(*ptr)) ptr++;
    }

    /* Strip trailing markdown backticks */
    char *end = ptr + strlen(ptr) - 1;
    while (end >= ptr && (isspace(*end) || *end == '`')) {
        *end = '\0';
        end--;
    }

    char *final_res = strdup(ptr);
    free(res);

    /* Convert LF (\n) to CRLF (\r\n) for protocol messages
     * LLM often generates Unix line endings, but RTSP/HTTP require CRLF */
    if (final_res) {
        char *crlf_res = lf_to_crlf(final_res);
        if (crlf_res) {
            free(final_res);
            return crlf_res;
        }
    }

    return final_res;
}

char *construct_prompt_stall(char *protocol_name, char *examples, char *history)
{
    char *template = "In the %s protocol, the communication history between the %s client and the %s server is as follows."
                     "The next proper client request that can affect the server's state are:\\n\\n"
                     "Desired format of real client requests:\\n%sCommunication History:\\n\\\"\\\"\\\"\\n%s\\\"\\\"\\\"\\n"
                     "(System constraint: Output exactly ONE complete client request message. MUST include headers and MUST end with \\\\r\\\\n\\\\r\\\\n. NO markdown, NO formatting, NO explanations.)";

    char *prompt = NULL;
    asprintf(&prompt, template, protocol_name, protocol_name, protocol_name, examples, history);

    json_object *messages = json_object_new_array();
    
    json_object *sys_msg = json_object_new_object();
    json_object_object_add(sys_msg, "role", json_object_new_string("system"));
    json_object_object_add(sys_msg, "content", json_object_new_string("You are a network protocol expert assistant. Output ONLY the raw required protocol command."));
    json_object_array_add(messages, sys_msg);

    json_object *user_msg = json_object_new_object();
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "content", json_object_new_string(prompt));
    json_object_array_add(messages, user_msg);

    char *final_prompt = strdup(json_object_to_json_string(messages));
    json_object_put(messages);

    free(prompt);

    return final_prompt;
}

char *construct_prompt_for_templates(char *protocol_name, char **final_msg)
{
    // Give one example for learning formats
    char *prompt_rtsp_example = "For the RTSP protocol, the DESCRIBE client request template is:\\n"
                                "DESCRIBE: [\\\"DESCRIBE <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"CSeq: <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"User-Agent: <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"Accept: <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"\\\\r\\\\n\\\"]";

    char *prompt_http_example = "For the HTTP protocol, the GET client request template is:\\n"
                                "GET: [\\\"GET <<VALUE>>\\\\r\\\\n\\\"]";

    char *msg = NULL;
    asprintf(&msg, "%s\\n%s\\nFor the %s protocol, output ALL client request templates. "
                   "Output ONLY strictly valid JSON. NO markdown, NO code blocks, NO explanations. "
                   "The templates must be comprehensive and follow the list-of-strings format.", 
                   prompt_rtsp_example, prompt_http_example, protocol_name);
    *final_msg = msg;
    /** Format of prompt_grammars
    prompt_grammars = [
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user", "content": msg}
    ]
     **/
    json_object *messages = json_object_new_array();
    
    json_object *sys_msg = json_object_new_object();
    json_object_object_add(sys_msg, "role", json_object_new_string("system"));
    json_object_object_add(sys_msg, "content", json_object_new_string("You are a helpful assistant."));
    json_object_array_add(messages, sys_msg);

    json_object *user_msg = json_object_new_object();
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "content", json_object_new_string(msg));
    json_object_array_add(messages, user_msg);

    char *prompt_grammars = strdup(json_object_to_json_string(messages));
    json_object_put(messages);

    return prompt_grammars;
}

char *construct_prompt_for_remaining_templates(char *protocol_name, char *first_question, char *first_answer)
{
    char *second_question = NULL;
    asprintf(&second_question, "For the %s protocol, other templates of client requests are:", protocol_name);

    json_object *answer_str = json_object_new_string(first_answer);

    json_object *messages = json_object_new_array();
    
    json_object *sys_msg = json_object_new_object();
    json_object_object_add(sys_msg, "role", json_object_new_string("system"));
    json_object_object_add(sys_msg, "content", json_object_new_string("You are a helpful assistant."));
    json_object_array_add(messages, sys_msg);

    json_object *user_msg1 = json_object_new_object();
    json_object_object_add(user_msg1, "role", json_object_new_string("user"));
    json_object_object_add(user_msg1, "content", json_object_new_string(first_question));
    json_object_array_add(messages, user_msg1);

    json_object *ast_msg = json_object_new_object();
    json_object_object_add(ast_msg, "role", json_object_new_string("assistant"));
    json_object_object_add(ast_msg, "content", json_object_new_string(first_answer));
    json_object_array_add(messages, ast_msg);

    json_object *user_msg2 = json_object_new_object();
    json_object_object_add(user_msg2, "role", json_object_new_string("user"));
    json_object_object_add(user_msg2, "content", json_object_new_string(second_question));
    json_object_array_add(messages, user_msg2);

    char *prompt = strdup(json_object_to_json_string(messages));
    json_object_put(messages);

    json_object_put(answer_str);
    free(second_question);

    return prompt;
}

char *extract_stalled_message(char *message, size_t message_len)
{
    if (message == NULL || message_len == 0) return NULL;
    
    char *start = message;
    while (*start == '\r' || *start == '\n' || *start == ' ' || *start == '`') start++;
    if (strncmp(start, "ftp", 3) == 0 || strncmp(start, "txt", 3) == 0) {
        start += 3;
    }
    while (*start == '\r' || *start == '\n' || *start == ' ') start++;

    if (*start == '\0') return NULL;

    char *res = strdup(start);
    // Remove trailing markdown
    char *end = res + strlen(res) - 1;
    while (end >= res && (*end == '\r' || *end == '\n' || *end == ' ' || *end == '`')) {
        *end = '\0';
        end--;
    }
    
    return res;
}

char *format_request_message(char *message)
{

    int message_len = strlen(message);
    int max_len = message_len;
    int res_len = 0;
    char *res = ck_alloc(message_len * sizeof(char));
    for (int i = 0; i < message_len; i++)
    {
        // If an \n is not padded with an \r before, we add it
        if (message[i] == '\n' && (i == 0 || (message[i - 1] != '\r')))
        {
            if (res_len == max_len)
            {
                res = ck_realloc(res, max_len + 10);
                max_len += 10;
            }
            res[res_len++] = '\r';
        }

        if (res_len == max_len)
        {
            res = ck_realloc(res, max_len + 10);
            max_len += 10;
        }
        res[res_len++] = message[i];
    }

    // Add \r\n\r\n to ensure that the packet is accepted
    for (int i = 0; i < 2; i++)
    {
        if (res_len == max_len)
        {
            res = ck_realloc(res, max_len + 10);
            max_len += 10;
        }
        res[res_len++] = '\r';
        if (res_len == max_len)
        {
            res = ck_realloc(res, max_len + 10);
            max_len += 10;
        }
        res[res_len++] = '\n';
    }

    if (res_len == max_len)
    {
        res = ck_realloc(res, max_len + 1);
        max_len++;
    }
    res[res_len++] = '\0';
    free(message);
    return res;
}

char *construct_prompt_for_protocol_message_types(char *protocol_name)
{
    /***
     * Prompt to ask the protocol states as follow:
     * ```
     * In the RTSP protocol, the protocol states are:
     *
     * Desired format:
     * <comma_separated_list_of_states_in_uppercase>
     * ```
     * ***/
    char *prompt = NULL;

    // transfer the prompt into string
    asprintf(&prompt, "In the %s protocol, the message types are: \\n\\n"
                      "Desired format: <comma_separated_list_of_states_in_uppercase_and_without_whitespaces>\\n"
                      "Constraint: Output ONLY the list. No explanations.", protocol_name);

    return prompt;
}

char *construct_prompt_for_requests_to_states(const char *protocol_name,
                                              const char *protocol_state,
                                              const char *example_requests)
{
    /***
     Prompt to ask the sequence of client requests to reach a protocol state as follows:
        ```
        In the RTSP protocol, if the server just starts, to reach the PLAYING state, the sequence of client requests can be:
        DESCRIBE rtsp://127.0.0.1:8554/aacAudioTest RTSP/1.0
        CSeq: 2
        User-Agent: ./testRTSPClient (LIVE555 Streaming Media v2018.08.28)
        Accept: application/sdp

        SETUP rtsp://127.0.0.1:8554/aacAudioTest/track1 RTSP/1.0
        CSeq: 3
        User-Agent: ./testRTSPClient (LIVE555 Streaming Media v2018.08.28)
        Transport: RTP/AVP;unicast;client_port=38784-38785

        PLAY rtsp://127.0.0.1:8554/aacAudioTest/ RTSP/1.0
        CSeq: 4
        User-Agent: ./testRTSPClient (LIVE555 Streaming Media v2018.08.28)
        Session: 000022B8
        Range: npt=0.000-

        Similarly, in the RTSP protocol, if the server just starts, to reach the RECORD state, the sequence of client requests can be:
     ***/

    // Transfer formats of example_requests
    json_object *example_requests_json = json_object_new_string(example_requests);
    const char *example_requests_json_str = json_object_to_json_string(example_requests_json);

    json_object *protocol_state_json = json_object_new_string(protocol_state);
    const char *protocol_state_json_str = json_object_to_json_string(protocol_state_json);

    char *prompt = NULL;

    int example_request_len = strlen(example_requests_json_str) - 2;
    if (example_request_len > EXAMPLE_SEQUENCE_PROMPT_LENGTH)
    {
        example_request_len = EXAMPLE_SEQUENCE_PROMPT_LENGTH;
    }

    asprintf(&prompt,
             "In the %s protocol, if the server just starts, to reach the INIT state, the sequence of client requests can be:\\n"
             "%.*s\\nSimilarly, in the %s protocol, if the server just starts, to reach the %.*s state, the sequence of client requests can be:\\n",
             protocol_name,
             example_request_len,
             example_requests_json_str + 1,
             protocol_name,
             (int)strlen(protocol_state_json_str) - 2,
             protocol_state_json_str + 1);

    json_object_put(protocol_state_json);
    json_object_put(example_requests_json);

    return prompt;
}

void extract_message_grammars(char *answers, klist_t(gram) * grammar_list)
{

    char *ptr = answers;
    int len = strlen(answers);

    while (ptr < answers + len)
    {
        char *start = strchr(ptr, '[');
        if (start == NULL)
            break;
        char *end = strchr(start, ']');
        if (end == NULL)
            break;
        int count = end - start + 1;
        char *temp = (char *)ck_alloc(count + 1);
        strncpy(temp, start, count);
        temp[count] = '\0';
        ptr = end + 1;

        // convert temp to json object and save it to the list
        json_object *jobj = json_tokener_parse(temp);
        if (jobj && json_object_get_type(jobj) == json_type_array) {
            *kl_pushp(gram, grammar_list) = jobj;
            printf("Found valid grammar array: %s\n", temp);
        } else {
            printf("Skipping invalid grammar snippet (not an array): %s\n", temp);
            if (jobj) json_object_put(jobj);
        }
        ck_free(temp);
    }
}

int parse_pattern(pcre2_code *replacer, pcre2_match_data *match_data, const char *str, size_t len, char *pattern)
{
    printf("[Grammar] Building pattern for: %.*s\n", (int)len, str);
    strcat(pattern, "(?:");
    // offset == 3;
    int rc = pcre2_match(replacer, str, len, 0, 0, match_data, NULL);

    if (rc < 0)
    {
        switch (rc)
        {
        case PCRE2_ERROR_NOMATCH:
            // printf("No match for %s!\n", str);
            break;
        default:
            // printf("Matching error %d\n", rc);
            break;
        }
        pcre2_match_data_free(match_data);
        pcre2_code_free(replacer);
        return 0;
    }
    // printf("RC is %d\n",rc);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    // for(int i = 1; i<rc;i++){
    //     printf("Start %d, end %d\n",ovector[2*i],ovector[2*i+1]);
    // }

    if (rc == 4)
    { // matched the first option - there is a special value
        strncat(pattern, str + ovector[2], ovector[3] - ovector[2]);
        // offset += ovector[3] - ovector[2];

        strcat(pattern, "(.*)");
        // offset += 3;

        strncat(pattern, str + ovector[6], ovector[7] - ovector[6]);
        // offset += ovector[7] - ovector[6];
    }
    else if (rc == 5)
    {
        // matched the second option - there is no special value
        strncat(pattern, str + ovector[8], ovector[9] - ovector[8]);
        // offset += ovector[9] - ovector[8];
    }
    else
    {
        FATAL("Regex groups were updated but not the handling code.");
    }
    strcat(pattern, ")");
    return 1;
}

// If successful, puts 2 patterns in the patterns array, the first one is the header, the second is the fields
// Else returns an array with the first element being NULL
char *extract_message_pattern(const char *header_str, khash_t(field_table) * field_table, pcre2_code **patterns, int debug_file, const char *debug_file_name)
{
    int errornumber;
    size_t erroroffset;
    char header_pattern[1024] = {0};
    char fields_pattern[8192] = {0};
    pcre2_code *replacer = pcre2_compile("(?:(.*)(?:<<(.*)>>)(.*))|(.+)", PCRE2_ZERO_TERMINATED, PCRE2_DOTALL, &errornumber, &erroroffset, NULL);
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(replacer, NULL);
    char *message_type = NULL;
    // int offset = 0;
    /**
     * Example output
     * patterns[0] = (?:PLAY (.*)\r\n)
     * patterns[1] = (?|(?:CSeq: (.*)\r\n)|(?:User-Agent: (.*)\r\n)|(?:Range: (.*)\r\n)|(?:\r\n))
     */

    {
        // We use the string in such an escaped format for easier debugging as the regex library supports parsing it properly
        // The string contains quotations so they are ignored
        header_str++;

        int message_len = 0;
        while (header_str[message_len] != '\0' 
        && header_str[message_len] != ' ' 
        && header_str[message_len] != '\n' 
        && header_str[message_len] != '\r' 
        && header_str[message_len] != '\\' )
        {
            message_len++;
        }
        message_type = ck_alloc(message_len + 1);
        memcpy(message_type, header_str, message_len);
        message_type[message_len] = '\0';

        size_t len = strlen(header_str) - 1;
        strcat(header_pattern, "^"); // Ensure that it captures the start of the string
        if (!parse_pattern(replacer, match_data, header_str, len, header_pattern))
        {
            patterns[0] = NULL;
            return NULL;
        }
    }

    int first = 1;

    strcat(fields_pattern, "(?|");
    for (khiter_t field_t_iter = kh_begin(field_table); field_t_iter != kh_end(field_table); ++field_t_iter)
    {
        if (!kh_exist(field_table, field_t_iter) || kh_value(field_table, field_t_iter) < (TEMPLATE_CONSISTENCY_COUNT / 2 + (TEMPLATE_CONSISTENCY_COUNT % 2)))
            continue;

        if (!first)
        {
            strcat(fields_pattern, "|");
        }
        else
        {
            first = 0;
        }

        json_object *field_v = json_object_new_string(kh_key(field_table, field_t_iter));
        const char *str = json_object_to_json_string(field_v);
        // We use the string in such an escaped format for easier debugging as the regex library supports parsing it properly
        // The string contains quotations so they are ignored
        str++;
        size_t len = strlen(str) - 1;
        int matched = parse_pattern(replacer, match_data, str, len, fields_pattern);
        json_object_put(field_v);
        if (!matched)
        {
            patterns[0] = NULL;
            return NULL;
        }
    }

    strcat(fields_pattern, ")");

    if (first == 1)
    { // convert from (?|) to (.+) when the group is empty
        fields_pattern[1] = '.';
        fields_pattern[2] = '+';
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(replacer);
    printf("Header pattern is %s\n", header_pattern);
    printf("Fields pattern is %s\n", fields_pattern);

    if (debug_file != -1 && debug_file_name != NULL)
    {
        ck_write(debug_file, header_pattern, strlen(header_pattern), debug_file_name);
        ck_write(debug_file, "\n", 1, debug_file_name);
        ck_write(debug_file, fields_pattern, strlen(fields_pattern), debug_file_name);
    }

    {
        pcre2_code *p = pcre2_compile(header_pattern, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, NULL);
        pcre2_jit_compile(p, PCRE2_JIT_COMPLETE);
        patterns[0] = p;
    }
    {
        pcre2_code *p = pcre2_compile(fields_pattern, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, NULL);
        pcre2_jit_compile(p, PCRE2_JIT_COMPLETE);
        patterns[1] = p;
    }
    return message_type;
}

range_list starts_with(char *line, int length, pcre2_code *pattern)
{
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(pattern, NULL);

    int rc = pcre2_match(pattern, line, length, 0, 0, match_data, NULL); // find the first range

    // printf("starts_with rc is %d\n", rc);
    if (rc < 0)
    {
        switch (rc)
        {
        case PCRE2_ERROR_NOMATCH:
            // printf("No match!\n");
            break;
        default:
            // printf("Matching error %d\n", rc);
            break;
        }
        pcre2_match_data_free(match_data);
        range_list res;
        kv_init(res);
        return res;
    }

    range_list dyn_ranges;
    kv_init(dyn_ranges);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    for (int i = 1; i < rc; i++)
    {
        if (ovector[2 * i] == -1)
            continue;
        // printf("Group %d %d %d\n",i, ovector[2 * i], ovector[2 * i + 1]);
        range v = {.start = ovector[2 * i], .len = ovector[2 * i + 1] - ovector[2 * i], .mutable = 1};
        kv_push(range, dyn_ranges, v);
        // kv_push(range, dyn_ranges, v);
        //  ranges[0][i - 1] = v;
    }
    range v = {.start = ovector[0], .len = ovector[1] - ovector[0], .mutable = 1};
    kv_push(range, dyn_ranges, v); // add the global range at the end

    pcre2_match_data_free(match_data);
    return dyn_ranges;
}

range_list get_mutable_ranges(char *line, int length, int offset, pcre2_code *pattern)
{
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(pattern, NULL);

    range_list dyn_ranges;
    kv_init(dyn_ranges);

    for (;;) // catch all the other ranges
    {
        int rc = pcre2_match(pattern, line, length, offset, 0, match_data, NULL);
        if (rc < 0)
        {
            switch (rc)
            {
            case PCRE2_ERROR_NOMATCH:
                // printf("No match!\n");
                break;
            default:
                // printf("Matching error %d\n", rc);
                break;
            }
            pcre2_match_data_free(match_data);
            match_data = NULL;
            break;
        }
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
        if (offset != ovector[0])
        {
            range v = {.start = offset, .len = ovector[0] - offset, .mutable = 1};
            kv_push(range, dyn_ranges, v);
        }

        // printf("Matched over %d %d\n", ovector[0], ovector[1]);
        for (int i = 1; i < rc; i++)
        {
            if (ovector[2 * i] == -1)
                continue;
            // printf("Group %d %d %d\n",i, ovector[2 * i], ovector[2 * i + 1]);
            range v = {.start = ovector[2 * i], .len = ovector[2 * i + 1] - ovector[2 * i], .mutable = 1};
            kv_push(range, dyn_ranges, v);
            // ranges[0][i - 1] = v;
        }
        if (offset == ovector[1])
        { // in the case the match is empty, we just move a step forward
            offset++;
        }
        else
        {
            offset = ovector[1];
        }
    }

    if (offset < length) // catch anything past the last matched pattern
    {
        range v = {.start = offset, .len = length - offset, .mutable = 1};
        kv_push(range, dyn_ranges, v);
    }

    if (match_data != NULL)
    {
        pcre2_match_data_free(match_data);
    }
    return dyn_ranges;
}

char *unescape_string(const char *input)
{
    size_t length = strlen(input);
    char *output = (char *)malloc((length + 1) * sizeof(char));

    if (output == NULL)
    {
        printf("Memory allocation failed.\n");
        return NULL;
    }

    size_t i, j = 0;
    for (i = 0; i < length; i++)
    {
        if (input[i] == '\\')
        {
            i++; // Skip the backslash
            switch (input[i])
            {
            case 'n':
                output[j++] = '\n';
                break;
            case 't':
                output[j++] = '\t';
                break;
            case 'r':
                output[j++] = '\r';
                break;
            case '\\':
                output[j++] = '\\';
                break;
            default:
                output[j++] = input[i];
                break;
            }
        }
        else
        {
            output[j++] = input[i];
        }
    }

    output[j] = '\0'; // Add null-terminator to the output string
    return output;
}

void write_new_seeds(char *enriched_file, char *contents)
{
    FILE *fp = fopen(enriched_file, "w");
    if (fp == NULL)
    {
        printf("Error in opening the file %s\n", enriched_file);
        exit(1);
    }

    // remove the newline and whiltespace in the beginning of the string if any
    while (contents[0] == '\n' || contents[0] == ' ' || contents[0] == '\t' || contents[0] == '\r')
    {
        contents++;
    }

    // Check if last 4 characters of the client_request_answer string are \r\n\r\n
    // If not, add them
    int len = strlen(contents);
    if (contents[len - 1] != '\n' || contents[len - 2] != '\r' || contents[len - 3] != '\n' || contents[len - 4] != '\r')
    {
        fprintf(fp, "%s\r\n\r\n", contents);
    }
    else
    {
        fprintf(fp, "%s", contents);
    }

    fclose(fp);
}

char *format_string(char *state_string)
{
    // remove the newline and whiltespace in the beginning of the string if any
    while (state_string[0] == '\n' || state_string[0] == ' ' || state_string[0] == '\t' || state_string[0] == '\r')
    {
        state_string++;
    }

    int len = strlen(state_string);
    while (state_string[len - 1] == '\n' || state_string[len - 1] == '\r' || state_string[len - 1] == ' ' || state_string[len - 1] == '.')
    {
        state_string[len - 1] = '\0';
        len--;
    }

    return state_string;
}

/***
 * Get the protocol states based on self-consistency check
 * pass the parameters: protocol_name, states_set, states_string
 ***/
void get_protocol_message_types(char *state_prompt, khash_t(strSet) * states_set)
{
    khash_t(strMap) *state_to_times = kh_init(strMap); // map from state to times

    for (int i = 0; i < CONFIDENT_TIMES; i++)
    {
        char *state_answer = chat_with_llm(state_prompt, "instruct", MESSAGE_TYPE_RETRIES, 0.5);
        if (state_answer == NULL)
            continue;
        // printf("## Answer from LLM:\n %s\n", state_answer);

        state_answer = format_string(state_answer);

        char *state_tokens = strtok(state_answer, ",");
        while (state_tokens != NULL)
        {
            char *protocol_state = state_tokens;
            protocol_state = format_string(protocol_state);
            // save the state to the map
            int ret;
            khiter_t k = kh_put(strMap, state_to_times, protocol_state, &ret);
            if (ret == 0)
            {
                kh_value(state_to_times, k)++;
            }
            else
            {
                kh_value(state_to_times, k) = 1;
            }

            state_tokens = strtok(NULL, ",");
        }
    }

    // traverse the map and get the states whose times are larger than 0.5 * CONFIDENT_TIMES
    for (khiter_t k = kh_begin(state_to_times); k != kh_end(state_to_times); ++k)
    {
        if (kh_exist(state_to_times, k))
        {
            if (kh_value(state_to_times, k) >= 0.5 * CONFIDENT_TIMES)
            {
                const char *protocol_state = kh_key(state_to_times, k);
                // add the state to the set
                int ret;
                kh_put(strSet, states_set, protocol_state, &ret);
            }
        }
    }
}

khash_t(strSet) * duplicate_hash(khash_t(strSet) * set)
{
    khash_t(strSet) *new_set = kh_init(strSet);

    for (khiter_t k = kh_begin(set); k != kh_end(set); ++k)
    {
        if (kh_exist(set, k))
        {
            const char *val = kh_key(set, k);
            int ret;
            kh_put(strSet, new_set, val, &ret);
        }
    }

    return new_set;
}

// message_set_list generate_combinations(khash_t(strSet)* sequence, int size)
// {
//     if(size == 0)
//     {
//         message_set_list output;
//         kv_init(output);
//         kv_push(khash_t(strSet)*,output,kh_init(strSet));
//         return output;
//     }
//     else
//     {
//         message_set_list subcombinations = generate_combinations(sequence,size-1);
//         message_set_list newCombinations;
//         kv_init(newCombinations);
//         for(int i = 0; i < kv_size(subcombinations);i++)
//         {
//             khash_t(strSet)* target = kv_A(subcombinations,i);
//             khiter_t sequence_iter;
//             for (sequence_iter = kh_begin(sequence); sequence_iter != kh_end(sequence); sequence_iter++)
//             {
//                 if (!kh_exist(sequence, sequence_iter))
//                     continue;
//                 khiter_t k = kh_get(strSet, target, kh_val(sequence,sequence_iter));
//                 if (kh_exist(target, k))
//                     continue;
//                 khash_t(strSet)* newCombination = duplicate_hash(target);
//                 int absent;
//                 kh_put(strSet,newCombination,kh_val(sequence,sequence_iter))    
//             }
//         }
//         return newCombinations;
//     }
// }
void make_combination(khash_t(strSet)* sequence, char** data , message_set_list* res,khiter_t st, khiter_t end, int index, int size);

message_set_list message_combinations(khash_t(strSet)* sequence, int size)
{
    message_set_list res;
    kv_init(res);
    char* data[size];
    make_combination(sequence,data, &res, kh_begin(sequence), kh_end(sequence), 0, size);
    return res;
}

void make_combination(khash_t(strSet)* sequence, char** data , message_set_list* res,khiter_t st, khiter_t end,
                     int index, int size)
{

    if (index == size)
    {
        khash_t(strSet)* combination = kh_init(strSet);
        int absent;
        for (int j=0; j<size; j++){
            kh_put(strSet,combination, data[j],&absent );
        }
        kv_push(khash_t(strSet)*,*res,combination);
        return;
    }
    for (khiter_t i=st; i != end && end-i+1 >= size-index; i++)
    {
        if(!kh_exist(sequence,i))
            continue;
        data[index] = kh_key(sequence,i);
        make_combination(sequence, data,res, i+1, end, index+1, size);
    }
}



int min(int a, int b) {
    return a < b ? a : b;
}

char *enrich_sequence_with_prompt(char *sequence, khash_t(strSet) * missing_message_types, char **prompt_out)
{
    const char *prompt_template =
        "The following is one sequence of client requests:\\n"
        "%.*s\\n"
        "Please add the %.*s client requests in the proper locations, and the modified sequence of client requests is: "
        "(System constraint: Output ONLY the raw protocol commands. NO markdown code blocks, NO explanations, NO intro text. ONLY output the raw TCP sequence.)";

    int missing_fields_len = 0;
    int missing_fields_capacity = 100;
    char *missing_fields_seq = ck_alloc(missing_fields_capacity);

    khiter_t k;
    int i = 0;
    for (k = kh_begin(missing_message_types); 
    k != kh_end(missing_message_types) && i < min(MAX_ENRICHMENT_MESSAGE_TYPES, kh_size(missing_message_types)); 
    ++k)
    {
        if (!kh_exist(missing_message_types, k))
            continue;
        ++i; // Increment only after seeing a message type
        const char *message_type = kh_key(missing_message_types, k);
        int needed_len = strlen(message_type) + 2; // add for the ', '

        if (missing_fields_len + needed_len > missing_fields_capacity)
        {
            missing_fields_capacity += 2 * needed_len;
            missing_fields_seq = ck_realloc(missing_fields_seq, missing_fields_capacity);
        }

        memcpy(missing_fields_seq + missing_fields_len, message_type, strlen(message_type));
        memcpy(missing_fields_seq + missing_fields_len + needed_len - 2, ", ", 2);

        missing_fields_len += needed_len;
    }
    missing_fields_len -= 2; // ignore the last ', '

    char *prompt = NULL;

    json_object *sequence_escaped = json_object_new_string(sequence);
    const char *sequence_escaped_str = json_object_to_json_string(sequence_escaped);
    sequence_escaped_str++;

    int sequence_len = strlen(sequence_escaped_str) - 1;
    int allowed_tokens = (MAX_TOKENS - strlen(prompt_template) - missing_fields_len);
    if (sequence_len > allowed_tokens)
    {
        sequence_len = allowed_tokens;
    }
    asprintf(&prompt, prompt_template, sequence_len, sequence_escaped_str, missing_fields_len, missing_fields_seq);
    ck_free(missing_fields_seq);
    json_object_put(sequence_escaped);

    char *response = chat_with_llm(prompt, "instruct", ENRICHMENT_RETRIES, 0.5);
    if (response) {
        // Step 3: Sanity check for enriched message
        if (strstr(response, "\r\n\r\n") == NULL) {
            printf("[LLM] Enriched message missing \\r\\n\\r\\n, appending...\n");
            char *new_resp = NULL;
            asprintf(&new_resp, "%s\r\n\r\n", response);
            free(response);
            response = new_resp;
        }
    }

    if (prompt_out) {
        *prompt_out = prompt;
    } else {
        free(prompt);
    }

    return response;
}

char *enrich_sequence(char *sequence, khash_t(strSet) * missing_message_types)
{
    return enrich_sequence_with_prompt(sequence, missing_message_types, NULL);
}

/* ---- Feedback retry: construct feedback prompt for stall stage ---- */
static char *construct_feedback_prompt_stall(
    const char *protocol_name,
    const char *failed_message,
    const char *error_detail)
{
    char *prompt_text = NULL;
    asprintf(&prompt_text,
        "The following %s client request message was generated but FAILED validation:\n\n"
        "--- BEGIN FAILED MESSAGE ---\n%s\n--- END FAILED MESSAGE ---\n\n"
        "Validation error: %s\n\n"
        "Please generate a CORRECTED %s client request that fixes the above error. "
        "Output exactly ONE complete client request message. "
        "MUST include proper headers and MUST end with \\r\\n\\r\\n. "
        "NO markdown, NO formatting, NO explanations.",
        protocol_name, failed_message, error_detail, protocol_name);

    json_object *messages = json_object_new_array();

    json_object *sys_msg = json_object_new_object();
    json_object_object_add(sys_msg, "role", json_object_new_string("system"));
    json_object_object_add(sys_msg, "content", json_object_new_string(
        "You are a network protocol expert assistant. "
        "The previous message failed validation. "
        "Fix the described issue and output ONLY the raw corrected protocol message."));
    json_object_array_add(messages, sys_msg);

    json_object *user_msg = json_object_new_object();
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "content", json_object_new_string(prompt_text));
    json_object_array_add(messages, user_msg);

    char *result = strdup(json_object_to_json_string(messages));
    json_object_put(messages);
    free(prompt_text);

    return result;
}

/* ---- Feedback retry: construct feedback prompt for enrichment stage ---- */
static char *construct_feedback_prompt_enrichment(
    const char *protocol_name,
    const char *failed_message,
    const char *error_detail)
{
    char *prompt_text = NULL;
    asprintf(&prompt_text,
        "The following %s message sequence was generated but FAILED validation:\n\n"
        "--- BEGIN FAILED SEQUENCE ---\n%s\n--- END FAILED SEQUENCE ---\n\n"
        "Validation error: %s\n\n"
        "Please generate a CORRECTED %s message sequence that fixes the above error. "
        "The sequence should contain multiple properly formatted client request messages. "
        "Each message MUST end with \\r\\n\\r\\n. "
        "NO markdown, NO formatting, NO explanations.",
        protocol_name, failed_message, error_detail, protocol_name);

    json_object *messages = json_object_new_array();

    json_object *sys_msg = json_object_new_object();
    json_object_object_add(sys_msg, "role", json_object_new_string("system"));
    json_object_object_add(sys_msg, "content", json_object_new_string(
        "You are a network protocol expert assistant. "
        "The previous message sequence failed validation. "
        "Fix the described issue and output ONLY the raw corrected protocol messages."));
    json_object_array_add(messages, sys_msg);

    json_object *user_msg = json_object_new_object();
    json_object_object_add(user_msg, "role", json_object_new_string("user"));
    json_object_object_add(user_msg, "content", json_object_new_string(prompt_text));
    json_object_array_add(messages, user_msg);

    char *result = strdup(json_object_to_json_string(messages));
    json_object_put(messages);
    free(prompt_text);

    return result;
}

/* ---- Feedback retry: stall stage ---- */
char *llm_feedback_retry_stall(
    const char *protocol_name,
    const char *failed_message,
    llm_validation_result_t error,
    llm_validation_mode_t mode,
    int max_retries)
{
    if (!protocol_name || !failed_message || max_retries <= 0)
        return NULL;

    const char *error_detail = get_validation_error_detail(protocol_name, error, failed_message);
    printf("[LLM-Feedback-Stall] Error: %s\n", error_detail);

    for (int attempt = 0; attempt < max_retries; attempt++) {
        printf("[LLM-Feedback-Stall] Retry %d/%d\n", attempt + 1, max_retries);

        char *feedback_prompt = construct_feedback_prompt_stall(protocol_name, failed_message, error_detail);
        char *llm_response = chat_with_llm(feedback_prompt, "instruct", 1, 0.7);
        free(feedback_prompt);

        if (!llm_response) {
            printf("[LLM-Feedback-Stall] LLM returned NULL on attempt %d\n", attempt + 1);
            continue;
        }

        char *extracted = extract_stalled_message(llm_response, strlen(llm_response));
        free(llm_response);

        if (!extracted) {
            printf("[LLM-Feedback-Stall] extract_stalled_message returned NULL on attempt %d\n", attempt + 1);
            continue;
        }

        char *formatted = format_request_message(extracted);
        // format_request_message frees extracted, so 'extracted' is now dangling

        protocol_context_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        llm_validation_result_t vresult = validate_llm_message_with_mode(
            protocol_name, LLM_STAGE_STALL, formatted, &ctx, mode);

        if (vresult == LLM_VALID_OK) {
            printf("[LLM-Feedback-Stall] Recovered on attempt %d\n", attempt + 1);
            return formatted; // caller must ck_free()
        }

        printf("[LLM-Feedback-Stall] Validation failed (%d) on attempt %d\n", vresult, attempt + 1);
        ck_free(formatted);
    }

    printf("[LLM-Feedback-Stall] All %d retries exhausted\n", max_retries);
    return NULL;
}

/* ---- Feedback retry: enrichment stage ---- */
char *llm_feedback_retry_enrichment(
    const char *protocol_name,
    const char *failed_message,
    llm_validation_result_t error,
    llm_validation_mode_t mode,
    int max_retries)
{
    if (!protocol_name || !failed_message || max_retries <= 0)
        return NULL;

    const char *error_detail = get_validation_error_detail(protocol_name, error, failed_message);
    printf("[LLM-Feedback-Enrichment] Error: %s\n", error_detail);

    for (int attempt = 0; attempt < max_retries; attempt++) {
        printf("[LLM-Feedback-Enrichment] Retry %d/%d\n", attempt + 1, max_retries);

        char *feedback_prompt = construct_feedback_prompt_enrichment(protocol_name, failed_message, error_detail);
        char *llm_response = chat_with_llm(feedback_prompt, "instruct", 1, 0.7);
        free(feedback_prompt);

        if (!llm_response) {
            printf("[LLM-Feedback-Enrichment] LLM returned NULL on attempt %d\n", attempt + 1);
            continue;
        }

        char *unescaped = unescape_string(llm_response);
        free(llm_response);

        if (!unescaped) {
            printf("[LLM-Feedback-Enrichment] unescape_string returned NULL on attempt %d\n", attempt + 1);
            continue;
        }

        char *formatted = format_request_message(unescaped);
        // format_request_message frees unescaped, so 'unescaped' is now dangling

        protocol_context_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        llm_validation_result_t vresult = validate_llm_sequence_with_mode(
            protocol_name, LLM_STAGE_ENRICHMENT, formatted, &ctx, mode);

        if (vresult == LLM_VALID_OK) {
            printf("[LLM-Feedback-Enrichment] Recovered on attempt %d\n", attempt + 1);
            return formatted; // caller must ck_free()
        }

        printf("[LLM-Feedback-Enrichment] Validation failed (%d) on attempt %d\n", vresult, attempt + 1);
        ck_free(formatted);
    }

    printf("[LLM-Feedback-Enrichment] All %d retries exhausted\n", max_retries);
    return NULL;
}

// // For debugging
// // gcc -g -o chat-llm chat-llm.c chat-llm.h -lcurl -ljson-c -lpcre2-8
// int main(int argc, char **argv)
// {
//     char *protocol_name = argv[1];
//     char *in_dir = argv[2];
//     khash_t(strSet) *states_set = kh_init(strSet);

//     char *state_prompt = construct_prompt_for_protocol_states(protocol_name);

//     // Get protocol states
//     get_protocol_message_types(state_prompt, states_set);

//     // traverse the states_set
//     khiter_t k;
//     for (k = kh_begin(states_set); k != kh_end(states_set); ++k)
//     {
//         if (kh_exist(states_set, k))
//         {
//             const char *protocol_state = kh_key(states_set, k);
//             printf("## State_traverse: %s\n", protocol_state);
//         }
//     }

//     // Get seeds to states and save them to the in_dir
//     get_seeds_to_states(in_dir, states_set, protocol_name);

//     // char *prompt = NULL;
//     // asprintf(&prompt, "user: The colors of flowers:\\nassistant: red and yellow.\\nuser: Other colors are:");
//     // printf("## Prompt to LLM:\n %s\n", prompt);
//     // char *answer = chat_with_llm(prompt, "instruct");
//     // printf("## Answer from LLM:\n %s\n", answer);

//     char *protocol_name = argv[1];
//     khash_t(consistency_table) *const_table = kh_init(consistency_table);
//     klist_t(rang) *protocol_patterns = kl_init(rang);

//     for (int iter = 0; iter < 5; iter++)
//     {

//         char *templates_prompt = construct_prompt_for_templates(protocol_name);
//         char *templates_answer = chat_with_llm(templates_prompt, "turbo");
//         // printf("## Answer from LLM:\n %s\n", templates_answer);
//         char *remaining_prompt = construct_prompt_for_remaining_templates(protocol_name, templates_prompt, templates_answer);
//         // printf("remaining prompt is:\n %s\n", remaining_prompt);
//         char *remaining_templates = chat_with_llm(remaining_prompt, "turbo");
//         // printf("## Remaining templates:\n %s\n", remaining_templates);

//         char *combined_templates = NULL;
//         asprintf(&combined_templates, "%s\n%s", templates_answer, remaining_templates);

//         printf("The final info is\n%s\n", combined_templates);
//         klist_t(gram) *grammar_list = kl_init(gram);
//         extract_message_grammars(combined_templates, grammar_list);

//         kliter_t(gram) * iter;
//         for (iter = kl_begin(grammar_list); iter != kl_end(grammar_list); iter = kl_next(iter))
//         {
//             json_object *jobj = kl_val(iter);

//             json_object *header = json_object_array_get_idx(jobj, 0);

//             int absent;

//             const char *header_str = json_object_get_string(header);

//             khiter_t k = kh_put(consistency_table, const_table, header_str, &absent);
//             if (absent)
//             {
//                 khash_t(field_table) *field_table = kh_init(field_table);
//                 kh_value(const_table, k) = field_table;
//             }

//             for (int i = 1; i < json_object_array_length(jobj); i++)
//             {
//                 const char *v = json_object_get_string(json_object_array_get_idx(jobj, i));
//                 khash_t(field_table) *field_table = kh_value(const_table, k);
//                 khiter_t field_k = kh_put(field_table, field_table, v, &absent);
//                 if (absent)
//                 {
//                     kh_value(field_table, field_k) = 0;
//                 }
//                 kh_value(field_table, field_k)++;
//             }
//         }
//         kl_destroy_gram(grammar_list);
//     }

//     for (khiter_t con_t_iter = kh_begin(const_table); con_t_iter != kh_end(const_table); ++con_t_iter)
//     {
//         if (kh_exist(const_table, con_t_iter))
//         {
//             pcre2_code **patterns = ck_alloc(2 * sizeof(pcre2_code *));

//             khash_t(field_table) *field_table = kh_value(const_table, con_t_iter);
//             const char* header_str = json_object_to_json_string(json_object_new_string(kh_key(const_table, con_t_iter)));

//             extract_message_pattern_k(header_str,field_table, patterns);
//             *kl_pushp(rang, protocol_patterns) = patterns;
//         }
//     }

//     char *demo_lines[] = {

//         "DESCRIBE 123\r\n"
//         "CSeq: 1212313\r\n"
//         "User-Agent: 1212313\r\n"
//         "Accept: 1212313\r\n"
//         "\r\n",

//         "DESCRIBE 123\r\n"
//         "DESCRIBE 123\r\n"
//         "User-Agent: 1212313\r\n"
//         "CSeq: 1212313\r\n"
//         "Accept: 1212313\r\n"
//         "\r\n",

//         "DESCRIBE 123\r\n"
//         "1231321321321"
//         "User-Agent: 1212313\r\n"
//         "CSeq: 1212313\r\n"
//         "Accept: 1212313\r\n"
//         "\r\n"
//         "1231321321321",

//         "DESCRIBE 123\r\n"
//         "1231321321321"
//         "User-Agent: 1212313\r\n"
//         "CSeq: 1212313\r1231321321321\n"
//         "Accept: 1212313\r\n"
//         "\r\n"
//         "1231321321321",

//         "PLAY 123\r\n"
//         "CSeq: 1212313\r\n"
//         "DESCRIBE 123\r\n"
//         "User-Agent: 1212313\r\n"
//         "Session: 1212313\r\n"
//         "Range: 1212313\r\n"
//         "\r\n",

//     };

// char* answers = "For the RTSP protocol, the DESCRIBE client request template is:"
//     "{\"DESCRIBE\":\"string\\r\\n\",\"CSeq:\":\"integer\\r\\n\",\"User-Agent:\":\"string\\r\\n\",\"Accept:\":\"string\\r\\n\\r\\n\"}."
//     "For the RTSP protocol, the DESCRIBE client request template is:{\"DESCRIBE\":\"string\\r\\n\",\"CSeq:\":\"integer\\r\\n\",\"User-Agent:\":\"string\\r\\n\",\"Accept:\":\"string\\r\\n\\r\\n\"}";

// for (int demo = 0; demo < sizeof(demo_lines) / sizeof(char *); demo++)
// {
//     printf("\nTrying to match \n%s\n\n", demo_lines[demo]);
//     int max_rc = -1;
//     kliter_t(rang) * iter_rang;
//     range_list max_ranges;
//     int i = 0;
//     for (iter_rang = kl_begin(protocol_patterns); iter_rang != kl_end(protocol_patterns); iter_rang = kl_next(iter_rang),i++)
//     {
//         // printf("Compare! \n");

//         pcre2_code **patterns = kl_val(iter_rang);
//         pcre2_code *header_pattern = patterns[0];
//         pcre2_code *fields_pattern = patterns[1];

//         range_list header_ranges = starts_with(demo_lines[demo], strlen(demo_lines[demo]), header_pattern);
//         kv_init(header_ranges);

//         if (kv_size(header_ranges) == 0)
//         {
//             printf("Demo %d Did not match pattern %d\n", demo, i);
//             continue;
//         }
//         else
//         {
//             printf("Demo %d Did matched pattern %d\n", demo, i);
//             range header_match = kv_pop(header_ranges);
//             char *offsetted_line = demo_lines[demo];
//             size_t offsetted_len = strlen(demo_lines[demo]);
//             range_list field_ranges = get_mutable_ranges(offsetted_line,offsetted_len, header_match.len,fields_pattern);

//             for(int i = 0; i < kv_size(field_ranges);i++){
//                 kv_push(range, header_ranges, kv_A(field_ranges,i));
//             }
//             kv_destroy(field_ranges);

//             max_ranges = header_ranges;

//             break;
//         }
//     }

//     if (max_rc != -1)
//     {
//         printf("Matched! \n");
//         for (int i = 0; i < max_rc; i++)
//         {
//             printf("start=%d len=%d mutable=%d\n", kv_A(max_ranges,i).start,kv_A(max_ranges,i).len, kv_A(max_ranges,i).mutable);
//             printf("content=%s\n", json_object_to_json_string(json_object_new_string_len(demo_lines[demo] + kv_A(max_ranges,i).start, kv_A(max_ranges,i).len)));
//         }
//     }
//     else
//     {
//         printf("No matches\n");
//     }
// }

// Traverse the list

//     return 0;
// }
