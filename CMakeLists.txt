cmake_minimum_required(VERSION 3.16)
project(FaabricGenerator CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED CONFIG)

add_executable(protoc-gen-faabric
  src/main.cc
  src/FaabricGenerator.cc
)

target_link_libraries(protoc-gen-faabric PRIVATE
  protobuf::libprotoc
  protobuf::libprotobuf
)

target_include_directories(protoc-gen-faabric PRIVATE
  ${Protobuf_INCLUDE_DIRS}
)