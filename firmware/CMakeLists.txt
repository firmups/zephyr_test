include(cmake/warn_all.cmake)

cmake_minimum_required(VERSION 3.20.0)

# Auto-include credentials.conf if present — keep it out of git
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf)
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(firmups_device LANGUAGES C)

warn_all(app)
target_include_directories(app PRIVATE include)
target_sources(app PRIVATE src/main.c src/udpclient.c src/application/common/fatal_error.c src/driver/common/filesystem.c)

if(${BOARD} MATCHES "native")
  zephyr_compile_options(-fsanitize-undefined-trap-on-error)
  target_sources(app PRIVATE
    src/application/native/firmware_updater.c
    src/driver/native/led.c
    src/driver/native/button.c)
  target_compile_definitions(app PUBLIC NATIVE_SIM)
elseif(${BOARD} MATCHES "frdm_rw612")
  if("${CONFIG_APP_WIFI_SSID}" STREQUAL "")
    message(FATAL_ERROR "CONFIG_APP_WIFI_SSID is empty. "
      "Create firmware/credentials.conf with:\n"
      "  CONFIG_APP_WIFI_SSID=\"<ssid>\"\n"
      "  CONFIG_APP_WIFI_PSK=\"<passphrase>\"\n"
      "(file is gitignored)")
  endif()
  if("${CONFIG_APP_WIFI_PSK}" STREQUAL "")
    message(FATAL_ERROR "CONFIG_APP_WIFI_PSK is empty. "
      "Create firmware/credentials.conf with:\n"
      "  CONFIG_APP_WIFI_SSID=\"<ssid>\"\n"
      "  CONFIG_APP_WIFI_PSK=\"<passphrase>\"\n"
      "(file is gitignored)")
  endif()
  target_sources(app PRIVATE
    src/application/frdm_rw612/firmware_updater.c
    src/application/frdm_rw612/wifi_connect.c
    src/driver/frdm_rw612/led.c
    src/driver/frdm_rw612/button.c)
  target_compile_definitions(app PUBLIC FRDM_RW612)
else()
  target_sources(app PRIVATE
    src/application/stm32h573i/firmware_updater.c
    src/driver/stm32h573i/led.c
    src/driver/stm32h573i/button.c)
endif()