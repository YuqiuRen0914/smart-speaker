idf_component_register(SRCS src/icons.cpp INCLUDE_DIRS include PRIV_INCLUDE_DIRS ../icons ../icons/generated)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
