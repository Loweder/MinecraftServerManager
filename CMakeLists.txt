cmake_minimum_required(VERSION 3.25)
set(TARGET ServerManager)
set(MSERMAN_VERSION 1)
set(VERSION 2.0.0)
project(${TARGET}
        VERSION ${VERSION}
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(SOURCES main.cpp
        jsoncpp.cpp
        utility.cpp
        verify.cpp
        bit_defines.hpp
        operation.cpp)


configure_file(version.h.in version.h)
add_executable(${TARGET} ${SOURCES})
find_package(libzip REQUIRED)
target_link_libraries(${TARGET} PRIVATE libzip::zip)

set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "mserman")