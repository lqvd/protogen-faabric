#ifndef FAABRIC_GENERATOR_H_
#define FAABRIC_GENERATOR_H_

#include <google/protobuf/compiler/code_generator.h>
#include <string>

using namespace google::protobuf;

class GrannyGenerator : public compiler::CodeGenerator {
 public:
  GrannyGenerator() = default;
  ~GrannyGenerator() override = default;

  uint64_t GetSupportedFeatures() const override {
    return FEATURE_PROTO3_OPTIONAL;
  }

  bool Generate(const FileDescriptor* file, const std::string& parameter,
                compiler::GeneratorContext* generator_context,
                std::string* error) const override;
};

#endif  // FAABRIC_GENERATOR_H_