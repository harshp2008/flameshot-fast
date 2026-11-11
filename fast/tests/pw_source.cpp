// Test video source for a PRIVATE PipeWire daemon (never the user's one).
// Publishes a fixed BGRx test pattern as a "Video/Source" node and links
// itself to any "Stream/Input/Video" node that appears (no session manager is
// running in the private daemon). Prints "NODE_ID=<n>" once the node exists.
//   pixel(x, y) = B:x%256 G:y%256 R:0x42
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>

static int W = 320, H = 200; // optional argv: width height

struct Data
{
    pw_main_loop* loop = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook registryListener{};
    pw_stream* stream = nullptr;
    spa_hook streamListener{};
    uint32_t myNode = SPA_ID_INVALID, consumerNode = SPA_ID_INVALID;
    uint32_t myPort = SPA_ID_INVALID, consumerPort = SPA_ID_INVALID;
    bool linked = false, printed = false;
};

static void tryLink(Data* d)
{
    if (d->linked || d->myPort == SPA_ID_INVALID ||
        d->consumerPort == SPA_ID_INVALID)
        return;
    char out[16], in[16];
    snprintf(out, sizeof out, "%u", d->myPort);
    snprintf(in, sizeof in, "%u", d->consumerPort);
    pw_properties* p = pw_properties_new(
      PW_KEY_LINK_OUTPUT_PORT, out, PW_KEY_LINK_INPUT_PORT, in, nullptr);
    pw_core_create_object(d->core,
                          "link-factory",
                          PW_TYPE_INTERFACE_Link,
                          PW_VERSION_LINK,
                          &p->dict,
                          0);
    pw_properties_free(p);
    d->linked = true;
    fprintf(stderr, "pw_source: linked port %s -> %s\n", out, in);
}

static void onGlobal(void* data,
                     uint32_t id,
                     uint32_t,
                     const char* type,
                     uint32_t,
                     const spa_dict* props)
{
    auto* d = static_cast<Data*>(data);
    if (!props)
        return;
    if (!strcmp(type, PW_TYPE_INTERFACE_Node)) {
        const char* cls = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        if (cls && !strcmp(cls, "Stream/Input/Video") && id != d->myNode) {
            d->consumerNode = id; // a new consumer: link again
            d->consumerPort = SPA_ID_INVALID;
            d->linked = false;
        }
    } else if (!strcmp(type, PW_TYPE_INTERFACE_Port)) {
        const char* node = spa_dict_lookup(props, PW_KEY_NODE_ID);
        const char* dir = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
        if (!node || !dir)
            return;
        const uint32_t n = atoi(node);
        if (n == d->myNode && !strcmp(dir, "out"))
            d->myPort = id;
        if (n == d->consumerNode && !strcmp(dir, "in"))
            d->consumerPort = id;
    }
    tryLink(d);
}
static void onRemove(void* data, uint32_t id)
{
    auto* d = static_cast<Data*>(data);
    if (id == d->consumerNode) {
        d->consumerNode = d->consumerPort = SPA_ID_INVALID;
        d->linked = false;
    }
}
static const pw_registry_events registryEvents = { .version =
                                                     PW_VERSION_REGISTRY_EVENTS,
                                                   .global = onGlobal,
                                                   .global_remove = onRemove };

static void onState(void* data,
                    pw_stream_state,
                    pw_stream_state state,
                    const char* err)
{
    auto* d = static_cast<Data*>(data);
    if (state == PW_STREAM_STATE_ERROR) {
        fprintf(stderr, "pw_source: stream error %s\n", err);
        exit(2);
    }
    if (!d->printed && state >= PW_STREAM_STATE_PAUSED) {
        d->myNode = pw_stream_get_node_id(d->stream);
        printf("NODE_ID=%u\n", d->myNode);
        fflush(stdout);
        d->printed = true;
    }
}

static void onParam(void* data, uint32_t id, const spa_pod* param)
{
    auto* d = static_cast<Data*>(data);
    if (!param || id != SPA_PARAM_Format)
        return;
    uint8_t buf[512];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    const spa_pod* params[1];
    params[0] = (const spa_pod*)spa_pod_builder_add_object(
      &b,
      SPA_TYPE_OBJECT_ParamBuffers,
      SPA_PARAM_Buffers,
      SPA_PARAM_BUFFERS_buffers,
      SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
      SPA_PARAM_BUFFERS_blocks,
      SPA_POD_Int(1),
      SPA_PARAM_BUFFERS_size,
      SPA_POD_Int(W * 4 * H),
      SPA_PARAM_BUFFERS_stride,
      SPA_POD_Int(W * 4),
      SPA_PARAM_BUFFERS_dataType,
      SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr)));
    pw_stream_update_params(d->stream, params, 1);
}

static void onProcess(void* data)
{
    auto* d = static_cast<Data*>(data);
    pw_buffer* pb = pw_stream_dequeue_buffer(d->stream);
    if (!pb)
        return;
    spa_data& sd = pb->buffer->datas[0];
    if (sd.data) {
        auto* base = static_cast<uint8_t*>(sd.data);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint8_t* p = base + y * W * 4 + x * 4;
                p[0] = x % 256;
                p[1] = y % 256;
                p[2] = 0x42;
                p[3] = 0;
            }
        sd.chunk->offset = 0;
        sd.chunk->stride = W * 4;
        sd.chunk->size = W * 4 * H;
    }
    pw_stream_queue_buffer(d->stream, pb);
}
static const pw_stream_events streamEvents = { .version =
                                                 PW_VERSION_STREAM_EVENTS,
                                               .state_changed = onState,
                                               .param_changed = onParam,
                                               .process = onProcess };

int main(int argc, char** argv)
{
    pw_init(&argc, &argv);
    if (argc >= 3) {
        W = atoi(argv[1]);
        H = atoi(argv[2]);
    }
    Data d;
    d.loop = pw_main_loop_new(nullptr);
    pw_context* ctx = pw_context_new(pw_main_loop_get_loop(d.loop), nullptr, 0);
    d.core = pw_context_connect(ctx, nullptr, 0);
    if (!d.core) {
        fprintf(stderr, "pw_source: cannot connect to PipeWire\n");
        return 1;
    }
    d.registry = pw_core_get_registry(d.core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(
      d.registry, &d.registryListener, &registryEvents, &d);

    d.stream = pw_stream_new(d.core,
                             "flameshot-test-source",
                             pw_properties_new(PW_KEY_MEDIA_CLASS,
                                               "Video/Source",
                                               PW_KEY_MEDIA_TYPE,
                                               "Video",
                                               PW_KEY_MEDIA_CATEGORY,
                                               "Capture",
                                               PW_KEY_NODE_NAME,
                                               "test-source",
                                               nullptr));
    pw_stream_add_listener(d.stream, &d.streamListener, &streamEvents, &d);

    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    spa_rectangle size{ W, H };
    spa_fraction rate{ 30, 1 };
    const spa_pod* params[1];
    params[0] = (const spa_pod*)spa_pod_builder_add_object(
      &b,
      SPA_TYPE_OBJECT_Format,
      SPA_PARAM_EnumFormat,
      SPA_FORMAT_mediaType,
      SPA_POD_Id(SPA_MEDIA_TYPE_video),
      SPA_FORMAT_mediaSubtype,
      SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
      SPA_FORMAT_VIDEO_format,
      SPA_POD_Id(SPA_VIDEO_FORMAT_BGRx),
      SPA_FORMAT_VIDEO_size,
      SPA_POD_Rectangle(&size),
      SPA_FORMAT_VIDEO_framerate,
      SPA_POD_Fraction(&rate));
    if (pw_stream_connect(d.stream,
                          PW_DIRECTION_OUTPUT,
                          PW_ID_ANY,
                          PW_STREAM_FLAG_MAP_BUFFERS,
                          params,
                          1) < 0)
        return 1;
    pw_main_loop_run(d.loop);
    return 0;
}
