set(insights_vendor_includes
  "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_insights/src"
  "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_diagnostics/include"
  "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_diag_data_store/include"
  "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_diag_data_store/src/rtc_store")
set(insights_vendor_sources "")
if(CONFIG_LUNAROS_DIAGNOSTIC_INSIGHTS)
  find_program(insights_node NAMES node REQUIRED)
  execute_process(COMMAND "${insights_node}" "${CMAKE_CURRENT_LIST_DIR}/materialize-port.mjs" "${CMAKE_CURRENT_BINARY_DIR}/insights_encoder" RESULT_VARIABLE port_result)
  if(NOT port_result EQUAL 0)
    message(FATAL_ERROR "Insights private encoder materialization failed")
  endif()
  list(APPEND insights_vendor_sources
    "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_insights/src/esp_insights_encoder.c"
    "${CMAKE_CURRENT_BINARY_DIR}/insights_encoder/esp_insights_cbor_encoder.c"
    "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_diagnostics/src/esp_diagnostics_metrics.c"
    "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_diagnostics/src/esp_diagnostics_utils.c"
    "${CMAKE_CURRENT_LIST_DIR}/upstream/esp_diag_data_store/src/esp_diag_data_store.c")
endif()
