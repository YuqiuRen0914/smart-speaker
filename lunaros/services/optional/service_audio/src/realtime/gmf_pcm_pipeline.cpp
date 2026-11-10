#include "gmf_pcm_pipeline.h"

#include <algorithm>
#include <new>
#include "esp_audio_render.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_drc.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_caps_def.h"

namespace lunaredu::audio {
namespace {
// Private capability, deliberately distinct from the vendor ALC/EQ capabilities.
constexpr uint64_t kSpeakerPolicy = 0x004b50534e554cULL; // "LUNSPK"
constexpr auto kProcessor = static_cast<esp_audio_render_proc_type_t>(kSpeakerPolicy);
struct StageConfig { GmfPcmPipeline::Transform transform; void *context; unsigned channels; };
struct SpeakerStage { esp_gmf_element_t base{}; StageConfig config{}; };

esp_gmf_job_err_t stage_control(esp_gmf_element_handle_t, void *) {
  return ESP_GMF_JOB_ERR_OK;
}

esp_gmf_job_err_t stage_process(esp_gmf_element_handle_t handle, void *) {
  auto *stage = static_cast<SpeakerStage *>(handle);
  auto &element = stage->base;
  const size_t frame_bytes = sizeof(int16_t) * stage->config.channels;
  const size_t block_bytes = GmfPcmPipeline::kMaximumFrames * frame_bytes;
  esp_gmf_payload_t *input = nullptr, *output = nullptr;
  auto result = ESP_GMF_JOB_ERR_FAIL;
  const auto acquired = esp_gmf_port_acquire_in(element.in, &input, block_bytes, 0);
  if (acquired < ESP_GMF_IO_OK || input == nullptr) return result;
  if (input->valid_size > 0 && input->valid_size <= block_bytes &&
      input->valid_size % frame_bytes == 0 && input->buf != nullptr) {
    // Fixed capacity avoids reallocations when adjacent blocks differ in size.
    if (esp_gmf_port_acquire_out(element.out, &output, block_bytes, 0) >= ESP_GMF_IO_OK &&
        output != nullptr && output->buf != nullptr) {
      if (stage->config.transform(stage->config.context,
            reinterpret_cast<const int16_t *>(input->buf),
            reinterpret_cast<int16_t *>(output->buf), input->valid_size / frame_bytes)) {
        output->valid_size = input->valid_size;
        output->is_done = input->is_done;
        output->pts = input->pts;
        result = ESP_GMF_JOB_ERR_OK;
      } else {
        output->valid_size = 0;
      }
    }
  }
  if (output != nullptr && esp_gmf_port_release_out(element.out, output, 0) < ESP_GMF_IO_OK)
    result = ESP_GMF_JOB_ERR_FAIL;
  if (esp_gmf_port_release_in(element.in, input, 0) < ESP_GMF_IO_OK)
    result = ESP_GMF_JOB_ERR_FAIL;
  return result;
}

esp_gmf_err_t stage_destroy(esp_gmf_obj_handle_t handle) {
  esp_gmf_element_deinit(handle);
  delete static_cast<SpeakerStage *>(handle);
  return ESP_GMF_ERR_OK;
}

esp_gmf_err_t stage_caps(esp_gmf_element_handle_t handle) {
  esp_gmf_cap_t capability{};
  capability.cap_eightcc = kSpeakerPolicy;
  return esp_gmf_cap_append(&ESP_GMF_ELEMENT_GET(handle)->caps, &capability);
}

esp_gmf_err_t stage_new(void *config, esp_gmf_obj_handle_t *handle) {
  auto *stage = new (std::nothrow) SpeakerStage;
  if (stage == nullptr) return ESP_GMF_ERR_MEMORY_LACK;
  stage->config = *static_cast<StageConfig *>(config);
  stage->base.base.new_obj = stage_new;
  stage->base.base.del_obj = stage_destroy;
  stage->base.base.cfg = &stage->config;
  esp_gmf_element_cfg_t cfg{};
  const size_t block_bytes = GmfPcmPipeline::kMaximumFrames * sizeof(int16_t) * stage->config.channels;
  ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, block_bytes);
  ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, block_bytes);
  auto ret = esp_gmf_element_init(stage, &cfg);
  if (ret == ESP_GMF_ERR_OK) ret = esp_gmf_obj_set_tag(stage, "lunar_speaker_policy");
  // This capability is fixed. Populate it during construction instead of
  // triggering GMF's lazy-discovery process-global mutex on first playback.
  if (ret == ESP_GMF_ERR_OK) ret = stage_caps(stage);
  if (ret != ESP_GMF_ERR_OK) { esp_gmf_obj_delete(stage); return ret; }
  stage->base.ops.open = stage_control;
  stage->base.ops.close = stage_control;
  stage->base.ops.process = stage_process;
  *handle = stage;
  return ESP_GMF_ERR_OK;
}
} // namespace

GmfPcmPipeline::~GmfPcmPipeline() { (void)close(); }

bool GmfPcmPipeline::open(uint32_t rate, Transform transform, Writer writer, void *context,
                          unsigned input_channels, unsigned output_channels, PcmEffects effects) {
  if (!output_channels) output_channels = input_channels;
  if (!valid_pcm_effects(effects) || rate == 0 || input_channels < 1 || input_channels > 2 || output_channels < 1 ||
      output_channels > 2 || (transform && input_channels != output_channels) ||
      writer == nullptr || !close()) return false;
  esp_gmf_pool_handle_t pool = nullptr;
  if (esp_gmf_pool_init(&pool) != ESP_GMF_ERR_OK) return false;
  pool_ = pool;
  std::array<esp_audio_render_proc_type_t, 5> processors{};
  size_t processor_count = 0;
  const auto register_effect = [&](esp_gmf_element_handle_t element, esp_audio_render_proc_type_t type) {
    if(!element||processor_count>=processors.size()){if(element)esp_gmf_obj_delete(element);return false;}
    auto *base = ESP_GMF_ELEMENT_GET(element);
    if ((base->ops.load_caps && base->ops.load_caps(element) != ESP_GMF_ERR_OK) ||
        esp_gmf_pool_register_element(pool, element, nullptr) != ESP_GMF_ERR_OK) {
      esp_gmf_obj_delete(element); return false;
    }
    processors[processor_count++] = type; return true;
  };
  if(effects.low_db||effects.mid_db||effects.high_db){
    esp_ae_eq_filter_para_t bands[]{
      {ESP_AE_EQ_FILTER_LOW_SHELF,120,0.70710678f,float(effects.low_db)},
      {ESP_AE_EQ_FILTER_PEAK,1000,1.0f,float(effects.mid_db)},
      {ESP_AE_EQ_FILTER_HIGH_SHELF,std::min<uint32_t>(8000,rate*2/5),0.70710678f,float(effects.high_db)}};
    esp_ae_eq_cfg_t eq{rate,static_cast<uint8_t>(input_channels),16,3,bands};
    esp_gmf_element_handle_t element=nullptr;
    // GMF duplicates this bounded configuration during init; stack bands never
    // escape into a live stream. No second EQ state machine is implemented.
    if(esp_gmf_eq_init(&eq,&element)!=ESP_GMF_ERR_OK||
        !register_effect(element,ESP_AUDIO_RENDER_PROC_EQ)){close();return false;}
  }
  if (effects.gain_db) {
    esp_ae_alc_cfg_t effect_config{rate, static_cast<uint8_t>(input_channels), 16};
    esp_gmf_element_handle_t element = nullptr;
    if (esp_gmf_alc_init(&effect_config, &element) != ESP_GMF_ERR_OK ||
        !register_effect(element, ESP_AUDIO_RENDER_PROC_ALC)) { close(); return false; }
  }
  if (effects.compressor) {
    // Source compressor: 4:1 above -12 dB, no makeup gain. It supplements,
    // rather than replaces, the existing linked output/thermal protection.
    esp_ae_drc_curve_point points[]{{0.0f,-9.0f},{-12.0f,-12.0f},{-100.0f,-100.0f}};
    esp_ae_drc_cfg_t effect_config = DEFAULT_ESP_GMF_DRC_CONFIG();
    effect_config.sample_rate = rate; effect_config.channel = input_channels;
    effect_config.drc_para.point = points; effect_config.drc_para.point_num = 3;
    effect_config.drc_para.knee_width = 3.0f;
    esp_gmf_element_handle_t element = nullptr;
    if (esp_gmf_drc_init(&effect_config, &element) != ESP_GMF_ERR_OK ||
        !register_effect(element, static_cast<esp_audio_render_proc_type_t>(ESP_GMF_CAPS_AUDIO_DRC))) { close(); return false; }
  }
  if (effects.fade_in_ms) {
    esp_ae_fade_cfg_t effect_config = DEFAULT_ESP_GMF_FADE_CONFIG();
    effect_config.sample_rate = rate; effect_config.channel = input_channels;
    effect_config.transit_time = effects.fade_in_ms;
    esp_gmf_element_handle_t element = nullptr;
    if (esp_gmf_fade_init(&effect_config, &element) != ESP_GMF_ERR_OK ||
        !register_effect(element, ESP_AUDIO_RENDER_PROC_FADE)) { close(); return false; }
  }
  StageConfig stage_cfg{transform, context, input_channels};
  esp_gmf_obj_handle_t stage = nullptr;
  if (transform != nullptr) {
    if (stage_new(&stage_cfg, &stage) != ESP_GMF_ERR_OK) { close(); return false; }
    if (esp_gmf_pool_register_element(pool, stage, nullptr) != ESP_GMF_ERR_OK) {
      esp_gmf_obj_delete(stage); close(); return false;
    }
    processors[processor_count++] = kProcessor;
  }
  if (input_channels != output_channels) {
    esp_gmf_element_handle_t converter = nullptr;
    if (esp_gmf_ch_cvt_init(nullptr, &converter) != ESP_GMF_ERR_OK) { close(); return false; }
    if (esp_gmf_pool_register_element(pool, converter, nullptr) != ESP_GMF_ERR_OK) {
      esp_gmf_obj_delete(converter); close(); return false;
    }
  }
  if (input_channels > output_channels) {
    // Render 1.1 also selects rate conversion when total byte rate decreases,
    // even with equal sample rates. Supply its official native-rate bypass.
    esp_gmf_element_handle_t converter = nullptr;
    if (esp_gmf_rate_cvt_init(nullptr,&converter) != ESP_GMF_ERR_OK) { close(); return false; }
    if (esp_gmf_pool_register_element(pool,converter,nullptr) != ESP_GMF_ERR_OK) {
      esp_gmf_obj_delete(converter); close(); return false;
    }
  }
  input_channels_ = input_channels;
  output_channels_ = output_channels;
  writer_ = writer;
  context_ = context;
  esp_audio_render_cfg_t cfg{};
  cfg.max_stream_num = 1;
  cfg.out_writer = write_callback;
  cfg.out_ctx = this;
  cfg.out_sample_info = {rate, 16, static_cast<uint8_t>(output_channels)};
  cfg.pool = pool;
  if (esp_audio_render_create(&cfg, &render_) != ESP_AUDIO_RENDER_ERR_OK) {
    close(); return false;
  }
  esp_audio_render_stream_handle_t stream = nullptr;
  esp_audio_render_sample_info_t input_info{rate,16,static_cast<uint8_t>(input_channels)};
  if (esp_audio_render_stream_get(render_, ESP_AUDIO_RENDER_FIRST_STREAM, &stream) != ESP_AUDIO_RENDER_ERR_OK ||
      (processor_count && esp_audio_render_stream_add_proc(stream, processors.data(), processor_count) != ESP_AUDIO_RENDER_ERR_OK)) {
    close(); return false;
  }
  for (size_t i = 0; i < processor_count; ++i) {
    esp_gmf_element_handle_t element = nullptr;
    if (esp_audio_render_stream_get_element(stream, processors[i], &element) != ESP_AUDIO_RENDER_ERR_OK) { close(); return false; }
    auto *base = ESP_GMF_ELEMENT_GET(element);
    base->in_attr.data_size = base->out_attr.data_size = kMaximumFrames * input_channels * sizeof(int16_t);
    if (processors[i] == ESP_AUDIO_RENDER_PROC_ALC)
      for (unsigned channel = 0; channel < input_channels; ++channel)
        if (esp_gmf_alc_set_gain(element, channel, effects.gain_db) != ESP_GMF_ERR_OK) { close(); return false; }
  }
  if (esp_audio_render_stream_open(stream, &input_info) != ESP_AUDIO_RENDER_ERR_OK) { close(); return false; }
  stream_ = stream;
  return true;
}

GmfPcmPipeline::Result GmfPcmPipeline::write(const int16_t *samples, size_t frames) {
  if (stream_ == nullptr || frames == 0 || frames > kMaximumFrames) return Result::Failed;
  const size_t count = frames * input_channels_;
  if (samples != nullptr) std::copy_n(samples, count, input_.begin());
  else std::fill_n(input_.begin(), count, int16_t(0));
  result_ = Result::Failed;
  const auto ret = esp_audio_render_stream_write(stream_,
      reinterpret_cast<uint8_t *>(input_.data()), count * sizeof(int16_t));
  return ret == ESP_AUDIO_RENDER_ERR_OK ? result_ : Result::Failed;
}

int GmfPcmPipeline::write_callback(uint8_t *pcm, uint32_t bytes, void *context) {
  auto &self = *static_cast<GmfPcmPipeline *>(context);
  const size_t frame_bytes = self.output_channels_ * sizeof(int16_t);
  if (bytes == 0 || bytes % frame_bytes != 0 || bytes > kMaximumFrames * frame_bytes) return -1;
  self.result_ = self.writer_(self.context_, reinterpret_cast<const int16_t *>(pcm), bytes / frame_bytes);
  // Cancellation is admission control, not a GMF processing fault. Returning an
  // IO error here would permanently poison the vendor stream until reopened.
  return self.result_ == Result::Failed ? -1 : 0;
}

bool GmfPcmPipeline::close() {
  if (render_ != nullptr) {
    if (esp_audio_render_destroy(render_) != ESP_AUDIO_RENDER_ERR_OK) return false;
    render_ = stream_ = nullptr;
  }
  if (pool_ != nullptr) {
    if (esp_gmf_pool_deinit(static_cast<esp_gmf_pool_handle_t>(pool_)) != ESP_GMF_ERR_OK) return false;
    pool_ = nullptr;
  }
  writer_ = nullptr;
  context_ = nullptr;
  return true;
}
} // namespace lunaredu::audio
