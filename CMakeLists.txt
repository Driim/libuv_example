cmake_minimum_required(VERSION 3.0)
project(libuv_example)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(Threads REQUIRED)
find_package(LibUV REQUIRED)

if (LIBUV_FOUND)
    include_directories(SYSTEM ${LIBUV_INCLUDE_DIRS})
endif (LIBUV_FOUND)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")

set(SOURCE_FILES_CLIENT client.c)
set(SOURCE_FILES_SERVER echo-server.c)

add_executable(client ${SOURCE_FILES_CLIENT})
add_executable(echo-server ${SOURCE_FILES_SERVER})

target_link_libraries (echo-server ${CMAKE_THREAD_LIBS_INIT} ${LIBUV_LIBRARIES} rt m)