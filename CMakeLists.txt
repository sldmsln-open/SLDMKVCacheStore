cmake_minimum_required(VERSION 3.14)
project(sldmkvcachestore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_EXAMPLES "Build example programs" ON)

# Find dependencies
find_package(Threads REQUIRED)
find_package(RocksDB REQUIRED)
find_package(gflags REQUIRED)
find_package(protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Check for nuRaft library
find_path(NURAFT_INCLUDE_DIR libnuraft/nuraft.hxx)
find_library(NURAFT_LIBRARY nuraft)

if(NOT NURAFT_LIBRARY OR NOT NURAFT_INCLUDE_DIR)
  message(WARNING "nuraft library not found. 
    Please install nuraft (https://github.com/eBay/NuRaft) 
    or set NURAFT_INCLUDE_DIR and NURAFT_LIBRARY manually.
    We'll proceed, but you may need to set paths manually.")
endif()

# Include directories
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}
  ${NURAFT_INCLUDE_DIR}
)

# Generate version header
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/sldmkvcachestore/Version.h
)

# Compile protobuf files
set(PROTO_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/kvcache.proto
)

# Generate protobuf cpp files
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Replace protobuf_generate_grpc_cpp with the official gRPC way:
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/kvcache.grpc.pb.cc
         ${CMAKE_CURRENT_BINARY_DIR}/kvcache.grpc.pb.h
  COMMAND $<TARGET_FILE:protobuf::protoc>
  ARGS --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
       --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       -I${CMAKE_CURRENT_SOURCE_DIR}/proto
       ${CMAKE_CURRENT_SOURCE_DIR}/proto/kvcache.proto
  DEPENDS ${PROTO_FILES} protobuf::protoc gRPC::grpc_cpp_plugin
)

# Add the grpc generated files to your sources
set(GRPC_SRCS ${CMAKE_CURRENT_BINARY_DIR}/kvcache.grpc.pb.cc)
set(GRPC_HDRS ${CMAKE_CURRENT_BINARY_DIR}/kvcache.grpc.pb.h)

# Collect source files
file(GLOB_RECURSE SRC_FILES 
  src/common/*.cpp
  src/kvcache/*.cpp
  src/raft/*.cpp
  src/storage/*.cpp
)

# Create main library
add_library(sldmkvcachestore
  ${SRC_FILES}
  ${PROTO_SRCS}
  ${PROTO_HDRS}
  ${GRPC_SRCS}
  ${GRPC_HDRS}
)

# Link dependencies
target_link_libraries(sldmkvcachestore
  PUBLIC
    RocksDB::rocksdb
    gflags::gflags
    protobuf::libprotobuf
    gRPC::grpc++
    Threads::Threads
)

# Link nuRaft if available
if(NURAFT_LIBRARY)
  target_link_libraries(sldmkvcachestore PUBLIC ${NURAFT_LIBRARY})
endif()

# Set include directories
target_include_directories(sldmkvcachestore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Build examples
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Build tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Installation rules
install(TARGETS sldmkvcachestore
  EXPORT sldmkvcachestore-targets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)

install(DIRECTORY include/
  DESTINATION include
  FILES_MATCHING PATTERN "*.h*"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sldmkvcachestore/Version.h
  DESTINATION include/sldmkvcachestore
) 