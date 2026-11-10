idf_component_register(SRCS "src/account_resources.cpp" "src/account_resources_cache.cpp"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src" REQUIRES lunaros)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
