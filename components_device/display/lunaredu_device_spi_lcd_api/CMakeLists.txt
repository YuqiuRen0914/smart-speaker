set(LUNAREDU_DEVICE_COMPONENT_KIND adapter)
set(api_sources src/disabled.cpp)
if(CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD)
  set(api_sources src/projection.cpp)
endif()
idf_component_register(SRCS ${api_sources} INCLUDE_DIRS "include"
  PRIV_REQUIRES lunaredu_device_spi_lcd device_api core log bblanchon__arduinojson)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
