idf_component_register(
  SRCS src/local_factory.cpp src/local_factory_projection.cpp src/lifecycle.cpp src/usb_commands.cpp
  INCLUDE_DIRS include
  REQUIRES core
  PRIV_REQUIRES device_api)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
