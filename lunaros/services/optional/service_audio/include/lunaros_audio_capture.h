#pragma once
#include <cstddef>
#include <cstdint>
namespace lunaredu::audio {
enum class PcmReferenceSource:uint8_t {None,SoftwareOutput,HardwareInput};
enum class PcmCaptureFailure:uint8_t {None,InputRead,ReferenceConversion,InvalidDuplex};
// Values supplied by the actual capture provider. Synchronous means mic/ref
// belong to the same ADC input batch; it does not assert zero acoustic delay.
struct PcmDuplexObservation {
  PcmReferenceSource reference_source=PcmReferenceSource::None;
  bool synchronous_reference=false,sample_clock_valid=false,monotonic_time_valid=false,reference_delay_known=false;
  uint32_t sample_rate=0;
  size_t reference_samples=0;
  uint64_t capture_generation=0,first_sample=0;
  int64_t captured_at_us=0;
  int32_t reference_delay_samples=0; // Signed; meaningful only with delay_known.
};
}
