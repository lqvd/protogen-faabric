#include "FaabricGenerator.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

using namespace google::protobuf;

void GenerateClientStub(const ServiceDescriptor* service, io::Printer& printer);
void GenerateServerService(const ServiceDescriptor* service, io::Printer& printer);
void GenerateServerRegistration(const ServiceDescriptor* service, io::Printer& printer);

static std::string toCppType(const Descriptor* d) {
    std::string n = d->full_name();
    size_t pos = 0;
    while ((pos = n.find('.', pos)) != std::string::npos) {
        n.replace(pos, 1, "::");
        pos += 2;
    }
    return "::" + n;
}

bool FaabricGenerator::Generate(const FileDescriptor* file,
                               const std::string& parameter,
                               compiler::GeneratorContext* context,
                               std::string* error) const
{
    (void)parameter;

    if (file->service_count() == 0) return true;

    std::string file_name(file->name());
    std::string base_name = file_name.substr(0, file_name.find_last_of('.'));
    std::string output_filename = base_name + ".faasm.h";

    auto output_stream = std::unique_ptr<io::ZeroCopyOutputStream>(
        context->Open(output_filename));
    if (!output_stream) {
        *error = "Failed to open output file " + output_filename;
        return false;
    }
    io::Printer printer(output_stream.get(), '$');

    printer.Print("#pragma once\n\n");
    printer.Print("#include <cstdint>\n");
    printer.Print("#include <vector>\n\n");
    printer.Print("#include <faabric/rpc/RpcServer.h>\n");
    printer.Print("#include <faasrpc/RpcCall.h>\n");
    printer.Print("#include <faasprc/Task.h>\n");
    printer.Print("#include <rpc.h>\n");
    printer.Print("#include \"$base$.pb.h\"\n\n", "base", base_name);

    // Dummy ServerContext to match gRPC API shape on the server side
    printer.Print(
        "namespace faabric::rpc {\n"
        "  class ServerContext {};\n"
        "} // namespace faabric::rpc\n\n"
    );

    std::string package = file->package();
    std::vector<std::string> parts;
    if (!package.empty()) {
        size_t start = 0, end = 0;
        while ((end = package.find('.', start)) != std::string::npos) {
            parts.push_back(package.substr(start, end - start));
            start = end + 1;
        }
        parts.push_back(package.substr(start));
        for (const auto& part : parts) {
            printer.Print("namespace $part$ {\n", "part", part);
        }
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

        GenerateServerRegistration(service, printer);
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        printer.Print("} // namespace\n");
    }

    if (printer.failed()) {
        *error = "FaabricGenerator failed to write to output file.";
        return false;
    }
    return true;
}

void GenerateClientStub(const ServiceDescriptor* service, io::Printer& printer) {
    printer.Print("class Stub final {\n");
    printer.Print("public:\n");
    printer.Indent();
    printer.Print("explicit Stub(int32_t channelId) : channelId_(channelId) {}\n\n");

    for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        if (method->client_streaming() || method->server_streaming()) continue;

        std::map<std::string, std::string> vars;
        vars["method_name"] = method->name();
        vars["full_name"] = "/" + service->full_name() + "/" + method->name();
        vars["req_type"] = toCppType(method->input_type());
        vars["res_type"] = toCppType(method->output_type());

        // ----------------------------------------------------------------
        // Sync: dispatch + await in one step.
        // One migration point at the co_await RpcCall (inside wait_migratable).
        // ----------------------------------------------------------------
        printer.Print(vars,
            "faabric::rpc::Task<$res_type$> $method_name$(const $req_type$& req) {\n");
        printer.Indent();
        printer.Print(vars,
            "std::vector<uint8_t> reqBuf(req.ByteSizeLong());\n"
            "req.SerializeToArray(reqBuf.data(), static_cast<int>(reqBuf.size()));\n"
            "int32_t requestId = 0;\n"
            "__faasm_rpc_unary_start(\n"
            "    channelId_,\n"
            "    \"$full_name$\",\n"
            "    reqBuf.data(),\n"
            "    static_cast<int32_t>(reqBuf.size()),\n"
            "    &requestId,\n"
            "    -1);\n"
            "co_return co_await faabric::coro::RpcCall<$res_type$>(requestId);\n");
        printer.Outdent();
        printer.Print("}\n\n");

        // ----------------------------------------------------------------
        // Async: migration checkpoint BEFORE dispatch (migration point 1),
        // then returns RpcCall. Migration point 2 fires when the caller
        // co_awaits the returned RpcCall.
        //
        // Usage:
        //   auto call_a = co_await stub.FooAsync(req_a);  // point 1
        //   auto call_b = co_await stub.FooAsync(req_b);  // point 1
        //   auto resp_a = co_await call_a;                // point 2
        //   auto resp_b = co_await call_b;                // point 2
        // ----------------------------------------------------------------
        printer.Print(vars,
            "faabric::rpc::Task<faabric::coro::RpcCall<$res_type$>> "
            "$method_name$Async(const $req_type$& req) {\n");
        printer.Indent();
        printer.Print(
            "// Migration point 1: migrate before dispatching if needed.\n"
            "// Ensures the request is sent from the correct host.\n"
            "co_await faabric::coro::MigrationCheckpoint{};\n\n");
        printer.Print(vars,
            "std::vector<uint8_t> reqBuf(req.ByteSizeLong());\n"
            "req.SerializeToArray(reqBuf.data(), static_cast<int>(reqBuf.size()));\n"
            "int32_t requestId = 0;\n"
            "__faasm_rpc_unary_start(\n"
            "    channelId_,\n"
            "    \"$full_name$\",\n"
            "    reqBuf.data(),\n"
            "    static_cast<int32_t>(reqBuf.size()),\n"
            "    &requestId,\n"
            "    -1);\n"
            "// Migration point 2 fires when the caller co_awaits the returned RpcCall.\n"
            "co_return faabric::coro::RpcCall<$res_type$>(requestId);\n");
        printer.Outdent();
        printer.Print("}\n\n");
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

void GenerateServerService(const ServiceDescriptor* service, io::Printer& printer) {
    printer.Print("class Service {\n");
    printer.Print("public:\n");
    printer.Indent();
    printer.Print("virtual ~Service() = default;\n\n");

    for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        if (method->client_streaming() || method->server_streaming()) continue;

        std::map<std::string, std::string> vars;
        vars["method_name"] = method->name();
        vars["req_type"] = toCppType(method->input_type());
        vars["res_type"] = toCppType(method->output_type());

        // Server handlers stay synchronous — they run in the RpcServer's
        // thread pool and don't need coroutine semantics.
        // Matches grpc::Status Method(ServerContext*, const Req*, Res*)
        printer.Print(vars,
            "virtual faabric::rpc::Status $method_name$(\n"
            "    faabric::rpc::ServerContext* ctx,\n"
            "    const $req_type$* req,\n"
            "    $res_type$* res) = 0;\n");
    }

    printer.Outdent();
    printer.Print("}; // class Service\n\n");
}

void GenerateServerRegistration(const ServiceDescriptor* service, io::Printer& printer) {
    std::map<std::string, std::string> vars;
    vars["service_name"] = service->name();

    printer.Print(vars,
        "inline void register$service_name$Service(\n"
        "    faabric::rpc::RpcServer& server,\n"
        "    std::shared_ptr<$service_name$::Service> service) {\n");
    printer.Indent();

    for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* method = service->method(i);
        if (method->client_streaming() || method->server_streaming()) continue;

        vars["method_name"] = method->name();
        vars["full_name"] = "/" + service->full_name() + "/" + method->name();
        vars["req_type"] = toCppType(method->input_type());
        vars["res_type"] = toCppType(method->output_type());

        printer.Print(vars,
            "server.registerHandler(\"$full_name$\",\n"
            "    [service](const uint8_t* reqData, size_t reqLen,\n"
            "              std::vector<uint8_t>& resData) -> faabric::rpc::Status {\n"
            "  $req_type$ req;\n"
            "  if (!req.ParseFromArray(reqData, static_cast<int>(reqLen))) {\n"
            "    return {faabric::rpc::StatusCode::INTERNAL,\n"
            "            \"Request deserialisation failed\"};\n"
            "  }\n\n"
            "  $res_type$ res;\n"
            "  faabric::rpc::ServerContext ctx;\n"
            "  faabric::rpc::Status status = service->$method_name$(&ctx, &req, &res);\n"
            "  if (!status.ok()) return status;\n\n"
            "  resData.resize(res.ByteSizeLong());\n"
            "  if (!res.SerializeToArray(resData.data(),\n"
            "                            static_cast<int>(resData.size()))) {\n"
            "    return {faabric::rpc::StatusCode::INTERNAL,\n"
            "            \"Response serialisation failed\"};\n"
            "  }\n"
            "  return status;\n"
            "});\n");
    }

    printer.Outdent();
    printer.Print("}\n\n");
}