#include "lunaredu_pet_subtitle_policy.h"
#include <cassert>
#include <cstring>
namespace {
void expect_text(const char *actual, const char *expected) {
  assert(std::strcmp(actual, expected) == 0);
}
lunaros_home_assistant_state_t ready_home_state() {
  lunaros_home_assistant_state_t state{};
  state.network_connected = true;
  state.assistant_enabled = true;
  state.assistant_service_enabled = true;
  state.assistant_activation_ready = true;
  state.assistant_transport_connected = true;
  state.assistant_microphone_enabled = true;
  state.subtitles_enabled = true;
  state.full_screen_pet = true;
  state.desktop_page = true;
  state.session_state = "inactive";
  state.transcript_role = "";
  state.transcript_text = "";
  return state;
}
void verify_home_status_priority() {
  auto state = ready_home_state();
  expect_text(lunaros_home_assistant_badge_text(&state), "待机");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "说“你好小智”开始对话");

  state.network_provisioning = true;
  expect_text(lunaros_home_assistant_badge_text(&state), "配网中");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "配网热点已开启，请完成网络设置");
  state.network_provisioning = false;
  state.network_connecting = true;
  expect_text(lunaros_home_assistant_badge_text(&state), "网络连接中");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "正在连接 Wi-Fi…");
  state.network_connecting = false;
  state.network_connected = false;
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "网络未连接，请检查网络");

  state = ready_home_state();
  state.assistant_service_enabled = false;
  expect_text(lunaros_home_assistant_badge_text(&state), "服务启动中");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "小智服务正在启动…");
  state.assistant_service_enabled = true;
  state.assistant_suspended = true;
  expect_text(lunaros_home_assistant_badge_text(&state), "助手暂停");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "语音助手暂被其他应用占用");
}
void verify_conversation_and_transcript_states() {
  auto state = ready_home_state();
  state.session_state = "listening";
  expect_text(lunaros_home_assistant_subtitle_text(&state), "正在聆听…");
  state.transcript_role = "user";
  state.transcript_text = "今天天气怎么样";
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "今天天气怎么样");

  state.assistant_microphone_enabled = false;
  expect_text(lunaros_home_assistant_badge_text(&state), "麦克风已关闭");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "麦克风已关闭");
  state.assistant_microphone_enabled = true;

  state.session_state = "processing";
  state.transcript_hold = true;
  expect_text(lunaros_home_assistant_badge_text(&state), "思考中");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "今天天气怎么样");

  state.session_state = "inactive";
  state.assistant_speaking = true;
  state.transcript_role = "assistant";
  state.transcript_text = "今天晴朗，适合出门。";
  expect_text(lunaros_home_assistant_badge_text(&state), "回答中");
  expect_text(lunaros_home_assistant_subtitle_text(&state),
              "今天晴朗，适合出门。");

  assert(lunaros_interaction_should_interrupt_assistant_on_press(true));
  assert(!lunaros_interaction_should_interrupt_assistant_on_press(false));
}
}
int main() {verify_home_status_priority();verify_conversation_and_transcript_states();}
