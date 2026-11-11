cmake_minimum_required(VERSION 3.22)
project(speaker_home_preview LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(SPEAKER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${SPEAKER_ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(export_clock_font export_clock_font.cpp)
target_link_libraries(export_clock_font PRIVATE lvgl)
target_compile_options(export_clock_font PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_executable(home_preview home_preview.cpp
    "${SPEAKER_ROOT}/main/assets/home_clock_font.c"
    "${SPEAKER_ROOT}/main/smart_speaker/smart_speaker_boot.cpp"
    "${SPEAKER_ROOT}/main/smart_speaker/smart_speaker_home.cpp"
    "${SPEAKER_ROOT}/main/smart_speaker/smart_speaker_home_pet.cpp"
    "${SPEAKER_ROOT}/main/smart_speaker/smart_speaker_launcher.cpp"
    "${SPEAKER_ROOT}/main/product/weather.cpp"
    "${SPEAKER_ROOT}/test/host/product_time_storage.cpp")
target_include_directories(home_preview PRIVATE
    "${SPEAKER_ROOT}/main" "${SPEAKER_ROOT}/main/smart_speaker"
    "${SPEAKER_ROOT}/lunaros/include" "${SPEAKER_ROOT}/test"
    "${SPEAKER_ROOT}/managed_components/bblanchon__arduinojson/src")
target_link_libraries(home_preview PRIVATE lvgl)
target_compile_options(home_preview PRIVATE -Wall -Wextra -Werror -UNDEBUG)
