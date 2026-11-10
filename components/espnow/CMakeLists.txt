file(READ "${CMAKE_CURRENT_LIST_DIR}/UPSTREAM.json" identity)
string(JSON count LENGTH "${identity}" files)
math(EXPR last "${count}-1")
foreach(index RANGE ${last})
  string(JSON file MEMBER "${identity}" files ${index})
  string(JSON expected GET "${identity}" files "${file}")
  file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/upstream/${file}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Frozen upstream source differs: ${file}")
  endif()
endforeach()
# Network compiles its private lifecycle/executor port. This bundle publishes
# official API/wire headers and their direct dependencies, never a radio owner.
idf_component_register(INCLUDE_DIRS include upstream/src/espnow/include upstream/src/utils/include upstream/src/security/include upstream/src/security/include/protocomm/security
  REQUIRES esp_wifi esp_event freertos app_update esp_partition esp_app_format)
