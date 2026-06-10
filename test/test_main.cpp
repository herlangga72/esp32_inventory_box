#include <unity.h>

void setUp() {}
void tearDown() {}

// --- JsonBuilder ---
extern void test_jb_empty_obj();
extern void test_jb_clear_twice();
extern void test_jb_single_string();
extern void test_jb_two_strings();
extern void test_jb_string_escape_quotes();
extern void test_jb_string_escape_backslash();
extern void test_jb_string_escape_newline_tab();
extern void test_jb_single_int();
extern void test_jb_negative_int();
extern void test_jb_zero_int();
extern void test_jb_single_float();
extern void test_jb_float_two_decimals();
extern void test_jb_bool_true();
extern void test_jb_bool_false();
extern void test_jb_empty_array();
extern void test_jb_array_of_strings();
extern void test_jb_array_of_ints();
extern void test_jb_array_of_objects();
extern void test_jb_status_response();
extern void test_jb_overflow_detection();
extern void test_jb_len_matches_strlen();

// --- JsonParser ---
extern void test_jp_single_string();
extern void test_jp_two_strings();
extern void test_jp_parse_int();
extern void test_jp_parse_negative_int();
extern void test_jp_parse_float();
extern void test_jp_parse_int_as_float();
extern void test_jp_bool_true();
extern void test_jp_bool_false();
extern void test_jp_unknown_key_ignored();
extern void test_jp_missing_field_not_found();
extern void test_jp_null_input_fails();
extern void test_jp_not_json_object();
extern void test_jp_empty_input();
extern void test_jp_missing_colon();
extern void test_jp_type_mismatch();
extern void test_jp_string_with_escapes();
extern void test_jp_mixed_fields();
extern void test_jp_null_value_skipped();

// --- Entities ---
extern void test_tool_default_ctor();
extern void test_tool_set_name();
extern void test_tool_set_name_truncation();
extern void test_tool_matches_exact();
extern void test_tool_matches_within_tolerance_above();
extern void test_tool_matches_within_tolerance_below();
extern void test_tool_matches_at_boundary();
extern void test_tool_not_matches_outside();
extern void test_tool_zero_tolerance();
extern void test_tool_size();
extern void test_user_default_ctor();
extern void test_user_set_name();
extern void test_user_set_pin();
extern void test_user_set_pin_truncation();
extern void test_user_validate_pin_correct();
extern void test_user_validate_pin_wrong();
extern void test_user_validate_pin_empty();
extern void test_user_size();

// --- Service message / SCB / pools ---
extern void test_message_size_16();
extern void test_scb_size_36();
extern void test_wifi_ap_smaller_than_sta();
extern void test_union_overlay_fits();
extern void test_pools_fit_domain();
extern void test_kernel_pool_oversized_for_safety();

int main() {
    UNITY_BEGIN();

    // JsonBuilder: 21 tests
    RUN_TEST(test_jb_empty_obj);
    RUN_TEST(test_jb_clear_twice);
    RUN_TEST(test_jb_single_string);
    RUN_TEST(test_jb_two_strings);
    RUN_TEST(test_jb_string_escape_quotes);
    RUN_TEST(test_jb_string_escape_backslash);
    RUN_TEST(test_jb_string_escape_newline_tab);
    RUN_TEST(test_jb_single_int);
    RUN_TEST(test_jb_negative_int);
    RUN_TEST(test_jb_zero_int);
    RUN_TEST(test_jb_single_float);
    RUN_TEST(test_jb_float_two_decimals);
    RUN_TEST(test_jb_bool_true);
    RUN_TEST(test_jb_bool_false);
    RUN_TEST(test_jb_empty_array);
    RUN_TEST(test_jb_array_of_strings);
    RUN_TEST(test_jb_array_of_ints);
    RUN_TEST(test_jb_array_of_objects);
    RUN_TEST(test_jb_status_response);
    RUN_TEST(test_jb_overflow_detection);
    RUN_TEST(test_jb_len_matches_strlen);

    // JsonParser: 18 tests
    RUN_TEST(test_jp_single_string);
    RUN_TEST(test_jp_two_strings);
    RUN_TEST(test_jp_parse_int);
    RUN_TEST(test_jp_parse_negative_int);
    RUN_TEST(test_jp_parse_float);
    RUN_TEST(test_jp_parse_int_as_float);
    RUN_TEST(test_jp_bool_true);
    RUN_TEST(test_jp_bool_false);
    RUN_TEST(test_jp_unknown_key_ignored);
    RUN_TEST(test_jp_missing_field_not_found);
    RUN_TEST(test_jp_null_input_fails);
    RUN_TEST(test_jp_not_json_object);
    RUN_TEST(test_jp_empty_input);
    RUN_TEST(test_jp_missing_colon);
    RUN_TEST(test_jp_type_mismatch);
    RUN_TEST(test_jp_string_with_escapes);
    RUN_TEST(test_jp_mixed_fields);
    RUN_TEST(test_jp_null_value_skipped);

    // Entities: 18 tests
    RUN_TEST(test_tool_default_ctor);
    RUN_TEST(test_tool_set_name);
    RUN_TEST(test_tool_set_name_truncation);
    RUN_TEST(test_tool_matches_exact);
    RUN_TEST(test_tool_matches_within_tolerance_above);
    RUN_TEST(test_tool_matches_within_tolerance_below);
    RUN_TEST(test_tool_matches_at_boundary);
    RUN_TEST(test_tool_not_matches_outside);
    RUN_TEST(test_tool_zero_tolerance);
    RUN_TEST(test_tool_size);
    RUN_TEST(test_user_default_ctor);
    RUN_TEST(test_user_set_name);
    RUN_TEST(test_user_set_pin);
    RUN_TEST(test_user_set_pin_truncation);
    RUN_TEST(test_user_validate_pin_correct);
    RUN_TEST(test_user_validate_pin_wrong);
    RUN_TEST(test_user_validate_pin_empty);
    RUN_TEST(test_user_size);

    // Service message / layout: 6 tests
    RUN_TEST(test_message_size_16);
    RUN_TEST(test_scb_size_36);
    RUN_TEST(test_wifi_ap_smaller_than_sta);
    RUN_TEST(test_union_overlay_fits);
    RUN_TEST(test_pools_fit_domain);
    RUN_TEST(test_kernel_pool_oversized_for_safety);

    return UNITY_END();
}
