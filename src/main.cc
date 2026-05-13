#include "FaabricGenerator.h"
#include <google/protobuf/compiler/plugin.h>

// Call point for Granny code generator
// https://protobuf.dev/reference/cpp/api-docs/google.protobuf.compiler.plugin/#google.protobuf.compiler

int main(int argc, char* argv[]) {
  FaabricGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}