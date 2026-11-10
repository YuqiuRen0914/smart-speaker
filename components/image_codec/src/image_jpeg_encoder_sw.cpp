#include <stddef.h>
#include <sdkconfig.h>
#include <algorithm>
#include <climits>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "image_input_validation.h"

#include <esp_heap_caps.h>
#ifdef ESP_PLATFORM
#include <esp_memory_utils.h>
#endif
#include <esp_jpeg_common.h>
#include <esp_jpeg_enc.h>
#include <esp_log.h>
#include <esp_imgfx_color_convert.h>

#include "image_jpeg_encoder_internal.h"

namespace {

constexpr const char *kLogTag = "encode_image_to_jpeg";

static void *malloc_psram(size_t size) {
  return heap_caps_malloc_prefer(size,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,
                                MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}

struct EncoderState {
  uint8_t *input=nullptr,*output=nullptr;
  size_t input_capacity=0,output_capacity=0;
  jpeg_enc_handle_t encoder=nullptr;
  esp_imgfx_color_convert_handle_t converter=nullptr;
  uint16_t width=0,height=0,convert_width=0,convert_height=0;
  jpeg_pixel_format_t format=JPEG_PIXEL_FORMAT_RGB888;
  esp_imgfx_pixel_fmt_t convert_format=ESP_IMGFX_PIXEL_FMT_RGB888;
  uint8_t quality=0;
  ~EncoderState(){reset();}
  void reset(){
    if(encoder)jpeg_enc_close(encoder);
    encoder=nullptr;
    if(converter)esp_imgfx_color_convert_close(converter);
    converter=nullptr;
    jpeg_free_align(input);input=nullptr;input_capacity=0;
    free(output);output=nullptr;output_capacity=0;
  }
  uint8_t *input_buffer(size_t bytes){
    if(input_capacity>=bytes)return input;
    auto *next=static_cast<uint8_t *>(jpeg_calloc_align(bytes,16));
    if(!next)return nullptr;
    jpeg_free_align(input);input=next;input_capacity=bytes;return input;
  }
  void trim(){
    bool external=true;
#ifdef ESP_PLATFORM
    external=(!input||esp_ptr_external_ram(input))&&(!output||esp_ptr_external_ram(output));
#endif
    if(!external||input_capacity>150*1024||output_capacity>128*1024)reset();
  }
};
static uint8_t *convert_input_to_encoder_buf(const uint8_t *src,
                                             uint16_t width, uint16_t height,
                                             ImagePixelFormat format,
                                             jpeg_pixel_format_t *out_fmt,
                                             int *out_size,EncoderState &workspace) {
  if (format == ImagePixelFormat::Gray8) {
    const int sz = static_cast<int>(width) * static_cast<int>(height);
    uint8_t *buf = workspace.input_buffer(size_t(sz));
    if (buf == nullptr) {
      return nullptr;
    }
    memcpy(buf, src, sz);
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_PIXEL_FORMAT_GRAY;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  if (format == ImagePixelFormat::Yuyv) {
    const int sz = static_cast<int>(width) * static_cast<int>(height) * 2;
    uint8_t *buf = workspace.input_buffer(size_t(sz));
    if (buf == nullptr) {
      return nullptr;
    }
    memcpy(buf, src, sz);
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_PIXEL_FORMAT_YCbYCr;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  if (format == ImagePixelFormat::Uyvy) {
    const int sz = static_cast<int>(width) * static_cast<int>(height) * 2;
    const uint8_t *s = src;
    uint8_t *buf = workspace.input_buffer(size_t(sz));
    if (buf == nullptr) {
      return nullptr;
    }
    uint8_t *d = buf;
    for (int i = 0; i < sz; i += 4) {
      d[0] = s[1];
      d[1] = s[0];
      d[2] = s[3];
      d[3] = s[2];
      s += 4;
      d += 4;
    }
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_PIXEL_FORMAT_YCbYCr;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  if (format == ImagePixelFormat::Yuv422Planar ||
      format == ImagePixelFormat::Yuv420Planar) {
    const int chroma_height = format == ImagePixelFormat::Yuv420Planar
                                  ? height / 2 : height;
    const int sz = static_cast<int>(width) * static_cast<int>(height) * 2;
    const uint8_t *y_plane = src;
    const uint8_t *u_plane = y_plane + static_cast<int>(width) * static_cast<int>(height);
    const uint8_t *v_plane = u_plane + (static_cast<int>(width) / 2) * chroma_height;
    uint8_t *buf = workspace.input_buffer(size_t(sz));
    if (buf == nullptr) {
      return nullptr;
    }
    uint8_t *dst = buf;
    for (int y = 0; y < height; ++y) {
      const uint8_t *y_row = y_plane + y * static_cast<int>(width);
      const int chroma_y = format == ImagePixelFormat::Yuv420Planar ? y / 2 : y;
      const uint8_t *u_row = u_plane + chroma_y * (static_cast<int>(width) / 2);
      const uint8_t *v_row = v_plane + chroma_y * (static_cast<int>(width) / 2);
      for (int x = 0; x < width; x += 2) {
        const uint8_t y0 = y_row[x + 0];
        const uint8_t y1 = y_row[x + 1];
        const uint8_t cb = u_row[x / 2];
        const uint8_t cr = v_row[x / 2];
        dst[0] = y0;
        dst[1] = cb;
        dst[2] = y1;
        dst[3] = cr;
        dst += 4;
      }
    }
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_PIXEL_FORMAT_YCbYCr;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  if (format == ImagePixelFormat::Rgb24 ||
      format == ImagePixelFormat::Rgb565 ||
      format == ImagePixelFormat::Rgb565ByteSwapped) {
    esp_imgfx_pixel_fmt_t in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB888;
    uint32_t src_len = 0;
    switch (format) {
    case ImagePixelFormat::Rgb24:
      in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB888;
      src_len = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 3u;
      break;
    case ImagePixelFormat::Rgb565:
      in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
      src_len = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 2u;
      break;
    case ImagePixelFormat::Rgb565ByteSwapped:
      in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_BE;
      src_len = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 2u;
      break;
    default:
      ESP_LOGE(kLogTag, "unsupported format: 0x%08lx",
               static_cast<unsigned long>(format));
      return nullptr;
    }

    const int sz = static_cast<int>(width) * static_cast<int>(height) * 2;
    uint8_t *buf = workspace.input_buffer(size_t(sz));
    if (buf == nullptr) {
      return nullptr;
    }

    esp_imgfx_color_convert_cfg_t convert_cfg = {
        .in_res = {.width = static_cast<int16_t>(width),
                   .height = static_cast<int16_t>(height)},
        .in_pixel_fmt = in_pixel_fmt,
        .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_YUYV,
        .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
    };
    if(workspace.converter&&(workspace.convert_width!=width||workspace.convert_height!=height||workspace.convert_format!=in_pixel_fmt)){
      esp_imgfx_color_convert_close(workspace.converter);workspace.converter=nullptr;
    }
    esp_imgfx_err_t err=ESP_IMGFX_ERR_OK;
    if(!workspace.converter)err=esp_imgfx_color_convert_open(&convert_cfg,&workspace.converter);
    auto convert_handle=workspace.converter;
    workspace.convert_width=width;workspace.convert_height=height;workspace.convert_format=in_pixel_fmt;
    if (err != ESP_IMGFX_ERR_OK || convert_handle == nullptr) {
      ESP_LOGE(kLogTag, "esp_imgfx_color_convert_open failed");
      return nullptr;
    }

    esp_imgfx_data_t convert_input_data = {
        .data = const_cast<uint8_t *>(src),
        .data_len = src_len,
    };
    esp_imgfx_data_t convert_output_data = {
        .data = buf,
        .data_len = static_cast<uint32_t>(sz),
    };
    err = esp_imgfx_color_convert_process(convert_handle, &convert_input_data,
                                          &convert_output_data);
    if (err != ESP_IMGFX_ERR_OK) {
      ESP_LOGE(kLogTag, "esp_imgfx_color_convert_process failed");
      return nullptr;
    }

    if (out_fmt != nullptr) {
      *out_fmt = JPEG_PIXEL_FORMAT_YCbYCr;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  ESP_LOGE(kLogTag, "unsupported format: 0x%08lx",
           static_cast<unsigned long>(format));
  if (out_size != nullptr) {
    *out_size = 0;
  }
  return nullptr;
}

static bool encode_with_esp_new_jpeg(const uint8_t *src, size_t src_len,
                                     uint16_t width, uint16_t height,
                                     ImagePixelFormat format, uint8_t quality,
                                     uint8_t **jpg_out, size_t *jpg_out_len,
                                     jpeg_write_cb cb, void *cb_arg,EncoderState &workspace) {
  (void)src_len;
  if (quality < 1) {
    quality = 1;
  } else if (quality > 100) {
    quality = 100;
  }

  jpeg_pixel_format_t enc_src_type = JPEG_PIXEL_FORMAT_RGB888;
  int enc_in_size = 0;
  uint8_t *enc_in = convert_input_to_encoder_buf(src, width, height, format,
                                                 &enc_src_type, &enc_in_size,workspace);
  if (enc_in == nullptr) {
    ESP_LOGE(kLogTag, "alloc/convert input failed");
    return false;
  }

  jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
  cfg.width = width;
  cfg.height = height;
  cfg.src_type = enc_src_type;
  cfg.subsampling =
      (enc_src_type == JPEG_PIXEL_FORMAT_GRAY) ? JPEG_SUBSAMPLE_GRAY
                                               : JPEG_SUBSAMPLE_420;
  cfg.quality = quality;
  cfg.rotate = JPEG_ROTATE_0D;
  cfg.task_enable = false;

  if(workspace.encoder&&(workspace.width!=width||workspace.height!=height||workspace.format!=enc_src_type||workspace.quality!=quality)){
    jpeg_enc_close(workspace.encoder);workspace.encoder=nullptr;
  }
  jpeg_error_t ret=JPEG_ERR_OK;
  if(!workspace.encoder)ret=jpeg_enc_open(&cfg,&workspace.encoder);
  workspace.width=width;workspace.height=height;workspace.format=enc_src_type;workspace.quality=quality;
  if(ret!=JPEG_ERR_OK||!workspace.encoder){workspace.reset();return false;}
  auto &h=workspace.encoder;
  const size_t pixels = static_cast<size_t>(width) * height;
  const size_t max_cap = std::min(static_cast<size_t>(INT_MAX), pixels * 16 + 4096);
  size_t out_cap=std::min(max_cap,std::max(workspace.output_capacity,std::max(size_t{2048},pixels*3/2+1024)));
  auto *&outbuf=workspace.output;
  int out_len = 0;
  for (;;) {
    if(workspace.output_capacity<out_cap){
      auto *next=static_cast<uint8_t *>(malloc_psram(out_cap));
      if(next){free(outbuf);outbuf=next;workspace.output_capacity=out_cap;}
      else {ret=JPEG_ERR_NO_MEM;break;}
    }
    if (outbuf == nullptr) { ret = JPEG_ERR_NO_MEM; break; }
    ret = jpeg_enc_process(h, enc_in, enc_in_size, outbuf, static_cast<int>(out_cap), &out_len);
    if (ret != JPEG_ERR_NO_MEM || out_cap >= max_cap) break;
    free(outbuf);
    outbuf=nullptr;workspace.output_capacity=0;
    out_cap = std::min(max_cap, out_cap * 2);
    jpeg_enc_close(h);
    h = nullptr;
    ret = jpeg_enc_open(&cfg, &h);
    if (ret != JPEG_ERR_OK) break;
  }


  if (ret != JPEG_ERR_OK || out_len <= 0 || static_cast<size_t>(out_len) > out_cap) {
    workspace.reset();
    ESP_LOGE(kLogTag, "jpeg_enc_process failed: %d", static_cast<int>(ret));
    return false;
  }

  if (cb != nullptr) {
    const bool complete = cb(cb_arg, 0, outbuf, static_cast<size_t>(out_len)) == static_cast<size_t>(out_len);
    const bool ended = complete && cb(cb_arg, static_cast<size_t>(out_len), nullptr, 0) == 0;
    workspace.trim();
    if (jpg_out != nullptr) {
      *jpg_out = nullptr;
    }
    if (jpg_out_len != nullptr) {
      *jpg_out_len = 0;
    }
    return ended;
  }

  if (jpg_out != nullptr && jpg_out_len != nullptr) {
    if (void *compact = realloc(outbuf, static_cast<size_t>(out_len))) {
      outbuf = static_cast<uint8_t *>(compact);
    }
    *jpg_out = outbuf;
    *jpg_out_len=static_cast<size_t>(out_len);outbuf=nullptr;workspace.output_capacity=0;
    return true;
  }

  workspace.trim();
  return true;
}

} // namespace

bool encode_jpeg_with_software_backend(const uint8_t *src, size_t src_len,
                                      uint16_t width, uint16_t height,
                                      ImagePixelFormat format, uint8_t quality,
                                      uint8_t **jpg_out, size_t *jpg_out_len,
                                      jpeg_write_cb cb, void *cb_arg) {
  EncoderState workspace;
  return encode_with_esp_new_jpeg(src,src_len,width,height,format,quality,
                                jpg_out,jpg_out_len,cb,cb_arg,workspace);
}



bool JpegEncoderWorkspace::encode(const uint8_t *src,size_t bytes,uint16_t width,uint16_t height,ImagePixelFormat format,uint8_t quality,jpeg_write_cb cb,void *arg){
  if(borrowed_||!cb||!valid_image_input(src,bytes,width,height,format))return false;
  borrowed_=true;
  struct Return {JpegEncoderWorkspace &owner;bool &borrowed,&retiring;
    ~Return(){borrowed=false;if(retiring){retiring=false;owner.retire();}}
  } returned{*this,borrowed_,retiring_};
  if(format==ImagePixelFormat::Jpeg){
#if CONFIG_LUNAROS_IMAGE_CODEC_ALLOW_JPEG_INPUT
    return cb(arg,0,src,bytes)==bytes&&cb(arg,bytes,nullptr,0)==0;
#else
    return false;
#endif
  }
  if(!state_){auto *storage=malloc_psram(sizeof(EncoderState));if(!storage)return false;state_=new(storage) EncoderState;}
  auto &workspace=*static_cast<EncoderState *>(state_);
  const bool result=encode_with_esp_new_jpeg(src,bytes,width,height,format,quality,nullptr,nullptr,cb,arg,workspace);
  if(!result)workspace.reset();else workspace.trim();
  return result;
}
void JpegEncoderWorkspace::retire(){
  if(borrowed_){retiring_=true;return;}
  if(state_){static_cast<EncoderState *>(state_)->~EncoderState();free(state_);state_=nullptr;}
}
