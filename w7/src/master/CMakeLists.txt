FILE(GLOB MASTER_SRC *.cc)

set ( CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ~/src/cpp-netlib/build )
find_package ( cppnetlib 0.13.0 REQUIRED )

# include_directories(${CMAKE_BINARY_DIR}/proto)
include_directories(${GENERATED_PROTOBUF_PATH})
include_directories(${CHECK_INCLUDE_DIRS})
include_directories(${ZOOKEEPER_INCLUDE_DIR}/zookeeper)
include_directories(${CONSERVATOR_INCLUDE_DIR}/conservator)
include_directories(${CMAKE_SOURCE_DIR}/src/master)
include_directories ( ${CPPNETLIB_INCLUDE_DIRS} )
link_libraries(grpc++_unsecure grpc gpr ${PROTOBUF_LIBRARY})

add_executable(master ${MASTER_SRC})
target_link_libraries(master mr_grpc_proto ${Boost_LIBRARIES}  ${SSL_LIBRARIES} ${CPPNETLIB_LIBRARIES} ${CASABLANCA_LIBRARY} ${AZURE_STORAGE_LIBRARY} ${ZOOKEEPER_LIBRARIES} ${CONSERVATOR_LIBRARIES} ${_GLOG} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

