cmake_minimum_required(VERSION 3.1)

# Add local option
option(USE_LOCAL "Compile for the local machine instead of kubernetes" OFF)
if (USE_LOCAL)
  message(STATUS "Defining LOCAL")
  add_definitions(-DLOCAL=1)
endif()

find_package(Threads REQUIRED)
find_package(Zookeeper REQUIRED)
find_package(Check REQUIRED)
find_package(Conservator REQUIRED)

find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

find_package(glog 0.5.0 REQUIRED)

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
set(_GLOG glog::glog)

# Find gRPC installation
# Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP gRPC::grpc++)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

include_directories(/usr/local/include)
link_directories(/usr/local/lib)

message(STATUS ${CMAKE_SOURCE_DIR})
set(PROTO_PATH "${CMAKE_SOURCE_DIR}/src/protos")
set(MAPREDUCE_PROTO "${CMAKE_SOURCE_DIR}/src/protos/mr.proto")
set(GENERATED_PROTOBUF_PATH "${CMAKE_BINARY_DIR}/proto")
file(MAKE_DIRECTORY ${GENERATED_PROTOBUF_PATH})

set(MAPREDUCE_PB_CPP_FILE "${GENERATED_PROTOBUF_PATH}/mr.pb.cc")
set(MAPREDUCE_PB_H_FILE "${GENERATED_PROTOBUF_PATH}/mr.pb.h")
set(MAPREDUCE_GRPC_PB_CPP_FILE "${GENERATED_PROTOBUF_PATH}/mr.grpc.pb.cc")
set(MAPREDUCE_GRPC_PB_H_FILE "${GENERATED_PROTOBUF_PATH}/mr.grpc.pb.h")
add_custom_command(
    OUTPUT "${MAPREDUCE_PB_H_FILE}"
           "${MAPREDUCE_PB_CPP_FILE}"
           "${MAPREDUCE_GRPC_PB_H_FILE}"
           "${MAPREDUCE_GRPC_PB_CPP_FILE}"
    COMMAND protoc
    ARGS "--proto_path=${PROTO_PATH}"
         "--cpp_out=${GENERATED_PROTOBUF_PATH}"
         "${MAPREDUCE_PROTO}"
    COMMAND protoc
    ARGS "--proto_path=${PROTO_PATH}"
         "--grpc_out=${GENERATED_PROTOBUF_PATH}"
         "--plugin=protoc-gen-grpc=/usr/local/bin/grpc_cpp_plugin"
         "${MAPREDUCE_PROTO}"
    DEPENDS ${MAPREDUCE_PROTO}
    )


add_library(mr_grpc_proto ${MAPREDUCE_PB_H_FILE} ${MAPREDUCE_PB_CPP_FILE} ${MAPREDUCE_GRPC_PB_H_FILE} ${MAPREDUCE_GRPC_PB_CPP_FILE})
target_link_libraries(mr_grpc_proto ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

set(GENERATED_PROTOBUF_FILES ${MAPREDUCE_PB_H_FILE} ${MAPREDUCE_PB_CPP_FILE}
    ${MAPREDUCE_GRPC_PB_H_FILE} ${MAPREDUCE_GRPC_PB_CPP_FILE})

add_subdirectory(master)
add_subdirectory(worker)
# add_subdirectory(httpsrv)
