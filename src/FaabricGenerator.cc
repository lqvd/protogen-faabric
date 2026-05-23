#include "FaabricGenerator.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace google::protobuf;

namespace {

static std::string toCppType(const Descriptor* d)
{
    std::string n = d->full_name();
    size_t pos = 0;
    while ((pos = n.find('.', pos)) != std::string::npos) {
        n.replace(pos, 1, "::");
        pos += 2;
    }
    return "::" + n;
}

static std::vector<std::string> splitPackage(const std::string& package)
{
    std::vector<std::string> parts;

    if (package.empty()) {
        return parts;
    }

    size_t start = 0;
    size_t end = 0;
    while ((end = package.find('.', start)) != std::string::npos) {
        parts.push_back(package.substr(start, end - start));
        start = end + 1;
    }

    parts.push_back(package.substr(start));
    return parts;
}

static bool hasSingleStringField(const Descriptor* d, std::string& fieldName)
{
    if (d->field_count() != 1) {
        return false;
    }

    const FieldDescriptor* f = d->field(0);
    if (f->type() != FieldDescriptor::TYPE_STRING) {
        return false;
    }

    fieldName = f->name();
    return true;
}

void GenerateClientStub(const ServiceDescriptor* service, io::Printer& printer)
{
    printer.Print("class Stub final {\n");
    printer.Print("public:\n");
    printer.Indent();

    printer.Print("explicit Stub(int32_t channelId) : channelId_(channelId) {}\n\n");

    for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);

        if (method->client_streaming() || method->server_streaming()) {
            continue;
        }

        std::map<std::string, std::string> vars;
        vars["method_name"] = method->name();
        vars["full_name"] = "/" + service->full_name() + "/" + method->name();
        vars["req_type"] = toCppType(method->input_type());
        vars["res_type"] = toCppType(method->output_type());

        printer.Print(
            vars,
            "faabric::rpc::Task<$res_type$> $method_name$(const $req_type$& req) {\n");
        printer.Indent();
        printer.Print(
            vars,
            "std::vector<uint8_t> reqBuf(req.ByteSizeLong());\n"
            "req.SerializeToArray(reqBuf.data(), static_cast<int>(reqBuf.size()));\n\n"
            "int32_t requestId = 0;\n"
            "int32_t status = __faasm_rpc_unary_start(\n"
            "    channelId_,\n"
            "    \"$full_name$\",\n"
            "    reqBuf.data(),\n"
            "    static_cast<int32_t>(reqBuf.size()),\n"
            "    &requestId,\n"
            "    -1);\n\n"
            "(void)status;\n"
            "co_return co_await faabric::rpc::RpcCall<$res_type$>(requestId);\n");
        printer.Outdent();
        printer.Print("}\n\n");

        std::string singleStringField;
        if (hasSingleStringField(method->input_type(), singleStringField)) {
            vars["field_name"] = singleStringField;

            printer.Print(
                vars,
                "faabric::rpc::Task<$res_type$> $method_name$(const std::string& message) {\n");
            printer.Indent();
            printer.Print(
                vars,
                "$req_type$ req;\n"
                "req.set_$field_name$(message);\n"
                "co_return co_await $method_name$(req);\n");
            printer.Outdent();
            printer.Print("}\n\n");

            printer.Print(
                vars,
                "faabric::rpc::Task<$res_type$> $method_name$(const char* message) {\n");
            printer.Indent();
            printer.Print(
                vars,
                "$req_type$ req;\n"
                "req.set_$field_name$(message);\n"
                "co_return co_await $method_name$(req);\n");
            printer.Outdent();
            printer.Print("}\n\n");
        }

        printer.Print(
            vars,
            "faabric::rpc::RpcCall<$res_type$> "
            "$method_name$Async(const $req_type$& req) {\n");
        printer.Indent();
        printer.Print(
            vars,
            "std::vector<uint8_t> reqBuf(req.ByteSizeLong());\n"
            "req.SerializeToArray(reqBuf.data(), static_cast<int>(reqBuf.size()));\n\n"
            "int32_t requestId = 0;\n"
            "int32_t status = __faasm_rpc_unary_start(\n"
            "    channelId_,\n"
            "    \"$full_name$\",\n"
            "    reqBuf.data(),\n"
            "    static_cast<int32_t>(reqBuf.size()),\n"
            "    &requestId,\n"
            "    -1);\n\n"
            "(void)status;\n"
            "return faabric::rpc::RpcCall<$res_type$>(requestId);\n");
        printer.Outdent();
        printer.Print("}\n\n");

        if (hasSingleStringField(method->input_type(), singleStringField)) {
            vars["field_name"] = singleStringField;

            printer.Print(
                vars,
                "faabric::rpc::RpcCall<$res_type$> "
                "$method_name$Async(const std::string& message) {\n");
            printer.Indent();
            printer.Print(
                vars,
                "$req_type$ req;\n"
                "req.set_$field_name$(message);\n"
                "return $method_name$Async(req);\n");
            printer.Outdent();
            printer.Print("}\n\n");

            printer.Print(
                vars,
                "faabric::rpc::RpcCall<$res_type$> "
                "$method_name$Async(const char* message) {\n");
            printer.Indent();
            printer.Print(
                vars,
                "$req_type$ req;\n"
                "req.set_$field_name$(message);\n"
                "return $method_name$Async(req);\n");
            printer.Outdent();
            printer.Print("}\n\n");
        }
    }

    printer.Outdent();
    printer.Print("private:\n");
    printer.Indent();
    printer.Print("int32_t channelId_;\n");
    printer.Outdent();
    printer.Print("}; // class Stub\n\n");

    printer.Print(
        "static std::unique_ptr<Stub> NewStub(int32_t channelId) {\n"
        "  return std::make_unique<Stub>(channelId);\n"
        "}\n\n");
}

void GenerateServerService(const ServiceDescriptor* service, io::Printer& printer)
{
    printer.Print("class Service : public faabric::rpc::Service {\n");
    printer.Print("public:\n");
    printer.Indent();

    printer.Print("Service() {\n");
    printer.Indent();

    for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        if (method->client_streaming() || method->server_streaming()) {
            continue;
        }
        std::map<std::string, std::string> vars;
        vars["full_name"] = "/" + service->full_name() + "/" + method->name();
        printer.Print(vars, "AddMethod(\"$full_name$\");\n");
    }

    printer.Outdent();
    printer.Print("}\n\n");

    // Declare the virtual methods the user implements.
    // Each returns Task<faabric::rpc::Status> so handlers can suspend at co_await
    // (e.g. when making downstream RPC calls).
    for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        if (method->client_streaming() || method->server_streaming()) {
            continue;
        }

        std::map<std::string, std::string> vars;
        vars["method_name"] = method->name();
        vars["req_type"] = toCppType(method->input_type());
        vars["res_type"] = toCppType(method->output_type());

        printer.Print(
            vars,
            "virtual faabric::rpc::Task<faabric::rpc::Status> $method_name$(\n"
            "    faabric::rpc::ServerContext* ctx,\n"
            "    const $req_type$* req,\n"
            "    $res_type$* res) = 0;\n\n");
    }

    printer.Print(
        "faabric::rpc::Task<faabric::rpc::Status> HandleCall(\n"
        "    const std::string& method,\n"
        "    const uint8_t* reqData,\n"
        "    size_t reqLen,\n"
        "    std::vector<uint8_t>& respData) override {\n");
    printer.Indent();

    for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        if (method->client_streaming() || method->server_streaming()) {
            continue;
        }

        std::map<std::string, std::string> vars;
        vars["method_name"] = method->name();
        vars["full_name"] = "/" + service->full_name() + "/" + method->name();
        vars["req_type"] = toCppType(method->input_type());
        vars["res_type"] = toCppType(method->output_type());

        printer.Print(
            vars,
            "if (method == \"$full_name$\") {\n"
            "  $req_type$ req;\n"
            "  if (!req.ParseFromArray(reqData, static_cast<int>(reqLen))) {\n"
            "    co_return faabric::rpc::Status{Rpc_StatusCode::INTERNAL,\n"
            "                         \"Deserialisation failed\"};\n"
            "  }\n\n"
            "  $res_type$ res;\n"
            "  faabric::rpc::ServerContext ctx;\n"
            "  faabric::rpc::Status status = co_await $method_name$(&ctx, &req, &res);\n"
            "  if (!status.ok()) {\n"
            "    co_return status;\n"
            "  }\n\n"
            "  respData.resize(res.ByteSizeLong());\n"
            "  res.SerializeToArray(respData.data(),\n"
            "                       static_cast<int>(respData.size()));\n"
            "  co_return status;\n"
            "}\n");
    }

    printer.Print(
        "co_return faabric::rpc::Status{Rpc_StatusCode::NOT_FOUND,\n"
        "                     \"Unknown method: \" + method};\n");

    printer.Outdent();
    printer.Print("}\n");

    printer.Outdent();
    printer.Print("}; // class Service\n\n");
}

} // namespace

bool FaabricGenerator::Generate(const FileDescriptor* file,
                                const std::string& parameter,
                                compiler::GeneratorContext* context,
                                std::string* error) const
{
    (void)parameter;

    if (file->service_count() == 0) {
        return true;
    }

    std::string fileName(file->name());
    std::string baseName = fileName.substr(0, fileName.find_last_of('.'));
    std::string outputFilename = baseName + ".faabric.h";

    auto outputStream = std::unique_ptr<io::ZeroCopyOutputStream>(
        context->Open(outputFilename));

    if (!outputStream) {
        *error = "Failed to open output file " + outputFilename;
        return false;
    }

    io::Printer printer(outputStream.get(), '$');

    printer.Print("// Generated by the protocol buffer compiler. DO NOT EDIT!\n");
    printer.Print("// source: $name$\n\n", "name", file->name());

    printer.Print("#pragma once\n\n");
    
    printer.Print("#include <faasrpc/RpcCall.h>\n");
    printer.Print("#include <faasrpc/Service.h>\n");
    printer.Print("#include <faasrpc/Server.h>\n");
    printer.Print("#include <faasrpc/Task.h>\n");
    printer.Print("#include <rpc.h>\n\n");

    printer.Print("#include <cstddef>\n");
    printer.Print("#include <cstdint>\n");
    printer.Print("#include <memory>\n");
    printer.Print("#include <string>\n");
    printer.Print("#include <vector>\n\n");

    printer.Print("#include \"$base$.pb.h\"\n\n", "base", baseName);

    std::vector<std::string> namespaceParts = splitPackage(file->package());

    for (const auto& part : namespaceParts) {
        printer.Print("namespace $part$ {\n", "part", part);
    }

    if (!namespaceParts.empty()) {
        printer.Print("\n");
    }

    for (int i = 0; i < file->service_count(); ++i) {
        const ServiceDescriptor* service = file->service(i);

        printer.Print("class $name$ final {\n", "name", service->name());
        printer.Print("public:\n");
        printer.Indent();

        GenerateClientStub(service, printer);
        GenerateServerService(service, printer);

        printer.Outdent();
        printer.Print("private:\n");
        printer.Indent();

        printer.Print(
            "$name$() = delete;\n"
            "~$name$() = delete;\n"
            "$name$(const $name$&) = delete;\n"
            "$name$& operator=(const $name$&) = delete;\n",
            "name", service->name());

        printer.Outdent();
        printer.Print("};\n\n");
    }

    for (auto it = namespaceParts.rbegin(); it != namespaceParts.rend(); ++it) {
        printer.Print("} // namespace $part$\n", "part", *it);
    }

    if (printer.failed()) {
        *error = "FaabricGenerator failed to write to output file.";
        return false;
    }

    return true;
}