cmake_minimum_required(VERSION 2.8)

project(lttng)

find_package(Boost COMPONENTS coroutine context filesystem system thread REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads)

pkg_check_modules(BABELTRACE babeltrace babeltrace-ctf REQUIRED)
pkg_check_modules(LIBEVDEV libevdev REQUIRED)
pkg_check_modules(PROCESS_CPP process-cpp REQUIRED)

include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wall -pedantic -Wextra -fvisibility=hidden")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Werror -Wall -fno-strict-aliasing -fvisibility=hidden -fvisibility-inlines-hidden -pedantic -Wextra")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

include_directories(
  ${CMAKE_SOURCE_DIR}/include

  ${Boost_INCLUDE_DIRS}
  ${BABELTRACE_INCLUDE_DIRS}
  ${LIBEVDEV_INCLUDE_DIRS}
  ${PROCESS_CPP_INCLUDE_DIRS})

file(GLOB_RECURSE LTTNG_HEADER_FILES include/*.h)

add_library(
  lttng
  ${LTTNG_HEADER_FILES}
  src/lttng.cpp
  src/ctf.cpp
)

target_link_libraries(
  lttng 

  ${Boost_LIBRARIES}
  ${BABELTRACE_LDFLAGS}
  ${LIBEVDEV_LDFLAGS}
)

add_executable(lttng-example examples/main.cpp examples/evdev.cpp)
add_executable(input-processing-example examples/evdev.cpp examples/evdev_main.cpp)
add_executable(evdev-reader examples/evdev_reader.cpp)

target_link_libraries(lttng-example ${PROCESS_CPP_LDFLAGS} lttng)
target_link_libraries(input-processing-example ${LIBEVDEV_LDFLAGS} ${PROCESS_CPP_LDFLAGS} ${CMAKE_THREAD_LIBS_INIT} lttng)
target_link_libraries(evdev-reader ${LIBEVDEV_LDFLAGS})

add_subdirectory(doc)
