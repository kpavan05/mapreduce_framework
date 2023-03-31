project(MapReduce)
cmake_minimum_required(VERSION 3.1)
# set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/CMakeModules/")

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost REQUIRED COMPONENTS log thread system filesystem program_options)
find_package(Casablanca REQUIRED)
find_package(AzureStorageCpp REQUIRED)
find_package(SSL REQUIRED)
find_package(glog 0.5.0 REQUIRED)

add_definitions(-std=c++11)

add_subdirectory(src)
