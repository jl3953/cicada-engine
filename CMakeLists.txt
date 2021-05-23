CMAKE_MINIMUM_REQUIRED(VERSION 3.5.1)

PROJECT(MICA C CXX)


SET(CMAKE_CXX_COMPILER g++)
SET(CMAKE_C_LINK_EXECUTABLE g++)

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
  add_definitions(-D_WIN32_WINNT=0x600)
endif()

find_package(Threads REQUIRED)

if(GRPC_AS_SUBMODULE)
  # One way to build a projects that uses gRPC is to just include the
  # entire gRPC project tree via "add_subdirectory".
  # This approach is very simple to use, but the are some potential
  # disadvantages:
  # * it includes gRPC's CMakeLists.txt directly into your build script
  #   without and that can make gRPC's internal setting interfere with your
  #   own build.
  # * depending on what's installed on your system, the contents of submodules
  #   in gRPC's third_party/* might need to be available (and there might be
  #   additional prerequisites required to build them). Consider using
  #   the gRPC_*_PROVIDER options to fine-tune the expected behavior.
  #
  # A more robust approach to add dependency on gRPC is using
  # cmake's ExternalProject_Add (see cmake_externalproject/CMakeLists.txt).

  # Include the gRPC's cmake build (normally grpc source code would live
  # in a git submodule called "third_party/grpc", but this example lives in
  # the same repository as gRPC sources, so we just look a few directories up)
  add_subdirectory(../../.. ${CMAKE_CURRENT_BINARY_DIR}/grpc EXCLUDE_FROM_ALL)
  message(STATUS "Using gRPC via add_subdirectory.")

  # After using add_subdirectory, we can now use the grpc targets directly from
  # this build.
  set(_PROTOBUF_LIBPROTOBUF libprotobuf)
  set(_REFLECTION grpc++_reflection)
  if(CMAKE_CROSSCOMPILING)
    find_program(_PROTOBUF_PROTOC protoc)
  else()
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
  endif()
  set(_GRPC_GRPCPP grpc++)
  if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
  else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)
  endif()
elseif(GRPC_FETCHCONTENT)
  # Another way is to use CMake's FetchContent module to clone gRPC at
  # configure time. This makes gRPC's source code available to your project,
  # similar to a git submodule.
  message(STATUS "Using gRPC via add_subdirectory (FetchContent).")
  include(FetchContent)
  FetchContent_Declare(
      grpc
      GIT_REPOSITORY https://github.com/grpc/grpc.git
      # when using gRPC, you will actually set this to an existing tag, such as
      # v1.25.0, v1.26.0 etc..
      # For the purpose of testing, we override the tag used to the commit
      # that's currently under test.
      GIT_TAG        vGRPC_TAG_VERSION_OF_YOUR_CHOICE)
  FetchContent_MakeAvailable(grpc)

  # Since FetchContent uses add_subdirectory under the hood, we can use
  # the grpc targets directly from this build.
  set(_PROTOBUF_LIBPROTOBUF libprotobuf)
  set(_REFLECTION grpc++_reflection)
  set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
  set(_GRPC_GRPCPP grpc++)
  if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
  else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)
  endif()
else()
  # This branch assumes that gRPC and all its dependencies are already installed
  # on this system, so they can be located by find_package().

  # Find Protobuf installation
  # Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
  set(protobuf_MODULE_COMPATIBLE TRUE)
  find_package(Protobuf CONFIG REQUIRED)
  message(STATUS "Using protobuf ${protobuf_VERSION}")

  set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
  set(_REFLECTION gRPC::grpc++_reflection)
  if(CMAKE_CROSSCOMPILING)
    find_program(_PROTOBUF_PROTOC protoc)
  else()
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
  endif()

  # Find gRPC installation
  # Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
  find_package(gRPC CONFIG REQUIRED)
  message(STATUS "Using gRPC ${gRPC_VERSION}")

  set(_GRPC_GRPCPP gRPC::grpc++)
  if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
  else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
  endif()
endif()

# Proto file
get_filename_component(hw_proto "src/mica/protos/smdbrpc.proto" ABSOLUTE)
get_filename_component(hw_proto_path "${hw_proto}" PATH)

# Generated sources
set(hw_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/smdbrpc.pb.cc")
set(hw_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/smdbrpc.pb.h")
set(hw_grpc_srcs  "${CMAKE_CURRENT_BINARY_DIR}/smdbrpc.grpc.pb.cc")
set(hw_grpc_hdrs  "${CMAKE_CURRENT_BINARY_DIR}/smdbrpc.grpc.pb.h")
add_custom_command(
    OUTPUT "${hw_proto_srcs}" "${hw_proto_hdrs}" "${hw_grpc_srcs}" "${hw_grpc_hdrs}"
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
    --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
    -I "${hw_proto_path}"
    --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
    "${hw_proto}"
    DEPENDS "${hw_proto}")

# Include generated *.pb.h files
include_directories("src/mica/protos")


OPTION(DEBUG "Enable debugging" OFF)
OPTION(LTO "Use link time optimization" OFF)

# OPTION(AVX2 "Enable AVX2" ON)



INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/src)
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)



ADD_DEFINITIONS(-std=c++14)

ADD_DEFINITIONS(-Wall -Wextra)
ADD_DEFINITIONS(-Wzero-as-null-pointer-constant)
ADD_DEFINITIONS(-Wsign-conversion)
ADD_DEFINITIONS(-Wconversion)
ADD_DEFINITIONS(-Winline)

ADD_DEFINITIONS(-Wno-unused-function)

ADD_DEFINITIONS(-march=native)
ADD_DEFINITIONS(-pthread)

ADD_DEFINITIONS(-g)



IF(DEBUG)

  MESSAGE(WARNING "Debugging is ENABLED (to disable, run `cmake .. -DDEBUG=OFF`). Performance will be low.")
  ADD_DEFINITIONS(-Og)
  # ADD_DEFINITIONS(-O0)

ELSE(DEBUG)

  MESSAGE(STATUS "Debugging is disabled (to enable, run `cmake .. -DDEBUG=ON`)")
  ADD_DEFINITIONS(-DNDEBUG)
  ADD_DEFINITIONS(-O3)
  # ADD_DEFINITIONS(--param inline-unit-growth=200 --param large-function-growth=500)

ENDIF(DEBUG)


IF(LTO)

  MESSAGE(STATUS "Link time optimization is enabled (to disable, run `cmake .. -DLTO=OFF`)")
  ADD_DEFINITIONS(-flto)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")

ELSE(LTO)

  MESSAGE(WARNING "Link time optimization is DISABLED (to enable, run `cmake .. -DLTO=ON`). Performance will be low.")

ENDIF(LTO)


# IF(AVX2)

#   MESSAGE(STATUS "AVX2 is enabled (to disable, run `cmake .. -DAVX2=OFF`)")
#   ADD_DEFINITIONS(-mavx2)
#   SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mavx2")

# ELSE(LTO)

#   MESSAGE(WARNING "AVX2 is DISABLED (to enable, run `cmake .. -DAVX2=ON`). Performance will be low.")

# ENDIF(AVX2)



SET(SOURCES ${SOURCES} src/mica/alloc/hugetlbfs_shm.cc)
SET(SOURCES ${SOURCES} src/mica/transaction/timestamp.cc)
SET(SOURCES ${SOURCES} src/mica/util/config.cc)
SET(SOURCES ${SOURCES} src/mica/util/cityhash/city_mod.cc)
SET(SOURCES ${SOURCES} src/mica/util/siphash/siphash24.c)
SET_SOURCE_FILES_PROPERTIES(src/mica/util/siphash/siphash24.c PROPERTIES LANGUAGE CXX)
SET(SOURCES ${SOURCES} src/mica/util/lcore.cc)
SET(SOURCES ${SOURCES} src/mica/util/stopwatch.cc)
SET(SOURCES ${SOURCES} src/mica/util/zipf.cc)

SET(LIBRARIES ${LIBRARIES} rt numa pthread)



IF(LTO)

  ADD_EXECUTABLE(test_atomics src/mica/test/test_atomics.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_atomics ${LIBRARIES})

  ADD_EXECUTABLE(test_tsc_sync src/mica/test/test_tsc_sync.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_tsc_sync ${LIBRARIES})

  ADD_EXECUTABLE(test_tx src/mica/test/test_tx.cc ${SOURCES}
      ${hw_proto_srcs} ${hw_grpc_srcs})
  TARGET_LINK_LIBRARIES(test_tx ${LIBRARIES}
      ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

  ADD_EXECUTABLE(test_tx_index src/mica/test/test_tx_index.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_tx_index ${LIBRARIES})

  ADD_EXECUTABLE(test_jenn src/mica/test/test_jenn.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_jenn ${LIBRARIES})

  ADD_EXECUTABLE(hotshard_gateway_server src/mica/test/hotshard_gateway_server.cc ${SOURCES}
      ${hw_proto_srcs} ${hw_grpc_srcs})
  TARGET_LINK_LIBRARIES(hotshard_gateway_server ${LIBRARIES}
      ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

ELSE(LTO)

  ADD_LIBRARY(common ${SOURCES})

  ADD_EXECUTABLE(test_atomics src/mica/test/test_atomics.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_atomics ${LIBRARIES})

  ADD_EXECUTABLE(test_tsc_sync src/mica/test/test_tsc_sync.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_tsc_sync ${LIBRARIES})

  ADD_EXECUTABLE(test_tx src/mica/test/test_tx.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_tx ${LIBRARIES})

  ADD_EXECUTABLE(test_tx_index src/mica/test/test_tx_index.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_tx_index ${LIBRARIES})

  ADD_EXECUTABLE(test_jenn src/mica/test/test_jenn.cc ${SOURCES})
  TARGET_LINK_LIBRARIES(test_jenn ${LIBRARIES})

  ADD_EXECUTABLE(hotshard_gateway_server src/mica/test/hotshard_gateway_server.cc ${SOURCES}
      ${hw_proto_srcs} ${hw_grpc_srcs})
  TARGET_LINK_LIBRARIES(hotshard_gateway_server ${LIBRARIES}
      ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

ENDIF(LTO)
