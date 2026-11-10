include("${CMAKE_CURRENT_LIST_DIR}/verify.cmake")
set(WAMR_ROOT_DIR "${vendor_directory}")
set(WAMR_BUILD_PLATFORM "esp-idf")
set(WAMR_BUILD_TARGET "XTENSA")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  # Upstream configure generates core/version.h in its own source tree. Work
  # from a build-local copy so repeated configuration keeps the verified vendor
  # sources byte-for-byte intact (including their original line endings).
  set(WAMR_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}/upstream")
  file(COPY "${vendor_directory}/" DESTINATION "${WAMR_ROOT_DIR}")
  if(NOT CONFIG_IDF_TARGET_ESP32S3)
    message(FATAL_ERROR "The initial LunarOS App runtime supports ESP32-S3")
  endif()
  set(WAMR_BUILD_INTERP 1)
  set(WAMR_BUILD_FAST_INTERP 0)
  set(WAMR_BUILD_AOT 0)
  if(CONFIG_LUNAROS_WAMR_AOT)
    set(WAMR_BUILD_AOT 1)
  endif()
  set(WAMR_BUILD_JIT 0)
  set(WAMR_BUILD_FAST_JIT 0)
  set(WAMR_BUILD_LIBC_BUILTIN 0)
  set(WAMR_BUILD_LIBC_WASI 0)
  set(WAMR_BUILD_LIB_PTHREAD 0)
  set(WAMR_BUILD_LIB_WASI_THREADS 0)
  set(WAMR_BUILD_MULTI_MODULE 0)
  set(WAMR_BUILD_SHARED_MEMORY 0)
  set(WAMR_BUILD_SIMD 0)
  set(WAMR_BUILD_REF_TYPES 0)
  set(WAMR_BUILD_GC 0)
  set(WAMR_BUILD_STRINGREF 0)
  set(WAMR_BUILD_EXTENDED_CONST_EXPR 0)
  set(WAMR_BUILD_LIME1 0)
  # LLVM static libraries retain the padded LEB encoding of table index zero.
  # Accept that MVP-compatible encoding without enabling reference-type APIs.
  set(WAMR_BUILD_CALL_INDIRECT_OVERLONG 1)
  set(WAMR_BUILD_INSTRUCTION_METERING 1)
  set(WAMR_DISABLE_HW_BOUND_CHECK 1)
  set(WAMR_DISABLE_STACK_HW_BOUND_CHECK 1)
  include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
  # This runtime has no WASI filesystem/socket imports. The upstream ESP port
  # discovers their optional implementations unconditionally.
  list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "/espidf_(file|socket)\\.c$")
  if(NOT CONFIG_LUNAROS_WAMR_AOT)
    list(FILTER WAMR_RUNTIME_LIB_SOURCE EXCLUDE REGEX "/espidf_memmap\\.c$")
    list(APPEND WAMR_RUNTIME_LIB_SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/interp_memory.c")
  endif()
  list(APPEND WAMR_RUNTIME_LIB_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/src/owner_identity.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/aot_admission.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/module_contract.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/runtime_policy.c")
  # LunarOS executes guests on a resident FreeRTOS owner. Preserve upstream
  # pthread helpers, but use the native task identity for interpreter entries.
  set_source_files_properties("${WAMR_ROOT_DIR}/core/shared/platform/esp-idf/espidf_thread.c"
    PROPERTIES COMPILE_DEFINITIONS "os_self_thread=lunaros_upstream_pthread_self")
endif()
idf_component_register(SRCS ${WAMR_RUNTIME_LIB_SOURCE}
  INCLUDE_DIRS "include" "${WAMR_ROOT_DIR}/core/iwasm/include"
  REQUIRES pthread lwip esp_timer esp_mm)
target_compile_options(${COMPONENT_LIB} PRIVATE -include sys/stat.h)
