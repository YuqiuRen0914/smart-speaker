include(${CMAKE_CURRENT_LIST_DIR}/../../source_ownership.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../tools/cmake/lunaros_components.cmake)
get_filename_component(lunaros_repo_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
lunaros_collect_builtin_services("${lunaros_repo_root}" lunaros_builtin_services)
set(lunaros_public_services service_local_web service_resource service_network service_ota)
set(lunaros_private_services ${lunaros_builtin_services})
list(REMOVE_ITEM lunaros_private_services ${lunaros_public_services} service_applications)
# Generic Device/service composition. Desktop and execution engines are selected
# independently by the consuming product; registration never creates a VM.
set(runtime_sources ${lunaros_runtime_sources})
idf_component_register(
    SRCS ${runtime_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES application device_api device_api_application service_applications core bblanchon__arduinojson esp_timer ${lunaros_public_services}
    PRIV_REQUIRES device_api_configuration configuration ${lunaros_private_services}
        freertos log app_update esp_app_format esp_hw_support esp_system
        heap bsp
)
idf_build_get_property(lunaros_build_components BUILD_COMPONENTS)
foreach(engine wasm python)
  if("service_${engine}" IN_LIST lunaros_build_components)
    idf_component_optional_requires(PRIVATE "service_${engine}")
    string(TOUPPER "${engine}" engine_definition)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "LUNAROS_${engine_definition}_SERVICE=1")
  endif()
endforeach()
if("service_diagnostics" IN_LIST lunaros_build_components)
  idf_component_optional_requires(PRIVATE service_diagnostics)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_DIAGNOSTICS_COMPONENT=1)
endif()
if("service_provisioning" IN_LIST lunaros_build_components)
  idf_component_optional_requires(PRIVATE service_provisioning)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_PROVISIONING_COMPONENT=1)
endif()
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
