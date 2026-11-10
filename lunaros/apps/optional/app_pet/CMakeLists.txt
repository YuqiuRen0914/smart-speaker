idf_component_register(
    SRCS "src/frame_region.cpp"
        "src/app/capabilities.cpp"
        "src/character/project.cpp"
        "src/character/expression_config.cpp"
        "src/character/project_validation.cpp"
        "src/device_api/character_project_projection.cpp"
        "src/app/setup_session.cpp"
        "src/app/pairing_guidance.cpp"
        "src/app/snapshot.cpp"
        "src/app/presentation_projector.cpp"
        "src/app/visual_environment.cpp"
        "src/app/touch_input.cpp"
        "src/app/ui_session.cpp"
        "src/app/ui_renderer.cpp"
        "src/app/ui_owner.cpp"
        "src/behavior/behavior_state.cpp"
        "src/behavior/affect_model.cpp"
        "src/behavior/appearance_policy.cpp"
        "src/behavior/emotion.cpp"

        "src/runtime/runtime.cpp"
        "src/runtime/service.cpp"
        "src/runtime/service_lifecycle.cpp"
        "src/state/pet_state.cpp"
        "src/state/pet_preferences.cpp"
        "src/app/foreground.cpp"
        "src/app/foreground_work.cpp"
        "src/app/desktop_surface.cpp"
        "src/app/connection_projection.cpp"
        "src/device_api/device_projection.cpp"
        "src/device_api/presentation_telemetry.cpp"
        "src/render/sensor_2d.cpp"
        "src/render/render_2d.cpp"
        "src/render/animator.cpp"
        "src/render/pose.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES application device_api display text_raster qr desktop
        bblanchon__arduinojson
        core
    PRIV_INCLUDE_DIRS
        "src"
        "src/render"
    PRIV_REQUIRES device_api_application configuration service_platform_link service_network service_storage service_resource digest

        esp_timer
        freertos
        log
        heap
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

# Optimize the integer UI  path which runs on the foreground owner.
set_source_files_properties("src/app/ui_renderer.cpp" "src/app/desktop_surface.cpp" PROPERTIES
    COMPILE_OPTIONS "-O2")

# Products with sufficient flash may favor CPU  throughput while keeping
# the application's lifecycle and behavior code under their normal size policy.
# Preserve strict floating-point semantics and the existing pixel contract.
if(LUNAROS_PET_RASTER_SPEED)
    set_source_files_properties("src/render/render_2d.cpp" PROPERTIES
        COMPILE_OPTIONS "-O3;-fno-lto")
endif()
idf_build_get_property(pet_build_components BUILD_COMPONENTS)
if("service_factory" IN_LIST pet_build_components)
    idf_component_optional_requires(PRIVATE service_factory)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_FACTORY_SERVICE_COMPONENT=1)
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>"
    -Wno-shadow -Wno-conversion
)

if("service_provisioning" IN_LIST pet_build_components)
    idf_component_optional_requires(PRIVATE service_provisioning)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_PET_PROVISIONING_COMPONENT=1)
endif()

# Product opt-in: bounded Display-owned GDMA clearing, no new render task.
if(LUNAROS_PET_DMA_CLEAR)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_PET_DMA_CLEAR=1)
endif()
