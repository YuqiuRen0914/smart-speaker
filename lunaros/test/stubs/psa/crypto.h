#pragma once

#include <stddef.h>
#include <stdint.h>

using psa_status_t = int;
using psa_algorithm_t = int;

inline constexpr psa_status_t PSA_SUCCESS = 0;
inline constexpr psa_algorithm_t PSA_ALG_SHA_256 = 1;
inline psa_status_t psa_crypto_init() { return PSA_SUCCESS; }

struct psa_hash_operation_t {
  uint32_t state = 0;
};

#define PSA_HASH_OPERATION_INIT psa_hash_operation_t{}

inline psa_status_t psa_hash_setup(psa_hash_operation_t *operation,
                                   psa_algorithm_t) {
  operation->state = 0;
  return PSA_SUCCESS;
}

inline psa_status_t psa_hash_update(psa_hash_operation_t *operation,
                                    const uint8_t *data, size_t size) {
  for (size_t index = 0; index < size; ++index) {
    operation->state = operation->state * 33U + data[index];
  }
  return PSA_SUCCESS;
}

inline psa_status_t psa_hash_finish(psa_hash_operation_t *operation,
                                    uint8_t *output, size_t output_size,
                                    size_t *output_length) {
  if (output_size < 32) {
    return -1;
  }
  for (size_t index = 0; index < 32; ++index) {
    output[index] = static_cast<uint8_t>(operation->state + index);
  }
  *output_length = 32;
  return PSA_SUCCESS;
}

inline psa_status_t psa_hash_abort(psa_hash_operation_t *) {
  return PSA_SUCCESS;
}

inline psa_status_t psa_hash_compute(psa_algorithm_t algorithm,
                                     const uint8_t *data, size_t size,
                                     uint8_t *output, size_t output_size,
                                     size_t *output_length) {
  psa_hash_operation_t operation{};
  psa_hash_setup(&operation, algorithm);
  psa_hash_update(&operation, data, size);
  return psa_hash_finish(&operation, output, output_size, output_length);
}
