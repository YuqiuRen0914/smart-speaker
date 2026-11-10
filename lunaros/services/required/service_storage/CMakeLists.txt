set(storage_root "${CMAKE_CURRENT_LIST_DIR}")
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB storage_sources "${storage_root}/src/*.cpp")
else()
    file(GLOB storage_sources CONFIGURE_DEPENDS
        "${storage_root}/src/*.cpp")
endif()

idf_component_register(
    SRCS ${storage_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES device_api freertos core bblanchon__arduinojson
    PRIV_REQUIRES configuration joltwallet__littlefs log
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
