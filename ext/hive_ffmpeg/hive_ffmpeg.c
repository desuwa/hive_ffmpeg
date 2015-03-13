#define RSTRING_NOT_MODIFIED

#include <string.h>

#include <ruby/ruby.h>
#include <ruby/encoding.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
  #define av_frame_alloc avcodec_alloc_frame
  #define av_frame_free avcodec_free_frame
#endif

#ifdef HAVE_JPEGLIB_H
  #include <jpeglib.h>
#endif

#ifdef HAVE_PNG_H
  #include <png.h>
#endif

#if defined(HAVE_JPEGLIB_H) || defined(HAVE_PNG_H)
  #include <setjmp.h>
#endif

#define HIVE_FMT_JPEG 1
#define HIVE_FMT_PNG 2

#define HIVE_ERR_IO 1
#define HIVE_ERR_OUT 2

#define HIVE_JPEG_QUALITY 90

#define STR2SYM(x) ID2SYM(rb_intern(x))

VALUE rb_mHive;
VALUE rb_cFFmpeg;

typedef struct {
  AVFormatContext *ctx;
  char *path;
  int closed;
} ff_state;

#ifdef HAVE_JPEGLIB_H
typedef struct {
  struct jpeg_error_mgr mgr;
  jmp_buf jmp;
} hive_jpeg_error_mgr;

void hive_jpeg_error_exit(j_common_ptr cinfo){
  hive_jpeg_error_mgr *err = (hive_jpeg_error_mgr*)cinfo->err;
  longjmp(err->jmp, 1);
}

static int
ff_save_jpeg(AVFrame *pFrame, int width, int height, char *path, int quality) {
  int i, linesize;

  uint8_t *data;

  struct jpeg_compress_struct cinfo;

  hive_jpeg_error_mgr err;

  JSAMPROW row_pointer[1];

  FILE *file = fopen(path, "wb");

  if (!file) {
    return HIVE_ERR_IO;
  }

  cinfo.err = jpeg_std_error(&err.mgr);
  err.mgr.error_exit = hive_jpeg_error_exit;

  if (setjmp(err.jmp)) {
    jpeg_destroy_compress(&cinfo);
    fclose(file);
    return HIVE_ERR_OUT;
  }

  jpeg_create_compress(&cinfo);

  jpeg_stdio_dest(&cinfo, file);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);

  jpeg_set_quality(&cinfo, quality, TRUE);

  jpeg_start_compress(&cinfo, TRUE);

  data = pFrame->data[0];
  linesize = pFrame->linesize[0];

  for (i = 0; i < height; ++i) {
    row_pointer[0] = data + i * linesize;
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);

  jpeg_destroy_compress(&cinfo);

  fclose(file);

  return 0;
}
#endif

#ifdef HAVE_PNG_H
static int ff_save_png(AVFrame *frame, int width, int height, char *path) {
  int i, linesize, status = 0;

  uint8_t *data = NULL;

  png_structp png = NULL;
  png_infop info = NULL;

  FILE *file = fopen(path, "wb");

  if (!file) {
    return HIVE_ERR_IO;
  }

  png = png_create_write_struct(
    PNG_LIBPNG_VER_STRING,
    NULL, NULL, NULL
  );

  if (!png) {
    status = HIVE_ERR_OUT;
    goto cleanup;
  }

  info = png_create_info_struct(png);

  if (!info) {
    status = HIVE_ERR_OUT;
    goto cleanup;
  }

  if (setjmp(png_jmpbuf(png))) {
    status = HIVE_ERR_OUT;
    goto cleanup;
  }

  png_init_io(png, file);

  png_set_IHDR(
    png,
    info,
    width, height,
    8,
    PNG_COLOR_TYPE_RGB,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT
  );

  png_write_info(png, info);

  linesize = frame->linesize[0];
  data = frame->data[0];

  for (i = 0; i < height; ++i) {
    png_write_row(png, data + i * linesize);
  }

  png_write_end(png, NULL);

  cleanup:
    png_destroy_write_struct(&png, &info);
    fclose(file);

  return status;
}
#endif

static inline void ff_check_closed(ff_state *state) {
  if (state->closed) {
    rb_raise(rb_eIOError, "FFmpeg - closed stream");
  }
}

static VALUE ff_get_media_type(enum AVMediaType media_type) {
  switch (media_type) {
    case AVMEDIA_TYPE_VIDEO:
      return STR2SYM("video");
    case AVMEDIA_TYPE_AUDIO:
      return STR2SYM("audio");
    case AVMEDIA_TYPE_DATA:
      return STR2SYM("data");
    case AVMEDIA_TYPE_SUBTITLE:
      return STR2SYM("subtitle");
    case AVMEDIA_TYPE_ATTACHMENT:
      return STR2SYM("attachment");
    default:
      return Qnil;
  }
}

static VALUE ff_get_codec_name(enum AVCodecID id) {
  const AVCodecDescriptor *cd;

  if (id == AV_CODEC_ID_NONE) {
    return Qnil;
  }

  cd = avcodec_descriptor_get(id);

  if (cd) {
    return rb_str_new2(cd->name);
  }

  return Qfalse;
}

static VALUE ff_get_stream_info(AVStream *stream) {
  VALUE hash = rb_hash_new();
  
  rb_hash_aset(hash,
    STR2SYM("index"),
    INT2FIX(stream->index)
  );

  rb_hash_aset(hash,
    STR2SYM("type"),
    ff_get_media_type(stream->codec->codec_type)
  );

  rb_hash_aset(hash,
    STR2SYM("codec"),
    ff_get_codec_name(stream->codec->codec_id)
  );

  if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
    rb_hash_aset(hash, STR2SYM("width"), INT2FIX(stream->codec->width));

    rb_hash_aset(hash, STR2SYM("height"), INT2FIX(stream->codec->height));

    rb_hash_aset(hash,
      STR2SYM("sar"),
      rb_rational_new2(
        INT2FIX(stream->sample_aspect_ratio.num),
        INT2FIX(stream->sample_aspect_ratio.den)
      )
    );
  }

  return hash;
}

static VALUE rb_ff_nb_streams(VALUE self) {
  ff_state *state;
  Data_Get_Struct(self, ff_state, state);

  ff_check_closed(state);

  return INT2FIX(state->ctx->nb_streams);
}

static VALUE rb_ff_format(VALUE self) {
  ff_state *state;
  Data_Get_Struct(self, ff_state, state);

  ff_check_closed(state);

  return rb_str_new2(state->ctx->iformat->name);
}

static VALUE rb_ff_duration(VALUE self) {
  ff_state *state;
  Data_Get_Struct(self, ff_state, state);

  ff_check_closed(state);

  if (state->ctx->duration == AV_NOPTS_VALUE) {
    return Qnil;
  }

  return rb_float_new(state->ctx->duration / (double)AV_TIME_BASE);
}

static VALUE rb_ff_streams(VALUE self) {
  VALUE ary;

  unsigned int i;

  ff_state *state;
  Data_Get_Struct(self, ff_state, state);

  ff_check_closed(state);

  ary = rb_ary_new();

  for (i = 0; i < state->ctx->nb_streams; ++i) {
    rb_ary_push(ary, ff_get_stream_info(state->ctx->streams[i]));
  }

  return ary;
}

#if defined(HAVE_JPEGLIB_H) || defined(HAVE_PNG_H)
static int ff_fmt_from_path(char *path) {
  size_t len = strlen(path);

  char *ext = strrchr(path, '.');

  if (!ext || ext == path) {
    return 0;
  }

  len = path + len - ext;

  if (len < 4) {
    return 0;
  }

  ++ext;

  if (memcmp(ext, "png", 3) == 0) {
    return HIVE_FMT_PNG;
  }

  if (memcmp(ext, "jpg", 3) == 0) {
    return HIVE_FMT_JPEG;
  }
  else if (len == 5 && memcmp(ext, "jpeg", 4) == 0) {
    return HIVE_FMT_JPEG;
  }

  return 0;
}

static VALUE rb_ff_save_frame(int argc, VALUE *argv, VALUE self) {
  VALUE opts, arg;

  ID format_arg_id;

  char *out_path = NULL;

  unsigned int i,
    max_size = 0,
    out_quality = HIVE_JPEG_QUALITY,
    out_format = 0,
    out_offset = 0,
    out_width = 0,
    out_height = 0;

  int ret = 0, got_frame = 0, stream_index;

  int64_t timestamp = 0;

  float ratio;
  AVRational sar;

  ff_state *state;

  AVFormatContext *ctx;
  AVStream *stream;
  AVCodec *codec;
  AVPacket packet;

  AVCodecContext *codec_ctx = NULL;
  struct SwsContext *sws_ctx = NULL;
  AVFrame *frame = NULL;
  AVFrame *frame_rgb = NULL;

  const char *error = NULL;

  if (argc < 1) {
    rb_raise(rb_eArgError, "FFmpeg - wrong number of arguments");
  }

  arg = argv[0];
  Check_Type(arg, T_STRING);
  out_path = StringValueCStr(arg);

  if (argc == 2) {
    opts = argv[1];

    Check_Type(opts, T_HASH);

    arg = rb_hash_lookup(opts, STR2SYM("max_size"));

    if (arg != Qnil) {
      Check_Type(arg, T_FIXNUM);
      max_size = FIX2INT(arg);
    }

    arg = rb_hash_lookup(opts, STR2SYM("offset"));

    if (arg != Qnil) {
      Check_Type(arg, T_FIXNUM);
      out_offset = FIX2INT(arg);

      if (out_offset > 100) {
        rb_raise(rb_eArgError, "FFmpeg - offset must be between 0 and 100");
      }
    }

    arg = rb_hash_lookup(opts, STR2SYM("format"));

    if (arg != Qnil) {
      Check_Type(arg, T_SYMBOL);
      format_arg_id = SYM2ID(arg);

      if (format_arg_id == rb_intern("png")) {
        out_format = HIVE_FMT_PNG;
      }
      else if (format_arg_id == rb_intern("jpg") ||
        format_arg_id == rb_intern("jpeg")) {

        out_format = HIVE_FMT_JPEG;

        arg = rb_hash_lookup(opts, STR2SYM("quality"));

        if (arg != Qnil) {
          Check_Type(arg, T_FIXNUM);
          out_quality = FIX2INT(arg);

          if (out_quality > 100) {
            rb_raise(rb_eArgError, "FFmpeg - quality must be between 0 and 100");
          }
        }
      }
      else {
        rb_raise(rb_eArgError, "FFmpeg - invalid output format");
      }
    }
  }

  if (!out_format) {
    out_format = ff_fmt_from_path(out_path);

    if (!out_format) {
      rb_raise(rb_eArgError, "FFmpeg - invalid output format");
    }
  }

  Data_Get_Struct(self, ff_state, state);

  ff_check_closed(state);

  ctx = state->ctx;

  stream = NULL;

  for (i = 0; i < ctx->nb_streams; ++i) {
    if (ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      stream_index = i;
      stream = ctx->streams[i];
      break;
    }
  }

  if (stream == NULL) {
    rb_raise(rb_eRuntimeError, "FFmpeg - no video stream");
  }

  codec_ctx = stream->codec;
  codec = avcodec_find_decoder(codec_ctx->codec_id);

  if (codec == NULL) {
    error = "FFmpeg - no suitable decoder";
    goto cleanup;
  }

  if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
    error = "FFmpeg - failed to initialize the decoder";
    goto cleanup;
  }

  out_width = codec_ctx->width;
  out_height = codec_ctx->height;

  if (!out_width || !out_height || codec_ctx->pix_fmt == AV_PIX_FMT_NONE) {
    error = "FFmpeg - invalid input file";
    goto cleanup;
  }

  sar = stream->sample_aspect_ratio;

  if (sar.num != sar.den && sar.num > 0 && sar.den > 0) {
    if (sar.num > sar.den) {
      out_width = (unsigned int)(out_width * (sar.num / (double)sar.den));
    }
    else {
      out_height = (unsigned int)(out_height * (sar.den / (double)sar.num));
    }
  }

  if (max_size > 0 && (max_size < out_width || max_size < out_height)) {
    if (out_width > out_height) {
      ratio = (float)out_height / (float)out_width;

      out_width = max_size;
      out_height = (int)(max_size * ratio);

      if (out_height == 0) {
        out_height = 1;
      }
    }
    else if (out_height > out_width) {
      ratio = (float)out_width / (float)out_height;

      out_height = max_size;
      out_width = (int)(max_size * ratio);

      if (out_width == 0) {
        out_width = 1;
      }
    }
    else {
      out_width = out_height = max_size;
    }
  }

  frame = av_frame_alloc();
  frame_rgb = av_frame_alloc();

  if (frame == NULL || frame_rgb == NULL) {
    error = "FFmpeg - av_frame_alloc failed";
    goto cleanup;
  }

  frame_rgb->format = PIX_FMT_RGB24;
  frame->width = out_width;
  frame->height = out_height;

  if (av_image_alloc(
    frame_rgb->data,
    frame_rgb->linesize,
    out_width,
    out_height,
    PIX_FMT_RGB24,
    16
  ) < 0) {
    error = "FFmpeg - av_image_alloc failed";
    goto cleanup;
  }

  sws_ctx = sws_getContext(
    codec_ctx->width,
    codec_ctx->height,
    codec_ctx->pix_fmt,
    out_width,
    out_height,
    PIX_FMT_RGB24,
    SWS_BILINEAR,
    NULL,
    NULL,
    NULL
  );

  if (sws_ctx == NULL) {
    error = "FFmpeg - scaler failed to initialize";
    goto cleanup;
  }

  if (out_offset) {
    timestamp = (int64_t)(av_rescale(
      (int64_t)((double)(ctx->duration / AV_TIME_BASE) * (out_offset / 100.0)),
      stream->time_base.den,
      stream->time_base.num
    ));
  }

  avformat_seek_file(ctx, stream_index, INT64_MIN, timestamp, INT64_MAX, 0);

  while (av_read_frame(ctx, &packet) >= 0) {
    if (packet.stream_index == stream_index) {
      avcodec_decode_video2(codec_ctx, frame, &got_frame, &packet);

      if (got_frame) {
        sws_scale(
          sws_ctx,
          (uint8_t const * const *)frame->data,
          frame->linesize,
          0,
          codec_ctx->height,
          frame_rgb->data,
          frame_rgb->linesize
        );

        av_free_packet(&packet);

	      break;
      }
    }

    av_free_packet(&packet);
  }

  if (out_format == HIVE_FMT_JPEG) {
#ifdef HAVE_JPEGLIB_H
    ret = ff_save_jpeg(frame_rgb, out_width, out_height, out_path, out_quality);
#else
    error = "FFmpeg - JPEG support is disabled";
#endif
  }
  else if (out_format == HIVE_FMT_PNG) {
#ifdef HAVE_PNG_H
    ret = ff_save_png(frame_rgb, out_width, out_height, out_path);
#else
    error = "FFmpeg - PNG support is disabled";
#endif
  }

  if (ret > 0) {
    if (ret == HIVE_ERR_IO) {
      error = "FFmpeg - couldn't open file for writing";
    }
    else if (ret == HIVE_ERR_OUT) {
      error = "FFmpeg - couldn't compress output image";
    }
  }

  cleanup:
    sws_freeContext(sws_ctx);
    if (frame_rgb->data != NULL) {
      av_freep(&frame_rgb->data[0]);
    }
    av_frame_free(&frame_rgb);
    av_frame_free(&frame);
    avcodec_close(codec_ctx);

  if (error != NULL) {
    rb_raise(rb_eRuntimeError, "%s", error);
  }

  return rb_ary_new3(2, UINT2NUM(out_width), UINT2NUM(out_height));
}
#endif

static void ff_free(ff_state *state) {
  if (state->ctx != NULL) {
    avformat_close_input(&state->ctx);
  }
  free(state->path);
  free(state);
}

static VALUE rb_ff_alloc(VALUE self) {
  AVFormatContext *ctx = avformat_alloc_context();

  ff_state *state = (ff_state*)malloc(sizeof(ff_state));
  state->ctx = ctx;
  state->path = NULL;
  state->closed = 0;

  return Data_Wrap_Struct(self, NULL, ff_free, state);
}

static VALUE rb_ff_init(VALUE self, VALUE file_arg) {
  char *path;
  ff_state *state;

  Check_Type(file_arg, T_STRING);

  Data_Get_Struct(self, ff_state, state);

  path = StringValueCStr(file_arg);

  if (avformat_open_input(&state->ctx, path, NULL, NULL) != 0) {
    state->ctx = NULL;
    rb_raise(rb_eRuntimeError, "FFmpeg - can't open file");
  }

  if (avformat_find_stream_info(state->ctx, NULL) < 0) {
    avformat_close_input(&state->ctx);
    state->ctx = NULL;
    rb_raise(rb_eRuntimeError, "FFmpeg - couldn't get stream info");
  }

  state->path = (char*)malloc(strlen(path) + 1);
  strcpy(state->path, path);

  return Qnil;
}

static VALUE rb_ff_close(VALUE self) {
  ff_state *state;
  Data_Get_Struct(self, ff_state, state);

  if (!state->closed) {
    avformat_close_input(&state->ctx);
    state->closed = 1;
  }

  return Qnil;
}

static VALUE rb_ff_is_closed(VALUE self) {
  ff_state *state;
  Data_Get_Struct(self, ff_state, state);

  return state->closed ? Qtrue : Qfalse;
}

static VALUE rb_ff_path(VALUE self) {
  ff_state *state;
  Data_Get_Struct(self, ff_state, state);

  return rb_enc_str_new(state->path, strlen(state->path), rb_utf8_encoding());
}

static VALUE rb_ff_open(int argc, VALUE *argv, VALUE klass) {
  VALUE ff = rb_class_new_instance(argc, argv, klass);

  if (rb_block_given_p()) {
    return rb_ensure(rb_yield, ff, rb_ff_close, ff);
  }

  return ff;
}

void Init_hive_ffmpeg() {
  rb_mHive = rb_define_module("Hive");
  rb_cFFmpeg = rb_define_class_under(rb_mHive, "FFmpeg", rb_cObject);

  rb_define_alloc_func(rb_cFFmpeg, rb_ff_alloc);

  rb_define_singleton_method(rb_cFFmpeg, "open", rb_ff_open, -1);

  rb_define_method(rb_cFFmpeg, "initialize", rb_ff_init, 1);
  rb_define_method(rb_cFFmpeg, "close", rb_ff_close, 0);
  rb_define_method(rb_cFFmpeg, "closed?", rb_ff_is_closed, 0);
  rb_define_method(rb_cFFmpeg, "path", rb_ff_path, 0);
  rb_define_method(rb_cFFmpeg, "format", rb_ff_format, 0);
  rb_define_method(rb_cFFmpeg, "nb_streams", rb_ff_nb_streams, 0);
  rb_define_method(rb_cFFmpeg, "streams", rb_ff_streams, 0);
  rb_define_method(rb_cFFmpeg, "duration", rb_ff_duration, 0);

#if defined(HAVE_JPEGLIB_H) || defined(HAVE_PNG_H)
  rb_define_method(rb_cFFmpeg, "save_frame", rb_ff_save_frame, -1);
#endif

  av_log_set_level(AV_LOG_QUIET);

  av_register_all();
}
