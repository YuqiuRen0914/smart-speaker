# The product chooses FONT; storage owns the publication format. Stage only
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
    file(GLOB_RECURSE seed_inputs CONFIGURE_DEPENDS "${data}/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${font}" "${project_dir}/lunaros/assets/fonts/LICENSE.txt" "${script}" ${seed_inputs})
    execute_process(COMMAND "${python}" "${script}" --data "${data}"
        --output "${stage}" --font "${font}" COMMAND_ERROR_IS_FATAL ANY)
    littlefs_create_partition_image(filesystem "${stage}" FLASH_IN_PROJECT)
endfunction()
