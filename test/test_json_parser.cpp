#include <unity.h>
#include "../src/utils/JsonParser.h"
#include <cstring>

void test_jp_single_string() {
    char name[32] = {};
    JField fields[] = {{"name", JField::T_STR, name, sizeof(name)}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"name\":\"herlangga\"}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_EQUAL_STRING("herlangga", name);
}

void test_jp_two_strings() {
    char ssid[33] = {}, pass[65] = {};
    JField fields[] = {
        {"ssid", JField::T_STR, ssid, sizeof(ssid)},
        {"password", JField::T_STR, pass, sizeof(pass)},
    };
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"ssid\":\"MyWiFi\",\"password\":\"secret\"}", fields, 2, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_TRUE(fields[1].found);
    TEST_ASSERT_EQUAL_STRING("MyWiFi", ssid);
    TEST_ASSERT_EQUAL_STRING("secret", pass);
}

void test_jp_parse_int() {
    int id = 0;
    JField fields[] = {{"id", JField::T_INT, &id}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"id\":42}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_EQUAL(42, id);
}

void test_jp_parse_negative_int() {
    int val = 0;
    JField fields[] = {{"val", JField::T_INT, &val}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"val\":-99}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_EQUAL(-99, val);
}

void test_jp_parse_float() {
    float w = 0;
    JField fields[] = {{"weight", JField::T_FLT, &w}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"weight\":3.75}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.75, w);
}

void test_jp_parse_int_as_float() {
    float w = 0;
    JField fields[] = {{"weight", JField::T_FLT, &w}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"weight\":5}", fields, 1, err));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, w);
}

void test_jp_bool_true() {
    bool active = false;
    JField fields[] = {{"active", JField::T_BOOL, &active}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"active\":true}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_TRUE(active);
}

void test_jp_bool_false() {
    bool active = true;
    JField fields[] = {{"active", JField::T_BOOL, &active}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"active\":false}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_FALSE(active);
}

void test_jp_unknown_key_ignored() {
    char name[32] = {};
    JField fields[] = {{"name", JField::T_STR, name, sizeof(name)}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"unknown\":\"ignored\",\"name\":\"test\"}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_EQUAL_STRING("test", name);
}

void test_jp_missing_field_not_found() {
    char name[32] = {};
    JField fields[] = {{"name", JField::T_STR, name, sizeof(name)}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{}", fields, 1, err));
    TEST_ASSERT_FALSE(fields[0].found);
}

void test_jp_null_input_fails() {
    JField fields[1] = {};
    String err;
    TEST_ASSERT_FALSE(jsonParse(nullptr, fields, 0, err));
}

void test_jp_not_json_object() {
    JField fields[1] = {};
    String err;
    TEST_ASSERT_FALSE(jsonParse("just text", fields, 0, err));
}

void test_jp_empty_input() {
    JField fields[1] = {};
    String err;
    TEST_ASSERT_FALSE(jsonParse("", fields, 0, err));
}

void test_jp_missing_colon() {
    JField fields[] = {{"x", JField::T_INT, nullptr}};
    String err;
    TEST_ASSERT_FALSE(jsonParse("{\"x\" 5}", fields, 1, err));
}

void test_jp_type_mismatch() {
    int x = 0;
    JField fields[] = {{"x", JField::T_INT, &x}};
    String err;
    TEST_ASSERT_FALSE(jsonParse("{\"x\":\"not_a_number\"}", fields, 1, err));
}

void test_jp_string_with_escapes() {
    char text[64] = {};
    JField fields[] = {{"text", JField::T_STR, text, sizeof(text)}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"text\":\"line1\\nline2\\tindent\"}", fields, 1, err));
    TEST_ASSERT_TRUE(fields[0].found);
    TEST_ASSERT_EQUAL_STRING("line1\nline2\tindent", text);
}

void test_jp_mixed_fields() {
    char name[32] = {};
    int count = 0;
    bool on = false;
    float val = 0;
    JField fields[] = {
        {"name", JField::T_STR, name, sizeof(name)},
        {"count", JField::T_INT, &count},
        {"on", JField::T_BOOL, &on},
        {"val", JField::T_FLT, &val},
    };
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"name\":\"test\",\"count\":7,\"on\":true,\"val\":2.5}", fields, 4, err));
    TEST_ASSERT_EQUAL_STRING("test", name);
    TEST_ASSERT_EQUAL(7, count);
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.5, val);
}

void test_jp_null_value_skipped() {
    char name[32] = {};
    JField fields[] = {{"name", JField::T_STR, name, sizeof(name)}};
    String err;
    TEST_ASSERT_TRUE(jsonParse("{\"name\":null,\"other\":\"ignored\"}", fields, 1, err));
    TEST_ASSERT_FALSE(fields[0].found);
}
