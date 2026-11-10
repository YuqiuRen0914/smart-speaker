#include <sdkconfig.h>
#include <climits>
#include <memory>
#include "image_jpeg_decoder.h"
#if CONFIG_LUNAROS_IMAGE_CODEC_ENABLE_JPEG_DECODER
#include <esp_jpeg_dec.h>
#endif

bool JpegDecoderWorkspace::decode(const uint8_t *jpeg_data, size_t jpeg_size,
                           Rgb565ImageBuffer &image, std::string &error) {
  error.clear();
  bool complete=false;
  struct Failure {JpegDecoderWorkspace &workspace;Rgb565ImageBuffer &image;bool &complete;
    ~Failure(){if(!complete){image=Rgb565ImageBuffer();workspace.retire();}}
  } failure{*this,image,complete};
#if CONFIG_LUNAROS_IMAGE_CODEC_ENABLE_JPEG_DECODER
  if (jpeg_data == nullptr || jpeg_size == 0 || jpeg_size > INT_MAX) {
    error = "invalid_jpeg_input";
    return false;
  }
  if(!decoder_){
    jpeg_dec_config_t config=DEFAULT_JPEG_DEC_CONFIG();config.output_type=JPEG_PIXEL_FORMAT_RGB565_LE;
    if(jpeg_dec_open(&config,&decoder_)!=JPEG_ERR_OK||!decoder_){error="jpeg_decoder_create_failed";return false;}
  }
  auto decoder=decoder_;
  jpeg_dec_io_t io = {};
  io.inbuf = const_cast<uint8_t *>(jpeg_data);
  io.inbuf_len = static_cast<int>(jpeg_size);
  jpeg_dec_header_info_t header = {};
  if (jpeg_dec_parse_header(decoder, &io, &header) != JPEG_ERR_OK) {
    error = "image_dimensions_unknown";
    return false;
  }
  const uint32_t pixels = static_cast<uint32_t>(header.width) * header.height;
  if (pixels == 0 || pixels > 320u * 240u) {
    error = "image_too_large";
    return false;
  }
  int buffer_size = 0;
  if (jpeg_dec_get_outbuf_len(decoder, &buffer_size) != JPEG_ERR_OK ||
      buffer_size < static_cast<int>(pixels * 2) || buffer_size > 320 * 240 * 2) {
    error = "invalid_jpeg_output_size";
    return false;
  }
  const bool reuse=image.data.capacity()>=size_t(buffer_size)&&image.data.data()&&
    reinterpret_cast<uintptr_t>(image.data.data())%16==0;
  std::unique_ptr<uint8_t, decltype(&jpeg_free_align)> output(
      reuse?nullptr:static_cast<uint8_t *>(jpeg_calloc_align(buffer_size,16)),jpeg_free_align);
  if (!reuse&&!output) {
    error = "jpeg_output_alloc_failed";
    return false;
  }
  io.outbuf=reuse?image.data.data():output.get();
  if (jpeg_dec_process(decoder, &io) != JPEG_ERR_OK) {
    error = "jpeg_decode_failed";
    return false;
  }
  if(reuse)image.data.resize(pixels*2);
  else image.data.adopt(output.release(),pixels*2,jpeg_free_align,size_t(buffer_size));
  image.width = header.width;
  image.height = header.height;
  complete=true;return true;
#else
  (void)jpeg_data;
  (void)jpeg_size;
  error = "jpeg_decoder_not_compiled";
  return false;
#endif
}

void JpegDecoderWorkspace::retire(){
#if CONFIG_LUNAROS_IMAGE_CODEC_ENABLE_JPEG_DECODER
  if(decoder_)jpeg_dec_close(decoder_);
#endif
  decoder_=nullptr;
}
bool decode_jpeg_to_rgb565(const uint8_t *data,size_t bytes,Rgb565ImageBuffer &image,std::string &error){
  JpegDecoderWorkspace workspace;return workspace.decode(data,bytes,image,error);
}
