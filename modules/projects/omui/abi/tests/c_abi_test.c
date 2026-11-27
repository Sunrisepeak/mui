/* openmui.h from C: the header compiles as C, links against openmui.abi,
 * and a runtime driven by it mounts a view document, finds a node, acts on it
 * through the bus interface and reads the result. (The C++ suite,
 * abi_test.cpp, covers every function; this one proves the C side.) */
#include <stdio.h>
#include <string.h>

#include "openmui.h"

struct Buffer {
    char   data[8192];
    size_t size;
};

static void collect(void* user, const char* data, size_t size) {
    struct Buffer* b = (struct Buffer*)user;
    if (b->size + size >= sizeof b->data) size = sizeof b->data - b->size - 1;
    memcpy(b->data + b->size, data, size);
    b->size += size;
    b->data[b->size] = '\0';
}

static OmuiString str(const char* s) {
    OmuiString o;
    o.data = s;
    o.size = strlen(s);
    return o;
}

static int failures = 0;

static void expect(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "c_abi_test: failed: %s\n", what);
        ++failures;
    }
}

static OmuiResult frame(OmuiRuntime rt, double t) {
    OmuiFrameInput in;
    memset(&in, 0, sizeof in);
    in.type = OMUI_STRUCTURE_TYPE_FRAME_INPUT;
    in.time = t;
    in.width = 320;
    in.height = 200;
    in.scale = 1.0;
    return omuiFrame(rt, &in, NULL);
}

int main(void) {
    static const char* view =
        "{\"omui\": \"0.1\", \"view\": {\"signals\": {\"count\": {\"type\": \"int\"}},"
        " \"root\": {\"type\": \"column\", \"children\": ["
        "  {\"type\": \"text\", \"id\": \"label\", \"props\": {\"text\": {\"expr\": \"format('{} clicks', count)\"}}},"
        "  {\"type\": \"box\", \"id\": \"inc\", \"props\": {\"label\": \"More\"},"
        "   \"on\": {\"activate\": [{\"set\": \"count\", \"expr\": \"count + 1\"}]}}]}}}";
    OmuiRuntimeCreateInfo info;
    OmuiRuntime rt = NULL;
    OmuiNode inc = 0, label = 0;
    OmuiSignal count = 0;
    struct Buffer out;

    memset(&info, 0, sizeof info);
    info.type = OMUI_STRUCTURE_TYPE_RUNTIME_CREATE_INFO;
    info.spec_version = OMUI_SPEC_VERSION;
    info.app_id = str("org.openmui.c-test");
    info.headless = 1;
    expect(omuiCreateRuntime(&info, &rt) == OMUI_SUCCESS && rt != NULL, "create a runtime");
    expect(omuiMount(rt, str("main"), str(view)) == OMUI_SUCCESS, "mount a view document");
    expect(frame(rt, 0.0) == OMUI_SUCCESS, "a frame");
    expect(omuiFind(rt, str("main/inc"), &inc) == OMUI_SUCCESS && inc != 0, "find main/inc");
    expect(omuiFind(rt, str("main/label"), &label) == OMUI_SUCCESS, "find main/label");

    memset(&out, 0, sizeof out);
    expect(omuiAct(rt, str("{\"node\": \"main/inc\", \"action\": \"activate\"}"), collect, &out) == OMUI_SUCCESS, "activate it");
    expect(frame(rt, 1.0 / 60.0) == OMUI_SUCCESS, "another frame");

    memset(&out, 0, sizeof out);
    expect(omuiGet(rt, label, str("text"), collect, &out) == OMUI_SUCCESS && strcmp(out.data, "\"1 clicks\"") == 0, "the label follows");
    expect(omuiSignalFind(rt, str("main"), str("count"), &count) == OMUI_SUCCESS, "the document's signal");
    memset(&out, 0, sizeof out);
    expect(omuiSignalGet(rt, count, collect, &out) == OMUI_SUCCESS && strcmp(out.data, "1") == 0, "its value");

    expect(omuiFind(rt, str("main/nothing"), &inc) == OMUI_ERROR_NOT_FOUND, "nothing is not found");
    expect(omuiDestroyRuntime(rt) == OMUI_SUCCESS, "destroy it");
    if (failures == 0) printf("c_abi_test: ok\n");
    return failures == 0 ? 0 : 1;
}
