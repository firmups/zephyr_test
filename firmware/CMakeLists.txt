include(cmake/warn_all.cmake)

cmake_minimum_required(VERSION 3.20.0)

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
else()
  target_sources(app PRIVATE 
    src/application/stm32h573i/firmware_updater.c
    src/driver/stm32h573i/led.c
    src/driver/stm32h573i/button.c)
endif()