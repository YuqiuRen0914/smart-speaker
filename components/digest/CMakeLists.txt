idf_component_register(SRCS "src/digest.cpp" "src/signature.cpp" "src/hmac.cpp" INCLUDE_DIRS "include" REQUIRES mbedtls esp_rom)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
