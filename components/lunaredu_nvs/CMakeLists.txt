idf_component_register(SRCS "src/initialize.cpp" INCLUDE_DIRS "include" PRIV_REQUIRES nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
