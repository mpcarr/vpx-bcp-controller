#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"
#include "plugins/LoggingPlugin.h"
#include "plugins/VPXPlugin.h"
#include "plugins/ControllerPlugin.h"
#include <filesystem>
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
VPXPluginAPI vpx{};
std::string tablePath;
int subscriptions = 0;
std::map<unsigned int, std::pair<msgpi_msg_callback, void*>> callbacks;
std::vector<DisplaySrcId> displayCache;
int displayChanges = 0;
void Check(bool value) { if (!value) throw std::runtime_error("Binding check failed"); }
void Fire(const std::string& name, void* data = nullptr) {
    for (const auto& [id, text] : ids) if (text == name) {
        auto callback = callbacks.at(id);
        callback.first(id, callback.second, data);
        return;
    }
    throw std::runtime_error("Missing callback: " + name);
}
std::vector<DisplaySrcId> GetDisplays() {
    GetDisplaySrcMsg request{};
    Fire(std::string(CTLPI_NAMESPACE) + CTLPI_DISPLAY_GET_SRC_MSG, &request);
    std::vector<DisplaySrcId> result(request.count);
    request = {static_cast<unsigned int>(result.size()), 0, result.data()};
    Fire(std::string(CTLPI_NAMESPACE) + CTLPI_DISPLAY_GET_SRC_MSG, &request);
    Check(request.count == result.size());
    return result;
}
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
        // Rejected hosts must not call any dispatch-table entry, and unloading
        // after rejection must be safe. Null entries make accidental calls fail.
        BCPPluginLoad(7, nullptr);
        BCPPluginUnload();
        for (int version : {0, 2}) {
            MsgPluginAPI incompatible{};
            incompatible.version = version;
            BCPPluginLoad(7, &incompatible);
            BCPPluginUnload();
            Check(classes.empty() && overrides.empty() && ids.empty() && subscriptions == 0);
        }
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
        tablePath = (std::filesystem::current_path() / "table folder" / "Example.vpx").string();
        vpx.GetTableInfo = [](VPXTableInfo* info) { info->path = tablePath.c_str(); };
        api.BroadcastMsg = [](uint32_t, unsigned int id, void* result) {
            if (ids[id].starts_with(SCRIPTPI_NAMESPACE)) *static_cast<ScriptablePluginAPI**>(result) = &scripting;
            if (ids[id].starts_with(VPXPI_NAMESPACE)) *static_cast<VPXPluginAPI**>(result) = &vpx;
            if (ids[id] == std::string(CTLPI_NAMESPACE) + CTLPI_DISPLAY_ON_SRC_CHG_MSG) {
                // Old call contexts must remain alive during removal notification.
                for (const auto& display : displayCache) Check(display.GetRenderFrame(display.callContext).frame != nullptr);
                displayCache = GetDisplays();
                ++displayChanges;
            }
        };
        api.SubscribeMsg = [](uint32_t, unsigned int id, msgpi_msg_callback callback, void* context) { callbacks[id] = {callback, context}; ++subscriptions; };
        api.UnsubscribeMsg = [](unsigned int id, msgpi_msg_callback, void*) { callbacks.erase(id); --subscriptions; };
        BCPPluginLoad(7, &api);
        Check(classes.size() == 2 && overrides.size() == 2 && subscriptions == 2);
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
        ScriptVariant launchArgs[2]{};
        launchArgs[0].vInt = 5050;
        launchArgs[1].vString.string = const_cast<char*>("nonexistent_bcp_export");
        bool tableRelativeError = false;
        try { Call(def, a, "ConnectToBuild", 2, launchArgs); }
        catch (const std::exception& e) {
            tableRelativeError = std::string(e.what()).find("table folder") != std::string::npos;
        }
        Check(tableRelativeError);
        ScriptVariant displayArgs[5]{};
        displayArgs[0].vString.string = const_cast<char*>("Main");
        displayArgs[1].vInt = 2; displayArgs[2].vInt = 1;
        displayArgs[3].vInt = 0; displayArgs[4].vBool = true;
        Call(def, a, "RegisterDisplay", 5, displayArgs);
        Check(displayCache.size() == 1 && displayCache[0].id.resId == 0 && displayCache[0].id.endpointId == 7);
        Check(displayCache[0].width == 2 && displayCache[0].height == 1 && displayCache[0].hardware == CTLPI_DISPLAY_HARDWARE_RGB_LED);
        Check(displayCache[0].frameFormat == CTLPI_DISPLAY_FORMAT_SRGB888 && !displayCache[0].GetIdentifyFrame);
        bool duplicateRejected = false;
        try { Call(def, b, "RegisterDisplay", 5, displayArgs); } catch (...) { duplicateRejected = true; }
        Check(duplicateRejected && displayCache.size() == 1);
        displayArgs[3].vInt = 1; displayArgs[4].vBool = false;
        Call(def, b, "RegisterDisplay", 5, displayArgs);
        Check(displayCache.size() == 2);
        // The discovery count must include entries beyond the caller's capacity.
        DisplaySrcId one{}; GetDisplaySrcMsg limited{1, 0, &one};
        Fire(std::string(CTLPI_NAMESPACE) + CTLPI_DISPLAY_GET_SRC_MSG, &limited);
        Check(limited.count == 2);
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
            auto sources = GetDisplays();
            for (const auto& source : sources) {
                auto frame = source.GetRenderFrame(source.callContext);
                auto pixels = static_cast<const uint8_t*>(frame.frame);
                if (source.id.resId == 0) Check(frame.frameId == 2 && std::vector<uint8_t>(pixels, pixels + 6) == std::vector<uint8_t>({10, 13, 0, 255, 128, 1}));
                else Check(frame.frameId == 0 && source.hardware == CTLPI_DISPLAY_HARDWARE_LCD_DISPLAY);
            }
            Call(def, a, "Disconnect"); Check(Call(def, b, "Connected").vBool);
            for (const auto& source : sources) if (source.id.resId == 0) Check(source.GetRenderFrame(source.callContext).frameId == 0);
            send(b, "trigger?name=still_connected");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (auto m : aa) Call(messageDef, m, "Release");
            for (auto m : bb) Call(messageDef, m, "Release");
        }
        Call(def, a, "Disconnect"); Call(def, b, "Disconnect");
        Call(def, a, "ClearDisplays"); Check(displayCache.size() == 1 && displayCache[0].id.resId == 1);
        Fire(std::string(VPXPI_NAMESPACE) + VPXPI_EVT_ON_GAME_END);
        Check(displayCache.empty() && displayChanges == 4);
        Call(def, a, "Release"); Call(def, b, "Release");
        BCPPluginUnload(); Check(classes.empty() && overrides.empty() && ids.empty() && subscriptions == 0);
        BCPPluginLoad(8, &api); BCPPluginUnload(); Check(classes.empty());
        std::cout << "Bindings OK\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
