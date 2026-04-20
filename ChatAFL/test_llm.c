#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "chat-llm.h"

int main(int argc, char **argv) {
    printf("========== STARTING FULL LLM SIMULATION TEST ==========\n\n");

    printf("=== [1] GRAMMAR EXTRACTION TEST ===\n");
    char *protocol = "FTP";
    char *final_msg = NULL;
    char *grammar_prompt = construct_prompt_for_templates(protocol, &final_msg);
    
    printf("[*] Sending Template Request 1/2...\n");
    char *grammar_resp1 = chat_with_llm(grammar_prompt, "chat", 3, 0.5);
    
    printf("[*] Sending Remaining Template Request 2/2...\n");
    char *remaining_prompt = construct_prompt_for_remaining_templates(protocol, final_msg, grammar_resp1);
    char *grammar_resp2 = chat_with_llm(remaining_prompt, "chat", 3, 0.5);

    char *combined_templates = NULL;
    asprintf(&combined_templates, "%s\n%s", grammar_resp1, grammar_resp2);

    klist_t(gram) *grammar_list = kl_init(gram);
    extract_message_grammars(combined_templates, grammar_list);
    printf("[+] Total Grammars Extracted by JSON parser: %d\n", grammar_list->size);

    khash_t(consistency_table) *const_table = kh_init(consistency_table);
    
    // Simulate Consistency Table populating
    kliter_t(gram) * iter;
    for (iter = kl_begin(grammar_list); iter != kl_end(grammar_list); iter = kl_next(iter))
    {
        json_object *jobj = kl_val(iter);
        if (json_object_get_type(jobj) != json_type_array || json_object_array_length(jobj) == 0) continue;
        json_object *header = json_object_array_get_idx(jobj, 0);
        int absent;
        const char *header_str = json_object_get_string(header);
        khiter_t k = kh_put(consistency_table, const_table, header_str, &absent);
        if (absent)
        {
            khash_t(field_table) *field_table = kh_init(field_table);
            kh_value(const_table, k) = field_table;
        }
        for (int i = 1; i < json_object_array_length(jobj); i++)
        {
            const char *v = json_object_get_string(json_object_array_get_idx(jobj, i));
            khash_t(field_table) *field_table = kh_value(const_table, k);
            khiter_t field_k = kh_put(field_table, field_table, v, &absent);
            if (absent) kh_value(field_table, field_k) = 0;
            kh_value(field_table, field_k) += (TEMPLATE_CONSISTENCY_COUNT); 
        }
    }
    
    int valid_patterns = 0;
    for (khiter_t con_t_iter = kh_begin(const_table); con_t_iter != kh_end(const_table); ++con_t_iter)
    {
        if (kh_exist(const_table, con_t_iter))
        {
            pcre2_code **patterns = malloc(2 * sizeof(pcre2_code *));
            khash_t(field_table) *field_table = kh_value(const_table, con_t_iter);
            json_object *es = json_object_new_string(kh_key(const_table, con_t_iter));
            const char* header_str = json_object_to_json_string(es);
            
            char *ret_type = extract_message_pattern(header_str, field_table, patterns, -1, NULL);
            if(ret_type != NULL) valid_patterns++;
            json_object_put(es);
        }
    }
    printf("[+] Valid PCRE2 regex patterns successfully compiled: %d\n\n", valid_patterns);

    printf("=== [2] SEED ENRICHMENT TEST ===\n");
    char *sequence = "USER anonymous\r\n";
    khash_t(strSet) *missing = kh_init(strSet);
    int absent;
    kh_put(strSet, missing, "PASS", &absent);
    printf("[*] Sending Enrich prompt for missing [PASS] ...\n");
    char *resp = enrich_sequence(sequence, missing);
    
    // Simulate sequence escaping and formatting for writing to disk
    if (resp) {
        char *unescaped = unescape_string(resp);
        char *formatted = format_request_message(unescaped);
        printf("[+] Clean Enrich Sequence Returned:\n%s\n", formatted);
        printf("[*] This buffer will be written via write_new_seeds()\n\n");
    }

    printf("=== [3] STALL BREAKING TEST ===\n");
    char *history = "USER anonymous\\n220 server ready\\n";
    printf("[*] 发送 Stall Promt 请求下一步操作...\n");
    char *stall_resp = chat_with_llm(construct_prompt_stall("FTP", "USER anonymous\\nPASS pass\\n", history), "chat", 3, 0.5);
    
    if(stall_resp) {
        char *extracted = extract_stalled_message(stall_resp, strlen(stall_resp));
        char *formatted_stall = format_request_message(extracted);
        printf("[+] Extracted clean Stall instruction:\n%s\n", formatted_stall);
        printf("[*] This instruction will be passed into fuzz_one injection\n\n");
    }

    printf("========== SIMULATION FINISHED ==========\n");
    return 0;
}
