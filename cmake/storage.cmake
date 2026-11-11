# The product chooses FONT and its wake model; storage owns the publication format. Stage only
# inside this build so concurrent checkouts never write to a shared repository.
function(lunaros_create_storage_pool data)
    cmake_parse_arguments(POOL "FONT" "" "" ${ARGN})
    if(POOL_UNPARSED_ARGUMENTS OR NOT POOL_FONT)
        message(FATAL_ERROR "Smart-speaker storage expects FONT")
    endif()
    idf_build_get_property(python PYTHON)
    idf_build_get_property(project_dir PROJECT_DIR)
    set(stage "${CMAKE_BINARY_DIR}/storage-pool-seed")
    set(font "${project_dir}/lunaros/assets/fonts/lunar-sans-sc.ttf")
    set(script "${project_dir}/tools/assets/seed-storage.py")
    # ESP-SR is already pinned by the Pet component. Publish the product's
    # selected model through the existing storage-pool owner, without adding
    # a model partition or changing the device layout.
    idf_component_get_property(sr_dir espressif__esp-sr COMPONENT_DIR)
    set(wake_model "${sr_dir}/model/wakenet_model/wn9s_nihaoxiaozhi")
    file(GLOB wake_inputs CONFIGURE_DEPENDS "${wake_model}/*")
    file(GLOB_RECURSE seed_inputs CONFIGURE_DEPENDS "${data}/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${font}" "${project_dir}/lunaros/assets/fonts/LICENSE.txt" "${script}" "${sr_dir}/LICENSE" ${seed_inputs} ${wake_inputs})
    execute_process(COMMAND "${python}" "${script}" --data "${data}"
        --output "${stage}" --font "${font}"
        --wake-model "${wake_model}" --model-license "${sr_dir}/LICENSE"
        COMMAND_ERROR_IS_FATAL ANY)
    littlefs_create_partition_image(filesystem "${stage}" FLASH_IN_PROJECT)
endfunction()
