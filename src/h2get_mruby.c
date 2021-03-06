#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "h2get.h"
#include "h2get_priv.h"

#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/compile.h"
#include "mruby/data.h"
#include "mruby/error.h"
#include "mruby/string.h"

struct h2get_mruby {
    struct h2get_ctx ctx;
};

struct h2get_mruby_priority {
    struct h2get_h2_priority prio;
};

struct h2get_mruby_frame {
    struct h2get_ctx *ctx;
    struct h2get_buf payload;
    struct h2get_h2_header header;
};

static char const H2GET_MRUBY_KEY[] = "$h2get_mruby_type";
static char const H2GET_MRUBY_FRAME_KEY[] = "$h2get_mruby_frame_type";
static char const H2GET_MRUBY_PRIORITY_KEY[] = "$h2get_mruby_priority_type";

static const struct mrb_data_type h2get_mruby_type = {
    H2GET_MRUBY_KEY, mrb_free,
};

static const struct mrb_data_type h2get_mruby_frame_type = {
    H2GET_MRUBY_FRAME_KEY, mrb_free,
};

static const struct mrb_data_type h2get_mruby_priority_type = {
    H2GET_MRUBY_PRIORITY_KEY, mrb_free,
};

static struct RClass *h2get_mruby_frame;
static struct RClass *h2get_mruby_priority;

#define H2GET_MRUBY_ASSERT_ARGS(expected_argc_)                                                                        \
    do {                                                                                                               \
        mrb_value *argv;                                                                                               \
        mrb_int argc;                                                                                                  \
        int iargc;                                                                                                     \
        mrb_get_args(mrb, "*", &argv, &argc);                                                                          \
        iargc = (int)argc;                                                                                             \
        if (iargc != expected_argc_) {                                                                                 \
            mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");                                             \
        }                                                                                                              \
    } while (0)

static mrb_value h2get_mruby_init(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    h2g = (struct h2get_mruby *)DATA_PTR(self);
    if (h2g) {
        mrb_free(mrb, h2g);
    }
    H2GET_MRUBY_ASSERT_ARGS(0);

    h2g = (struct h2get_mruby *)mrb_malloc(mrb, sizeof(*h2g));

    h2get_ctx_init(&h2g->ctx);

    mrb_data_init(self, h2g, &h2get_mruby_type);

    return self;
}

static mrb_value h2get_mruby_connect(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    const char *err = NULL;
    h2g = (struct h2get_mruby *)DATA_PTR(self);

    H2GET_MRUBY_ASSERT_ARGS(1);

    char *url = NULL;
    mrb_get_args(mrb, "z", &url);

    h2get_connect(&h2g->ctx, H2GET_BUFSTR(url), &err);
    if (err) {
        mrb_value exc;
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_close(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    h2g = (struct h2get_mruby *)DATA_PTR(self);

    h2get_close(&h2g->ctx);

    return mrb_nil_value();
}

static mrb_value create_frame(mrb_state *mrb, struct h2get_ctx *ctx, struct h2get_h2_header *header,
                              struct h2get_buf *payload)
{
    struct h2get_mruby_frame *h2g_frame;
    mrb_value frame;

    frame = mrb_obj_new(mrb, h2get_mruby_frame, 0, NULL);

    h2g_frame = (struct h2get_mruby_frame *)mrb_malloc(mrb, sizeof(*h2g_frame));
    h2g_frame->ctx = ctx;
    h2g_frame->header = *header;
    h2g_frame->payload = *payload;

    mrb_data_init(frame, h2g_frame, &h2get_mruby_frame_type);

    return frame;
}

static mrb_value h2get_mruby_read(mrb_state *mrb, mrb_value self)
{
    int ret;
    struct h2get_mruby *h2g;
    struct h2get_h2_header header;
    struct h2get_buf payload;
    int timeout;
    const char *err;
    mrb_value *argv;
    mrb_int argc;
    int iargc;

    mrb_get_args(mrb, "*", &argv, &argc);

    iargc = (int)argc;
    if (iargc == 0) {
        timeout = -1;
    } else {
        mrb_get_args(mrb, "i", &timeout);
    }

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    ret = h2get_read_one_frame(&h2g->ctx, &header, &payload, timeout, &err);
    if (ret < 0) {
        mrb_value exc;

        if (err == err_read_timeout) {
            return mrb_nil_value();
        }
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);

        return mrb_nil_value();
    }
    return create_frame(mrb, &h2g->ctx, &header, &payload);
}

static mrb_value h2get_mruby_send_settings(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    const char *err;
    int ret;

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    ret = h2get_send_settings(&h2g->ctx, &err);
    if (ret < 0) {
        mrb_value exc;
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_send_prefix(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    const char *err;
    int ret;

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    ret = h2get_send_prefix(&h2g->ctx, &err);
    if (ret < 0) {
        mrb_value exc;
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_send_priority(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    const char *err;
    int ret;
    mrb_int mrb_stream_id, mrb_dep_stream_id, mrb_exclusive, mrb_weight;
    uint32_t stream_id;
    struct h2get_h2_priority prio;

    mrb_get_args(mrb, "iiii", &mrb_stream_id, &mrb_dep_stream_id, &mrb_exclusive, &mrb_weight);
    stream_id = (uint32_t)mrb_stream_id;
    if (mrb_exclusive) {
        mrb_dep_stream_id |= 0x80000000;
    }
    prio.excl_dep_stream_id = (htonl(mrb_dep_stream_id));
    prio.weight = (uint8_t)mrb_weight;

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    ret = h2get_send_priority(&h2g->ctx, stream_id, &prio, &err);
    if (ret < 0) {
        mrb_value exc;
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_get(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    const char *err;
    int ret;

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    H2GET_MRUBY_ASSERT_ARGS(1);

    char *path = NULL;
    mrb_get_args(mrb, "z", &path);

    ret = h2get_get(&h2g->ctx, path, &err);
    if (ret < 0) {
        mrb_value exc;
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_getp(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    struct h2get_mruby_priority *h2p;
    const char *err;
    int ret;
    mrb_int mrb_stream_id;
    mrb_value mrb_prio;
    mrb_value exc;

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    H2GET_MRUBY_ASSERT_ARGS(3);

    char *path = NULL;
    ret = mrb_get_args(mrb, "zio", &path, &mrb_stream_id, &mrb_prio);

    h2p = mrb_data_get_ptr(mrb, mrb_prio, &h2get_mruby_priority_type);
    ret = h2get_getp(&h2g->ctx, path, (uint32_t)mrb_stream_id, h2p->prio,  &err);
    if (ret < 0) {
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_send_window_update(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    int ret;
    const char *err;
    mrb_int mrb_stream_id, mrb_increment;
    uint32_t stream_id, increment;

    mrb_get_args(mrb, "ii", &mrb_stream_id, &mrb_increment);
    stream_id = (uint32_t)mrb_stream_id;
    increment = (uint32_t)mrb_increment;

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    ret = h2get_send_windows_update(&h2g->ctx, stream_id, increment, &err);
    if (ret < 0) {
        mrb_value exc;
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_send_settings_ack(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby *h2g;
    int ret;

    h2g = (struct h2get_mruby *)DATA_PTR(self);

    ret = h2get_send_settings_ack(&h2g->ctx, 1);
    if (ret < 0) {
        mrb_value exc;
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, strerror(errno), strlen(strerror(errno)));
        mrb->exc = mrb_obj_ptr(exc);
    }

    return mrb_nil_value();
}

static mrb_value h2get_mruby_on_settings(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_frame *h2g_frame;
    struct h2get_mruby *h2g;
    mrb_value frame_mrbv;
    int ret;

    mrb_get_args(mrb, "o", &frame_mrbv);
    h2g = (struct h2get_mruby *)DATA_PTR(self);
    h2g_frame = (struct h2get_mruby_frame *)DATA_PTR(frame_mrbv);
    ret = h2get_ctx_on_peer_settings(&h2g->ctx, &h2g_frame->header, h2g_frame->payload.buf, h2g_frame->payload.len);
    if (ret < 0) {
        const char *err;
        mrb_value exc;
        err = h2get_render_error_code(-ret);
        exc = mrb_exc_new(mrb, E_RUNTIME_ERROR, err, strlen(err));
        mrb->exc = mrb_obj_ptr(exc);
    }
    return mrb_nil_value();
}

static mrb_value h2get_mruby_on_settings_ack(mrb_state *mrb, mrb_value self) { return mrb_nil_value(); }

static mrb_value h2get_mruby_kernel_sleep(mrb_state *mrb, mrb_value self)
{
    time_t beg, end;
    mrb_value *argv;
    mrb_int argc;
    int iargc;

    beg = time(0);
    mrb_get_args(mrb, "*", &argv, &argc);

    iargc = (int)argc;

    /* not implemented forever sleep (called without an argument)*/
    if (iargc == 0 || iargc >= 2) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");
    }

    if (mrb_fixnum_p(argv[0]) && mrb_fixnum(argv[0]) >= 0) {
        sleep(mrb_fixnum(argv[0]));
    } else {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "time interval must be positive");
    }
    end = time(0) - beg;

    return mrb_fixnum_value(end);
}

/* Frame */

static mrb_value h2get_mruby_frame_type_str(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_frame *h2g_frame;
    const char *ret;

    h2g_frame = (struct h2get_mruby_frame *)DATA_PTR(self);

    ret = h2get_frame_type_to_str(h2g_frame->header.type);
    return mrb_str_new(mrb, ret, strlen(ret));
}

static mrb_value h2get_mruby_frame_type_num(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_frame *h2g_frame;

    h2g_frame = (struct h2get_mruby_frame *)DATA_PTR(self);

    return mrb_fixnum_value(h2g_frame->header.type);
}

static mrb_value h2get_mruby_frame_to_s(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_frame *h2g_frame;
    char *buf = NULL;
    int ret;
    struct h2get_buf out;

    h2g_frame = (struct h2get_mruby_frame *)DATA_PTR(self);
    ret = asprintf(&buf, "%s frame <length=%zu, flags=0x%02x, stream_id=%" PRIu32 ">",
                   h2get_frame_type_to_str(h2g_frame->header.type), h2g_frame->payload.len, h2g_frame->header.flags,
                   ntohl(h2g_frame->header.stream_id << 1));
    out = H2GET_BUF(buf, ret);
    h2get_frame_get_renderer(h2g_frame->header.type)(h2g_frame->ctx, &out, &h2g_frame->header, h2g_frame->payload.buf,
                                                     h2g_frame->payload.len);

    return mrb_str_new(mrb, out.buf, out.len);
}

static mrb_value h2get_mruby_frame_flags(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_frame *h2g_frame;

    h2g_frame = mrb_data_get_ptr(mrb, self, &h2get_mruby_frame_type);
    return mrb_fixnum_value(h2g_frame->header.flags);
}

static mrb_value h2get_mruby_frame_len(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_frame *h2g_frame;

    h2g_frame = mrb_data_get_ptr(mrb, self, &h2get_mruby_frame_type);
    return mrb_fixnum_value(ntohl(h2g_frame->header.len << 8));
}

static mrb_value h2get_mruby_frame_stream_id(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_frame *h2g_frame;

    h2g_frame = mrb_data_get_ptr(mrb, self, &h2get_mruby_frame_type);
    return mrb_fixnum_value(ntohl(h2g_frame->header.stream_id << 1));
}

static mrb_value h2get_mruby_priority_init(mrb_state *mrb, mrb_value self)
{
    struct h2get_mruby_priority *h2p;
    mrb_int mrb_dep_stream_id, mrb_exclusive, mrb_weight;

    h2p = (struct h2get_mruby_priority *)DATA_PTR(self);
    if (h2p) {
        mrb_free(mrb, h2p);
    }
    H2GET_MRUBY_ASSERT_ARGS(3);

    mrb_get_args(mrb, "iii", &mrb_dep_stream_id, &mrb_exclusive, &mrb_weight);

    h2p = (struct h2get_mruby_priority *)mrb_malloc(mrb, sizeof(*h2p));
    if (mrb_exclusive) {
        mrb_dep_stream_id |= 0x80000000;
    }
    h2p->prio.excl_dep_stream_id = htonl(mrb_dep_stream_id);
    h2p->prio.weight = (uint8_t)mrb_weight;

    mrb_data_init(self, h2p, &h2get_mruby_priority_type);

    return self;
}


void run_mruby(const char *rbfile, int argc, char **argv)
{
    mrb_value ARGV;
    int i;
    mrb_state *mrb = mrb_open();

    ARGV = mrb_ary_new_capa(mrb, argc);
    for (i = 0; i < argc; i++) {
        char *utf8 = mrb_utf8_from_locale(argv[i], -1);
        if (utf8) {
            mrb_ary_push(mrb, ARGV, mrb_str_new_cstr(mrb, utf8));
            mrb_utf8_free(utf8);
        }
    }
    mrb_define_global_const(mrb, "ARGV", ARGV);
    struct RClass *h2get_mruby = mrb_define_class(mrb, "H2", mrb->object_class);
    MRB_SET_INSTANCE_TT(h2get_mruby, MRB_TT_DATA);

    h2get_mruby_frame = mrb_define_class(mrb, "H2Frame", mrb->object_class);
    MRB_SET_INSTANCE_TT(h2get_mruby_frame, MRB_TT_DATA);

    h2get_mruby_priority = mrb_define_class(mrb, "H2Priority", mrb->object_class);
    MRB_SET_INSTANCE_TT(h2get_mruby_priority, MRB_TT_DATA);

    /* H2 */
    mrb_define_method(mrb, h2get_mruby, "initialize", h2get_mruby_init, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby, "connect", h2get_mruby_connect, MRB_ARGS_ARG(1, 0));

    mrb_define_method(mrb, h2get_mruby, "send_prefix", h2get_mruby_send_prefix, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby, "send_settings", h2get_mruby_send_settings, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby, "send_settings_ack", h2get_mruby_send_settings_ack, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby, "send_priority", h2get_mruby_send_priority, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby, "send_window_update", h2get_mruby_send_window_update, MRB_ARGS_ARG(2, 0));

    mrb_define_method(mrb, h2get_mruby, "get", h2get_mruby_get, MRB_ARGS_ARG(1, 0));
    mrb_define_method(mrb, h2get_mruby, "getp", h2get_mruby_getp, MRB_ARGS_ARG(3, 0));

    mrb_define_method(mrb, h2get_mruby, "on_settings", h2get_mruby_on_settings, MRB_ARGS_ARG(1, 0));
    mrb_define_method(mrb, h2get_mruby, "on_settings_ack", h2get_mruby_on_settings_ack, MRB_ARGS_ARG(1, 0));
    mrb_define_method(mrb, h2get_mruby, "read", h2get_mruby_read, MRB_ARGS_ARG(0, 1));
    mrb_define_method(mrb, h2get_mruby, "close", h2get_mruby_close, MRB_ARGS_ARG(1, 0));

    /* Frame */
    mrb_define_method(mrb, h2get_mruby_frame, "type", h2get_mruby_frame_type_str, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby_frame, "type_num", h2get_mruby_frame_type_num, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby_frame, "to_s", h2get_mruby_frame_to_s, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby_frame, "flags", h2get_mruby_frame_flags, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby_frame, "len", h2get_mruby_frame_len, MRB_ARGS_ARG(0, 0));
    mrb_define_method(mrb, h2get_mruby_frame, "stream_id", h2get_mruby_frame_stream_id, MRB_ARGS_ARG(0, 0));

    /* Priority */
    mrb_define_method(mrb, h2get_mruby_priority, "initialize", h2get_mruby_priority_init, MRB_ARGS_ARG(3, 0));

    /* Kernel */
    mrb_define_method(mrb, mrb->kernel_module, "sleep", h2get_mruby_kernel_sleep, MRB_ARGS_ARG(1, 0));

    FILE *f = fopen(rbfile, "r");
    if (!f) {
        printf("Failed to open file `%s`: %s\n", rbfile, strerror(errno));
        exit(EXIT_FAILURE);
    }
    mrb_value obj = mrb_load_file(mrb, f);
    fclose(f);
    fflush(stdout);
    fflush(stderr);

    if (mrb->exc) {
        obj = mrb_funcall(mrb, mrb_obj_value(mrb->exc), "inspect", 0);
        fwrite(RSTRING_PTR(obj), RSTRING_LEN(obj), 1, stdout);
        putc('\n', stdout);
        exit(EXIT_FAILURE);
    }

    mrb_close(mrb);
    return;
}
