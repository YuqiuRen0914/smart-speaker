#include <mp3dec.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Helix can reject a false frame header without consuming input. The player
// retries that same header forever, flooding logs and starving device control.
// Keep this product fix outside the dependency manager's generated files.
extern "C" int __real_MP3Decode(HMP3Decoder decoder, unsigned char **input,
                                int *remaining, short *output, int use_size);
extern "C" int __wrap_MP3Decode(HMP3Decoder decoder, unsigned char **input,
                                int *remaining, short *output, int use_size) {
  unsigned char *start = input ? *input : nullptr;
  const int bytes = remaining ? *remaining : 0;
  const int result = __real_MP3Decode(decoder, input, remaining, output, use_size);
  if (result != 0) {
    if (start && bytes > 0 && *input == start && *remaining == bytes) {
      ++*input;
      --*remaining;
    }
    // Error recovery has no I2S write to yield CPU time to the UI/network tasks.
    vTaskDelay(1);
  }
  return result;
}
