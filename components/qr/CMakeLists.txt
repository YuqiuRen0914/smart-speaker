include("${CMAKE_CURRENT_LIST_DIR}/vendor/qrcodegen/verify.cmake")
idf_component_register(
    SRCS "src/qr_payload.cpp" "src/qr_code.cpp" "vendor/qrcodegen/upstream/qrcodegen.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "vendor/qrcodegen/upstream"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
