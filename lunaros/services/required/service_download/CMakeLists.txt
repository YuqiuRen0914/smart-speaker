idf_component_register(
    SRCS "src/download_service.cpp" "src/lifecycle.cpp" "src/download_transport.cpp" "src/network_download_reader.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES core service_network log
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

