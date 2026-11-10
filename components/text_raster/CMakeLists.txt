include("${CMAKE_CURRENT_LIST_DIR}/vendor/stb_truetype/verify.cmake")
idf_component_register(SRCS "src/text_raster.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "vendor/stb_truetype/upstream")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
