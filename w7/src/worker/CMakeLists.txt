FILE(GLOB WORKER_SRC *.cc)

# include_directories(${CMAKE_BINARY_DIR}/proto)

include_directories(${GENERATED_PROTOBUF_PATH})
include_directories(${CHECK_INCLUDE_DIRS})
include_directories(${ZOOKEEPER_INCLUDE_DIR}/zookeeper)
include_directories(${CONSERVATOR_INCLUDE_DIR}/conservator)
include_directories(${CMAKE_SOURCE_DIR}/src/worker)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I/usr/include/python3.8 -I/usr/include/python3.8  -Wno-unused-result -Wsign-compare -g -fdebug-prefix-map=/build/python3.8-4OrTnN/python3.8-3.8.10=. -specs=/usr/share/dpkg/no-pie-compile.specs -fstack-protector -Wformat -Werror=format-security  -DNDEBUG -g -fwrapv -O3 -Wall")
find_package(PythonLibs REQUIRED)
include_directories(${PYTHON_INCLUDE_DIRS})

link_libraries(grpc++_unsecure grpc gpr ${PROTOBUF_LIBRARY})
add_executable(worker ${WORKER_SRC})
target_link_libraries(worker mr_grpc_proto ${Boost_LIBRARIES} ${PYTHON_LIBRARIES}  ${SSL_LIBRARIES} ${CASABLANCA_LIBRARY} ${AZURE_STORAGE_LIBRARY} ${ZOOKEEPER_LIBRARIES} ${CONSERVATOR_LIBRARIES} ${_GLOG} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

