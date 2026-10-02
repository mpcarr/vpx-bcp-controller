#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"
#include "plugins/LoggingPlugin.h"
#include <map>
#include <stdexcept>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>

extern "C" void BCPPluginLoad(uint32_t, const MsgPluginAPI*);
extern "C" void BCPPluginUnload();
namespace {
std::map<std::string, ScriptClassDef*> classes, overrides;
std::map<unsigned int, std::string> ids;
ScriptablePluginAPI scripting{};
int subscriptions = 0;
void Check(bool value) { if (!value) throw std::runtime_error("Binding check failed"); }
ScriptVariant Call(ScriptClassDef* type, void* me, const char* name, int n = 0, ScriptVariant* args = nullptr) {
    for (unsigned int i = 0; i < type->nMembers; ++i) {
        auto& member = type->members[i];
        if (std::string(member.name.name) == name && member.nArgs == n) {
            ScriptVariant ret{}; member.Call(me, i, args, &ret); return ret;
        }
    }
    throw std::runtime_error("Missing method");
}
}
int main(int argc, char** argv) {
    try {
        scripting.version = 1;
        scripting.RegisterScriptClass = [](ScriptClassDef* d) { classes[d->name.name] = d; };
        scripting.UnregisterScriptClass = [](ScriptClassDef* d) { classes.erase(d->name.name); };
        scripting.RegisterScriptArrayType = [](ScriptArrayDef*) { throw std::runtime_error("Native BCP must not require array marshalling"); };
        scripting.SubmitTypeLibrary = [](unsigned int) {};
        scripting.SetCOMObjectOverride = [](const char* s, const ScriptClassDef* d) { if (d) overrides[s] = const_cast<ScriptClassDef*>(d); else overrides.erase(s); };
        scripting.OnError = [](unsigned int, const char* text) { throw std::runtime_error(text); };
        MsgPluginAPI api{}; api.version = 1;
        api.GetMsgID = [](const char* ns, const char* name) { auto id = static_cast<unsigned int>(ids.size()+1); ids[id] = std::string(ns) + name; return id; };
        api.ReleaseMsgID = [](unsigned int id) { ids.erase(id); };
        api.BroadcastMsg = [](uint32_t, unsigned int id, void* result) { if (ids[id].starts_with(SCRIPTPI_NAMESPACE)) *static_cast<ScriptablePluginAPI**>(result) = &scripting; };
        api.SubscribeMsg = [](uint32_t, unsigned int, msgpi_msg_callback, void*) { ++subscriptions; };
        api.UnsubscribeMsg = [](unsigned int, msgpi_msg_callback, void*) { --subscriptions; };
        BCPPluginLoad(7, &api);
        Check(classes.size() == 2 && overrides.size() == 2 && subscriptions == 1);
        auto def = overrides.at("vpx_bcp_controller.VpxBcpController");
        int connects = 0;
        for (unsigned int i = 0; i < def->nMembers; ++i) {
            Check(std::string(def->members[i].name.name) != "GetMessages");
            if (std::string(def->members[i].name.name) == "Connect") ++connects;
        }
        Check(connects == 2);
        void* a = def->CreateObject(); void* b = def->CreateObject(); Check(a != b);
        Check(!Call(def, a, "Connected").vBool);
        Check(Call(def, a, "ReadMessage").vObject == nullptr);
        Check(Call(def, b, "ReadMessage").vObject == nullptr);
        if (argc == 3) {
            auto connect = [&](void* c, int port) { ScriptVariant arg{}; arg.vInt = port; Call(def, c, "ConnectToDebug", 1, &arg); };
            auto send = [&](void* c, const char* text) { ScriptVariant arg{}; arg.vString.string = const_cast<char*>(text); Call(def, c, "Send", 1, &arg); };
            connect(a, std::stoi(argv[1])); connect(b, std::stoi(argv[2]));
            send(a, "trigger?name=first"); send(a, "trigger?name=second"); send(b, "trigger?name=monitor");
            auto messageDef = classes.at("BCP_Message");
            std::vector<void*> aa, bb;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline && (aa.size() < 3 || bb.size() < 2)) {
                while (auto value = Call(def, a, "ReadMessage").vObject) aa.push_back(value);
                while (auto value = Call(def, b, "ReadMessage").vObject) bb.push_back(value);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            Check(aa.size() == 3 && bb.size() == 2);
            auto get = [&](void* m, const char* key) {
                ScriptVariant arg{}; arg.vString.string = const_cast<char*>(key);
                auto value = Call(messageDef, m, "GetValue", 1, &arg).vString;
                std::string result(value.string); value.Release(&value); return result;
            };
            Check(get(aa[1], "name") == "caf\xc3\xa9");
            Check(get(aa[2], "json").size() > 100000);
            Check(get(bb[1], "debug") == "bool:true");
            Check(get(aa[0], "RawMessage") == "hello");
            Call(def, a, "Disconnect"); Check(Call(def, b, "Connected").vBool);
            send(b, "trigger?name=still_connected");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (auto m : aa) Call(messageDef, m, "Release");
            for (auto m : bb) Call(messageDef, m, "Release");
        }
        Call(def, a, "Disconnect"); Call(def, b, "Disconnect");
        Call(def, a, "Release"); Call(def, b, "Release");
        BCPPluginUnload(); Check(classes.empty() && overrides.empty() && ids.empty() && subscriptions == 0);
        BCPPluginLoad(8, &api); BCPPluginUnload(); Check(classes.empty());
        std::cout << "Bindings OK\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
