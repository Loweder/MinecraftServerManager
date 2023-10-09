cmake_minimum_required(VERSION 3.25)
project(ServerManager)

set(CMAKE_CXX_STANDARD 23)


add_executable(ServerManager main.cpp)

find_package(libzip REQUIRED)
target_link_libraries(ServerManager PRIVATE libzip::zip)

set_target_properties(ServerManager PROPERTIES OUTPUT_NAME "mserman")