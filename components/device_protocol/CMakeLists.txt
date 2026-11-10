idf_component_register(SRCS "src/device_profiles.cpp" "src/device_wire_envelope.cpp" src/device_protocol.cpp src/receiver_core.cpp
    INCLUDE_DIRS include REQUIRES digest)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
