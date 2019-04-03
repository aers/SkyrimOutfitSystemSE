#include "skse64_common/BranchTrampoline.h"
#include "skse64_common/skse_version.h"
#include "skse64/PluginAPI.h"

#include <ShlObj.h>

#include "version.h"
#include "ArmorAddonOverrideService.h"
#include "OutfitSystem.h"
#include "PlayerSkinning.h"


static PluginHandle g_pluginHandle = kPluginHandle_Invalid;
static SKSEPapyrusInterface* g_Papyrus = nullptr;
static SKSEMessagingInterface* g_Messaging = nullptr;
static SKSESerializationInterface* g_Serialization = nullptr;

static UInt32 g_pluginSerializationSignature = 'cOft';

void Callback_Messaging_SKSE(SKSEMessagingInterface::Message* message);
void Callback_Serialization_Save(SKSESerializationInterface* intfc);
void Callback_Serialization_Load(SKSESerializationInterface* intfc);

extern "C" {
bool SKSEPlugin_Query(const SKSEInterface* a_skse, PluginInfo* a_info)
{
    gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim Special Edition\\SKSE\\SkyrimOutfitSystemSE.log");
    gLog.SetPrintLevel(IDebugLog::kLevel_DebugMessage);
    gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);

    _MESSAGE("SkyrimOutfitSystemSE v%s", SKYRIMOUTFITSYSTEMSE_VERSION_VERSTRING);

    a_info->infoVersion = PluginInfo::kInfoVersion;
    a_info->name = "SkyrimOutfitSystemSE";
    a_info->version = 1;

    g_pluginHandle = a_skse->GetPluginHandle();

    if (a_skse->isEditor)
    {
        _FATALERROR("[FATAL ERROR] Loaded in editor, marking as incompatible!\n");
        return false;
    }

    if (a_skse->runtimeVersion != RUNTIME_VERSION_1_5_73)
    {
        _FATALERROR("[FATAL ERROR] Unsupported runtime version %08X!\n", a_skse->runtimeVersion);
        return false;
    }

    g_Messaging = static_cast<SKSEMessagingInterface*>(a_skse->QueryInterface(kInterface_Messaging));
    if (!g_Messaging)
    {
        _FATALERROR("[FATAL ERROR] Couldn't get messaging interface.");
        return false;
    }
    if (g_Messaging->interfaceVersion < SKSEMessagingInterface::kInterfaceVersion)
    {
        _FATALERROR("[FATAL ERROR] Messaging interface too old.");
        return false;
    }

    g_Serialization = static_cast<SKSESerializationInterface*>(a_skse->QueryInterface(kInterface_Serialization));
    if (!g_Serialization)
    {
        _FATALERROR("[FATAL ERROR] Couldn't get serialization interface.");
        return false;
    }
    if (g_Serialization->version < SKSESerializationInterface::kVersion)
    {
        _FATALERROR("[FATAL ERROR] Serialization interface too old.");
        return false;
    }

    if (!g_branchTrampoline.Create(1024 * 64))
    {
        _FATALERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
        return false;
    }

    if (!g_localTrampoline.Create(1024 * 64, nullptr))
    {
        _FATALERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
        return false;
    }

    return true;
}

void _RegisterAndEchoPapyrus(SKSEPapyrusInterface::RegisterFunctions callback, char* module) {
    bool status = g_Papyrus->Register(callback);
    if (status)
        _MESSAGE("Papyrus registration %s for %s.", "succeeded", module);
    else
        _MESSAGE("Papyrus registration %s for %s.", "FAILED", module);
};

bool SKSEPlugin_Load(const SKSEInterface* a_skse)
{
    _MESSAGE("loading");
    {  // Patches:
        OutfitSystem::ApplyPlayerSkinningHooks();
    }
    {  // Messaging callbacks.
        g_Messaging->RegisterListener(g_pluginHandle, "SKSE", Callback_Messaging_SKSE);
    }
    {  // Serialization callbacks.
        g_Serialization->SetUniqueID(g_pluginHandle, g_pluginSerializationSignature);
        //g_ISKSESerialization->SetRevertCallback  (g_pluginHandle, Callback_Serialization_Revert);
        g_Serialization->SetSaveCallback(g_pluginHandle, Callback_Serialization_Save);
        g_Serialization->SetLoadCallback(g_pluginHandle, Callback_Serialization_Load);
    }
    {  // Papyrus registrations
        g_Papyrus = (SKSEPapyrusInterface*)a_skse->QueryInterface(kInterface_Papyrus);
        _RegisterAndEchoPapyrus(OutfitSystem::RegisterPapyrus, "SkyrimOutfitSystemNativeFuncs");
    }
    return true;
}
};

void Callback_Messaging_SKSE(SKSEMessagingInterface::Message* message) {
    if (message->type == SKSEMessagingInterface::kMessage_PostLoad) {
    }
    else if (message->type == SKSEMessagingInterface::kMessage_PostPostLoad) {
    }
    else if (message->type == SKSEMessagingInterface::kMessage_DataLoaded) {
    }
    else if (message->type == SKSEMessagingInterface::kMessage_NewGame) {
        ArmorAddonOverrideService::GetInstance().reset();
    }
    else if (message->type == SKSEMessagingInterface::kMessage_PreLoadGame) {
        ArmorAddonOverrideService::GetInstance().reset(); // AAOS::load resets as well, but this is needed in case the save we're about to load doesn't have any AAOS data.
    }
};
void Callback_Serialization_Save(SKSESerializationInterface* intfc) {
    _MESSAGE("Writing savedata...");
    //
    if (intfc->OpenRecord(ArmorAddonOverrideService::signature, ArmorAddonOverrideService::kSaveVersion)) {
        try {
            auto& service = ArmorAddonOverrideService::GetInstance();
            service.save(intfc);
        }
        catch (const ArmorAddonOverrideService::save_error& exception) {
            _MESSAGE("Save FAILED for ArmorAddonOverrideService.");
            _MESSAGE(" - Exception string: %s", exception.what());
        }
    }
    else
        _MESSAGE("Save FAILED for ArmorAddonOverrideService. Record didn't open.");
    //
    _MESSAGE("Saving done!");
}
void Callback_Serialization_Load(SKSESerializationInterface* intfc) {
    _MESSAGE("Loading savedata...");
    //
    UInt32 type;    // This IS correct. A UInt32 and a four-character ASCII string have the same length (and can be read interchangeably, it seems).
    UInt32 version;
    UInt32 length;
    bool   error = false;
    //
    while (!error && intfc->GetNextRecordInfo(&type, &version, &length)) {
        switch (type) {
        case ArmorAddonOverrideService::signature:
            try {
                auto& service = ArmorAddonOverrideService::GetInstance();
                service.load(intfc, version);
            }
            catch (const ArmorAddonOverrideService::load_error& exception) {
                _MESSAGE("Load FAILED for ArmorAddonOverrideService.");
                _MESSAGE(" - Exception string: %s", exception.what());
            }
            break;
        default:
            _MESSAGE("Loading: Unhandled type %c%c%c%c", (char)(type >> 0x18), (char)(type >> 0x10), (char)(type >> 0x8), (char)type);
            error = true;
            break;
        }
    }
    //
    _MESSAGE("Loading done!");
}