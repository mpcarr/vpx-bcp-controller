#include "BcpClient.h"
#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"
#include "plugins/VPXPlugin.h"
#include "plugins/LoggingPlugin.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>

namespace {
const MsgPluginAPI* host = nullptr;
ScriptablePluginAPI* script = nullptr;
LoggingPluginAPI* logging = nullptr;
VPXPluginAPI* vpx = nullptr;
uint32_t endpoint = 0;
unsigned int endMessage = 0;
struct Controller;
std::set<Controller*> controllers;
void Log(const std::string& text, unsigned int level = LPI_LVL_INFO) {
    if (logging) logging->Log("BCP", "Controller", 0, level, text.c_str());
}
struct RefCounted {
    unsigned int references = 1;
    virtual ~RefCounted() = default;
    unsigned int AddRef() { return ++references; }
    unsigned int Release() { unsigned int n = --references; if (!n) delete this; return n; }
};
struct ScriptMessage : RefCounted { bcp::Message value; explicit ScriptMessage(bcp::Message m) : value(std::move(m)) {} };
struct Controller : RefCounted {
    bcp::Client client;
    bool loggingEnabled = false;
    std::string reportedError;
    Controller() { controllers.insert(this); }
    ~Controller() override { client.Disconnect(); controllers.erase(this); }
    void PollError() {
        auto error = client.LastError();
        if (loggingEnabled && !error.empty() && error != reportedError) { Log(error, LPI_LVL_ERROR); reportedError = error; }
    }
    void Connect(int port, const std::string& exe = "", const std::string& project = "") {
        if (port < 1 || port > 65535) throw std::invalid_argument("Invalid BCP port");
        client.Disconnect(); reportedError.clear();
        std::filesystem::path tableDirectory;
        if (!exe.empty() && std::filesystem::u8path(exe).is_relative() && vpx && vpx->GetTableInfo) {
            VPXTableInfo info{};
            vpx->GetTableInfo(&info);
            // VPX supplies a native narrow path, unlike UTF-8 script arguments.
            if (info.path && *info.path) tableDirectory = std::filesystem::path(info.path).parent_path();
        }
        bcp::Launch(exe, project, tableDirectory);
        client.Connect(port);
        if (loggingEnabled) Log("Connecting to localhost:" + std::to_string(port));
    }
};
void StringResult(ScriptVariant* ret, const std::string& value) {
    auto p = new char[value.size() + 1]; std::memcpy(p, value.c_str(), value.size() + 1);
    ret->vString = { [](ScriptString* s) { delete[] s->string; }, p };
}
void AddRef(void* me, int, ScriptVariant*, ScriptVariant* ret) { auto n = static_cast<RefCounted*>(me)->AddRef(); if (ret) ret->vUInt32 = n; }
void Release(void* me, int, ScriptVariant*, ScriptVariant* ret) { auto n = static_cast<RefCounted*>(me)->Release(); if (ret) ret->vUInt32 = n; }
void ControllerCall(void* me, int index, ScriptVariant* args, ScriptVariant* ret) {
    auto c = static_cast<Controller*>(me);
    try {
        switch (index) {
        case 2: c->Connect(args[0].vInt, args[1].vString.string); break;
        case 3: c->Connect(args[0].vInt, args[1].vString.string, args[2].vString.string); break;
        case 4: c->Connect(args[0].vInt); break;
        case 5: c->Connect(args[0].vInt, args[1].vString.string); break;
        case 6: c->client.Disconnect(); break;
        case 7:
            c->client.Send(args[0].vString.string);
            if (c->loggingEnabled) Log(std::string("Send: ") + args[0].vString.string, LPI_LVL_DEBUG);
            break;
        case 8: c->loggingEnabled = true; Log("Logging enabled"); break;
        case 9: ret->vBool = c->client.Connected(); break;
        case 10: StringResult(ret, c->client.LastError()); break;
        case 11: {
            c->PollError(); bcp::Message value;
            ret->vObject = c->client.Pop(value) ? new ScriptMessage(std::move(value)) : nullptr; break;
        }
        }
    } catch (const std::exception& e) {
        if (c->loggingEnabled) Log(e.what(), LPI_LVL_ERROR);
        script->OnError(PSC_ERR_FAIL, e.what());
    }
}
void MessageCall(void* me, int index, ScriptVariant* args, ScriptVariant* ret) {
    try {
        auto m = static_cast<ScriptMessage*>(me);
        if (index == 2) StringResult(ret, m->value.command);
        if (index == 3) StringResult(ret, m->value.GetValue(args[0].vString.string));
        if (index == 4) StringResult(ret, m->value.raw);
    } catch (const std::exception& e) { script->OnError(PSC_ERR_FAIL, e.what()); }
}
ScriptClassDef* MakeClass(const char* name, void* (*factory)(), std::initializer_list<ScriptClassMemberDef> members) {
    auto def = static_cast<ScriptClassDef*>(std::calloc(1, sizeof(ScriptClassDef) + members.size() * sizeof(ScriptClassMemberDef)));
    if (!def) throw std::bad_alloc();
    def->name.name = name; def->CreateObject = factory; def->nMembers = static_cast<unsigned int>(members.size());
    std::copy(members.begin(), members.end(), def->members); return def;
}
ScriptClassDef *controllerDef = nullptr, *messageDef = nullptr;
void OnGameEnd(unsigned int, void*, void*) { for (auto c : controllers) c->client.Disconnect(); }
}

MSGPI_EXPORT void MSGPIAPI BCPPluginLoad(uint32_t id, const MsgPluginAPI* api) {
    host = api; endpoint = id;
    auto acquire = [&](const char* ns, const char* name, void* target) {
        auto msg = host->GetMsgID(ns, name); host->BroadcastMsg(endpoint, msg, target); host->ReleaseMsgID(msg);
    };
    acquire(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API, &script);
    acquire(LOGPI_NAMESPACE, LOGPI_MSG_GET_API, &logging);
    acquire(VPXPI_NAMESPACE, VPXPI_MSG_GET_API, &vpx);
    if (!script || script->version != 1) { Log("Compatible script API is unavailable", LPI_LVL_ERROR); return; }
    messageDef = MakeClass("BCP_Message", nullptr, {
        {{"AddRef"}, {"uint32"}, 0, {}, AddRef}, {{"Release"}, {"uint32"}, 0, {}, Release},
        {{"Command"}, {"string"}, 0, {}, MessageCall},
        {{"GetValue"}, {"string"}, 1, {{"string"}}, MessageCall},
        {{"RawMessage"}, {"string"}, 0, {}, MessageCall}
    });
    controllerDef = MakeClass("BCP_Controller", []() -> void* { return new Controller(); }, {
        {{"AddRef"}, {"uint32"}, 0, {}, AddRef}, {{"Release"}, {"uint32"}, 0, {}, Release},
        {{"Connect"}, {"void"}, 2, {{"int"}, {"string"}}, ControllerCall},
        {{"Connect"}, {"void"}, 3, {{"int"}, {"string"}, {"string"}}, ControllerCall},
        {{"ConnectToDebug"}, {"void"}, 1, {{"int"}}, ControllerCall},
        {{"ConnectToBuild"}, {"void"}, 2, {{"int"}, {"string"}}, ControllerCall},
        {{"Disconnect"}, {"void"}, 0, {}, ControllerCall},
        {{"Send"}, {"void"}, 1, {{"string"}}, ControllerCall},
        {{"EnableLogging"}, {"void"}, 0, {}, ControllerCall},
        {{"Connected"}, {"bool"}, 0, {}, ControllerCall},
        {{"LastError"}, {"string"}, 0, {}, ControllerCall},
        {{"ReadMessage"}, {"BCP_Message"}, 0, {}, ControllerCall}
    });
    script->RegisterScriptClass(messageDef);
    script->RegisterScriptClass(controllerDef);
    script->SubmitTypeLibrary(endpoint);
    script->SetCOMObjectOverride("vpx_bcp_controller.VpxBcpController", controllerDef);
    script->SetCOMObjectOverride("BCP.Controller", controllerDef);
    endMessage = host->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_GAME_END);
    host->SubscribeMsg(endpoint, endMessage, OnGameEnd, nullptr);
}
MSGPI_EXPORT void MSGPIAPI BCPPluginUnload() {
    OnGameEnd(0, nullptr, nullptr);
    if (controllerDef) {
        host->UnsubscribeMsg(endMessage, OnGameEnd, nullptr); host->ReleaseMsgID(endMessage);
        script->SetCOMObjectOverride("vpx_bcp_controller.VpxBcpController", nullptr);
        script->SetCOMObjectOverride("BCP.Controller", nullptr);
        script->UnregisterScriptClass(controllerDef);
        script->UnregisterScriptClass(messageDef);
        std::free(controllerDef); std::free(messageDef); controllerDef = messageDef = nullptr;
    }
    // VPX must release all script objects before unloading the shared library.
    logging = nullptr; script = nullptr; vpx = nullptr; host = nullptr;
}
