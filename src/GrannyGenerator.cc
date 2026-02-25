#include "GrannyGenerator.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <map>
#include <memory>

using namespace google::protobuf;

bool GrannyGenerator::Generate(const FileDescriptor* file,
                               const std::string& parameter,
                               compiler::GeneratorContext* context,
                               std::string* error) const {
  (void)parameter;

  if (file->service_count() == 0) {
    return true;
  }

  std::string file_name(file->name());
  std::string base_name = file_name.substr(0, file_name.find_last_of('.'));
  std::string output_filename = base_name + ".granny.h";

  auto output_stream = std::unique_ptr<io::ZeroCopyOutputStream>(
      context->Open(output_filename));

  io::Printer printer(output_stream.get(), '$');

  printer.Print("#pragma once\n");
  printer.Print("#include \"$base$.pb.h\"\n", "base", base_name);
  printer.Print("#include \"granny_shim.h\"\n\n");

  for (int i = 0; i < file->service_count(); i++) {
    const ServiceDescriptor* service = file->service(i);

    printer.Print("class $classname$ {\npublic:\n",
                  "classname", std::string(service->name()));
    printer.Indent();

    printer.Print("class StubInterface {\npublic:\n");
    printer.Indent();
    printer.Print("virtual ~StubInterface() = default;\n");

    for (int j = 0; j < service->method_count(); j++) {
      const MethodDescriptor* method = service->method(j);

      std::map<std::string, std::string> vars;
      vars["method_name"] = std::string(method->name());
      vars["req_type"]    = std::string(method->input_type()->name());
      vars["res_type"]    = std::string(method->output_type()->name());

      printer.Print(vars,
        "virtual granny::Status $method_name$(granny::ClientContext* ctx, "
        "const $req_type$& req, $res_type$* res) = 0;\n");
    }

    printer.Outdent();
    printer.Print("};\n");

    printer.Outdent();
    printer.Print("};\n\n");
  }

  return true;
}