# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Include additional local components
set(EXTRA_COMPONENT_DIRS 
    "components/button"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_WebLedPlayer)

target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "main/certs/ca_cert.pem" TEXT)