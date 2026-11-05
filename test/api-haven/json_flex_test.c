#include "net/json.h"
#include <stdio.h>
#include <string.h>

static int sFail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); sFail++; } } while (0)

/* Checks flexible JSON path reads and bounded writer output, including overflow and parse round-trip. */
int main(void) {
    // --- path + defaults over a mixed doc ---
    const char *text = "{\"a\":{\"b.sh\":[{\"c\":\"hi\"},42]},\"t\":true,\"n\":3.5}";
    JsonNode nodes[64];
    char scratch[256];
    JsonDoc doc;
    CHECK(Json_parse(&doc, nodes, 64, scratch, sizeof(scratch), text));
    JsonRef root = Json_root(&doc);
    CHECK(Json_has(&doc, root, "a"));
    CHECK(!Json_has(&doc, root, "zzz"));
    JsonRef c = Json_path(&doc, root, "a.b.sh[0].c");
    JsonStringView value = Json_getString(&doc, c);
    CHECK(value.data && value.length == 2u && memcmp(value.data, "hi", value.length) == 0);
    CHECK(Json_getNumber(&doc, Json_path(&doc, root, "a.b.sh[1]"), -1.0) == 42.0);
    CHECK(Json_getBool(&doc, Json_path(&doc, root, "t"), false) == true);
    CHECK(Json_getBool(&doc, Json_path(&doc, root, "missing"), true) == true);
    CHECK(Json_path(&doc, root, "a.nope") < 0);
    CHECK(Json_path(&doc, root, "a.b.sh[9]") < 0);
    CHECK(Json_getString(&doc, Json_path(&doc, root, "t")).data == nullptr);

    // --- builder: {"method":"harness_run","params":{"engine":"codex","timeout":250},"tags":["a","b.sh"],"live":true,"drop":null} ---
    char out[256];
    JsonWriter w = JsonWriter(out, sizeof(out));
    CHECK(Json_beginObject(&w));
    CHECK(Json_addKey(&w, "method"));
    CHECK(Json_stringVal(&w, "harness_run"));
    CHECK(Json_addKey(&w, "params"));
    CHECK(Json_beginObject(&w));
    CHECK(Json_addKey(&w, "engine"));
    CHECK(Json_stringVal(&w, "codex"));
    CHECK(Json_addKey(&w, "timeout"));
    CHECK(Json_numberVal(&w, 250));
    CHECK(Json_endObject(&w));
    CHECK(Json_addKey(&w, "tags"));
    CHECK(Json_beginArray(&w));
    CHECK(Json_stringVal(&w, "a"));
    CHECK(Json_stringVal(&w, "b.sh"));
    CHECK(Json_endArray(&w));
    CHECK(Json_addKey(&w, "live"));
    CHECK(Json_boolVal(&w, true));
    CHECK(Json_addKey(&w, "drop"));
    CHECK(Json_nullVal(&w));
    CHECK(Json_endObject(&w));
    CHECK(JsonWriter_ok(&w));
    const char *want = "{\"method\":\"harness_run\",\"params\":{\"engine\":\"codex\",\"timeout\":250},\"tags\":[\"a\",\"b.sh\"],\"live\":true,\"drop\":null}";
    CHECK(strcmp(out, want) == 0);

    // --- overflow flips ok, round-trips back through the parser ---
    char small[16];
    JsonWriter w2 = JsonWriter(small, sizeof(small));
    CHECK(Json_beginObject(&w2));
    CHECK(!Json_addKey(&w2, "way-too-long-key-name"));
    CHECK(!JsonWriter_ok(&w2));
    JsonNode n2[32];
    char s2[64];
    JsonDoc d2;
    CHECK(Json_parse(&d2, n2, 32, s2, sizeof(s2), out));
    JsonStringView engine = Json_getString(&d2, Json_path(&d2, Json_root(&d2), "params.engine"));
    CHECK(engine.data && engine.length == 5u && memcmp(engine.data, "codex", engine.length) == 0);

    if (sFail == 0) {
        printf("json_flex_test: ALL CHECKS PASSED\n");
        return 0;
    }
    printf("json_flex_test: %d FAILURES\n", sFail);
    return 1;
}
