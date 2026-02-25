cmake_minimum_required(VERSION 3.16)
project(GrannyGenerator CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED CONFIG)

add_executable(protoc-gen-granny
  src/main.cc
  src/GrannyGenerator.cc
)

target_link_libraries(protoc-gen-granny PRIVATE
  protobuf::libprotoc
  protobuf::libprotobuf
)

target_include_directories(protoc-gen-granny PRIVATE
  ${Protobuf_INCLUDE_DIRS}
)