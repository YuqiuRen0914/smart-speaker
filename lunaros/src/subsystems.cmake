# Source roots are architecture boundaries, not independent IDF components.
set(lunar_subsystems kernel model app persistence network delivery device_api device_link device_web)
set(lunar_sources "")
foreach(subsystem IN LISTS lunar_subsystems)
    if(CMAKE_SCRIPT_MODE_FILE)
        file(GLOB_RECURSE subsystem_sources "${CMAKE_CURRENT_LIST_DIR}/${subsystem}/*.cpp")
    else()
        file(GLOB_RECURSE subsystem_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${subsystem}/*.cpp")
    endif()
    list(APPEND lunar_sources ${subsystem_sources})
endforeach()
# Desktop chooses its core/local/remote sources separately.
include(${CMAKE_CURRENT_LIST_DIR}/desktop/sources.cmake)
list(APPEND lunar_sources ${lunar_desktop_sources})
