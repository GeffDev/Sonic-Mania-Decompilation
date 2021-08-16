#include "RetroEngine.hpp"
#if RETRO_USE_MOD_LOADER

#include <filesystem>
#include <sstream>
#include <stdexcept>

int currentObjectID = 0;

#if RETRO_PLATFORM == RETRO_ANDROID
namespace fs = std::__fs::filesystem;
#else
namespace fs = std::filesystem;
#endif
#if RETRO_PLATFORM == RETRO_WIN
#include "Windows.h"
#undef GetObject // fuck you
#endif

#if RETRO_PLATFORM == RETRO_OSX || RETRO_PLATFORM == RETRO_LINUX || RETRO_PLATFORM == RETRO_ANDROID
#include <dlfcn.h>
#if RETRO_PLATFORM == RETRO_ANDROID
#define dlopen(file, flags) SDL_LoadObject(file)
#endif
#endif

std::vector<ModInfo> modList;
std::vector<ModCallback> modCallbackList[MODCB_MAX];

void *modFunctionTable[ModTable_Max];

#define addToModFunctionTable(id, func) modFunctionTable[id] = (void *)func;

void initModAPI()
{
    memset(modFunctionTable, 0, sizeof(modFunctionTable));

    addToModFunctionTable(ModTable_RegisterObject, ModRegisterObject);
    addToModFunctionTable(ModTable_GetGlobals, GetGlobals);
    addToModFunctionTable(ModTable_LoadModInfo, LoadModInfo);
    addToModFunctionTable(ModTable_AddModCallback, AddModCallback);
    addToModFunctionTable(ModTable_AddPublicFunction, AddPublicFunction);
    addToModFunctionTable(ModTable_GetPublicFunction, GetPublicFunction);
    addToModFunctionTable(ModTable_GetModPath, GetModPath);
    addToModFunctionTable(ModTable_GetSettingsBool, GetSettingsBool);
    addToModFunctionTable(ModTable_GetSettingsInt, GetSettingsInteger);
    addToModFunctionTable(ModTable_GetSettingsString, GetSettingsString);
    addToModFunctionTable(ModTable_ForeachSetting, ForeachSetting);
    addToModFunctionTable(ModTable_ForeachSettingCategory, ForeachSettingCategory);
    addToModFunctionTable(ModTable_SetSettingsBool, SetSettingsBool);
    addToModFunctionTable(ModTable_SetSettingsInt, SetSettingsInteger);
    addToModFunctionTable(ModTable_SetSettingsString, SetSettingsString);
    addToModFunctionTable(ModTable_SaveSettings, SaveSettings);
    addToModFunctionTable(ModTable_Super, Super);
    addToModFunctionTable(ModTable_GetObject, GetObject);

    loadMods();
}

void sortMods()
{
    std::sort(modList.begin(), modList.end(), [](ModInfo a, ModInfo b) {
        if (!(a.active && b.active))
            return a.active;
        // keep it unsorted i guess
        return false;
    });
}

void loadMods()
{
    modList.clear();
    for (int c = 0; c < MODCB_MAX; ++c) modCallbackList[c].clear();
    char modBuf[0x100];
    sprintf(modBuf, "%smods/", userFileDir);
    fs::path modPath(modBuf);

    if (fs::exists(modPath) && fs::is_directory(modPath)) {
        std::string mod_config = modPath.string() + "/modconfig.ini";
        FileIO *configFile     = fOpen(mod_config.c_str(), "r");
        if (configFile) {
            fClose(configFile);
            char buffer[0x100];
            auto ini = iniparser_load(mod_config.c_str());

            int c             = iniparser_getsecnkeys(ini, "Mods");
            const char **keys = new const char *[c];
            iniparser_getseckeys(ini, "Mods", keys);

            for (int m = 0; m < c; ++m) {
                bool active = false;
                ModInfo info;
                if (loadMod(&info, modPath.string(), std::string(keys[m] + 5), iniparser_getboolean(ini, keys[m], false)))
                    modList.push_back(info);
            }
        }

        try {
            auto rdi = fs::directory_iterator(modPath);
            for (auto de : rdi) {
                if (de.is_directory()) {
                    fs::path modDirPath = de.path();

                    ModInfo info;

                    std::string modDir            = modDirPath.string().c_str();
                    const std::string mod_inifile = modDir + "/mod.ini";
                    std::string folder            = modDirPath.filename().string();

                    bool flag = true;
                    for (int m = 0; m < modList.size(); ++m) {
                        if (modList[m].folder == folder) {
                            flag = false;
                            break;
                        }
                    }

                    if (flag && loadMod(&info, modPath.string(), modDirPath.filename().string(), false))
                        modList.push_back(info);
                }
            }
        } catch (fs::filesystem_error fe) {
            printLog(PRINT_ERROR, "Mods Folder Scanning Error: %s", fe.what());
        }
    }
    sortMods();
}

bool32 loadMod(ModInfo *info, std::string modsPath, std::string folder, bool32 active)
{
    if (!info)
        return false;

    printLog(PRINT_NORMAL, "[MOD] Trying to load mod %s...", folder.c_str());

    info->fileMap.clear();
    info->name    = "";
    info->desc    = "";
    info->author  = "";
    info->version = "";
    info->folder  = "";
    info->active  = false;

    const std::string modDir = modsPath + "/" + folder;

    FileIO *f = fOpen((modDir + "/mod.ini").c_str(), "r");
    if (f) {
        fClose(f);
        auto ini = iniparser_load((modDir + "/mod.ini").c_str());

        info->folder = folder;
        info->active = active;

        char infoBuf[0x100];
        info->name    = iniparser_getstring(ini, ":Name", "Unnamed Mod");
        info->desc    = iniparser_getstring(ini, ":Description", "");
        info->author  = iniparser_getstring(ini, ":Author", "Unknown Author");
        info->version = iniparser_getstring(ini, ":Version", "1.0.0");

        // DATA
        fs::path dataPath(modDir + "/Data");

        if (fs::exists(dataPath) && fs::is_directory(dataPath)) {
            try {
                auto data_rdi = fs::recursive_directory_iterator(dataPath);
                for (auto data_de : data_rdi) {
                    if (data_de.is_regular_file()) {
                        char modBuf[0x100];
                        strcpy(modBuf, data_de.path().string().c_str());
                        char folderTest[4][0x10] = {
                            "Data/",
                            "Data\\",
                            "data/",
                            "data\\",
                        };
                        int tokenPos = -1;
                        for (int i = 0; i < 4; ++i) {
                            tokenPos = std::string(modBuf).find(folderTest[i], 0);
                            if (tokenPos >= 0)
                                break;
                        }

                        if (tokenPos >= 0) {
                            char buffer[0x80];
                            for (int i = strlen(modBuf); i >= tokenPos; --i) {
                                buffer[i - tokenPos] = modBuf[i] == '\\' ? '/' : modBuf[i];
                            }

                            // printLog(modBuf);
                            std::string path(buffer);
                            std::string modPath(modBuf);
                            char pathLower[0x100];
                            memset(pathLower, 0, sizeof(char) * 0x100);
                            for (int c = 0; c < path.size(); ++c) {
                                pathLower[c] = tolower(path.c_str()[c]);
                            }

                            info->fileMap.insert(std::pair<std::string, std::string>(pathLower, modBuf));
                        }
                    }
                }
            } catch (fs::filesystem_error fe) {
                printLog(PRINT_ERROR, "Data Folder Scanning Error: %s", fe.what());
            }
        }
        // LOGIC
        std::string logic(iniparser_getstring(ini, ":LogicFile", ""));
        if (logic.length() && info->active) {
            std::istringstream stream(logic);
            std::string buf;
            while (std::getline(stream, buf, ',')) {
                int mode = 0;
#define ENDS_WITH(str) buf.length() > strlen(str) && !buf.compare(buf.length() - strlen(str), strlen(str), std::string(str))
#if RETRO_PLATFORM == RETRO_WIN
                if (ENDS_WITH(".dll"))
                    mode = 1;
#elif RETRO_PLATFORM == RETRO_OSX
                if (ENDS_WITH(".dylib"))
                    mode = 1;
#elif RETRO_PLATFORM == RETRO_LINUX || RETRO_PLATFORM == RETRO_ANDROID
                if (ENDS_WITH(".so"))
                    mode = 1;
#else
                if (false)
                    ;
#endif
#if RETRO_USE_PYTHON
                else if (ENDS_WITH(".py"))
                    mode = 2;
#endif
#undef ENDS_WITH
                fs::path file;

                if (!mode) {
                    // autodec
                    std::string autodec = "";
#if RETRO_PLATFORM == RETRO_WIN
                    autodec = ".dll";
#elif RETRO_PLATFORM == RETRO_OSX
                    autodec = ".dylib";
#elif RETRO_PLATFORM == RETRO_LINUX || RETRO_PLATFORM == RETRO_ANDROID
                    autodec = ".so";
#endif
                    file = fs::path(modDir + "/" + buf + autodec);
                    if (fs::exists(file))
                        mode = 1;
                }
#if RETRO_USE_PYTHON
                if (!mode) {
                    file = fs::path(modDir + "/" + buf + ".py");
                    if (fs::exists(file))
                        mode = 2;
                }
#endif
                if (!mode) {
                    iniparser_freedict(ini);
                    return false;
                }

                bool linked = false;
                if (mode == 1) {
#if RETRO_PLATFORM == RETRO_WIN
                    HMODULE hLibModule = LoadLibraryA(file.string().c_str());

                    if (hLibModule) {
                        modLink linkGameLogic = (modLink)GetProcAddress(hLibModule, "LinkModLogic");
                        if (linkGameLogic) {
                            info->linkModLogic.push_back(linkGameLogic);
                            linked = true;
                        }
                    }
#elif RETRO_PLATFORM == RETRO_OSX || RETRO_PLATFORM == RETRO_LINUX || RETRO_PLATFORM == RETRO_ANDROID
                    void *link_handle = (void *)dlopen(file.string().c_str(), RTLD_LOCAL | RTLD_LAZY);

                    if (link_handle) {
                        modLink linkGameLogic = (modLink)dlsym(link_handle, "LinkModLogic");
                        if (linkGameLogic) {
                            info->linkModLogic.push_back(linkGameLogic);
                            linked = true;
                        }
                    }
#endif
                }
                // TODO: mode 2 (python)
                if (!linked) {
                    iniparser_freedict(ini);
                    return false;
                }
            }
        }
        // SETTINGS
        FileIO *set = fOpen((modDir + "/modSettings.ini").c_str(), "r");
        if (set) {
            fClose(set);
            using namespace std;
            auto ini = iniparser_load((modDir + "/modSettings.ini").c_str());
            int sec  = iniparser_getnsec(ini);
            if (sec) {
                for (int i = 0; i < sec; ++i) {
                    const char *secn  = iniparser_getsecname(ini, i);
                    int len           = iniparser_getsecnkeys(ini, secn);
                    const char **keys = new const char *[len];
                    iniparser_getseckeys(ini, secn, keys);
                    map<string, string> secset;
                    for (int j = 0; j < len; ++j)
                        secset.insert(pair<string, string>(keys[j] + strlen(secn) + 1, iniparser_getstring(ini, keys[j], "")));
                    info->settings.insert(pair<string, map<string, string>>(secn, secset));
                }
            }
            else {
                // either you use categories or you don't, i don't make the rules
                map<string, string> secset;
                for (int j = 0; j < ini->size; ++j) secset.insert(pair<string, string>(ini->key[j] + 1, ini->val[j]));
                info->settings.insert(pair<string, map<string, string>>("", secset));
            }
            iniparser_freedict(ini);
        }

        printLog(PRINT_NORMAL, "[MOD] Loaded mod %s! Active: %s", folder.c_str(), active ? "Y" : "N");

        iniparser_freedict(ini);
        return true;
    }
    return false;
}

void saveMods()
{
    char modBuf[0x100];
    sprintf(modBuf, "%smods/", userFileDir);
    fs::path modPath(modBuf);

    sortMods();

    printLog(PRINT_NORMAL, "[MOD] Saving mods...");

    if (fs::exists(modPath) && fs::is_directory(modPath)) {
        std::string mod_config = modPath.string() + "/modconfig.ini";
        FileIO *file           = fOpen(mod_config.c_str(), "w");

        writeText(file, "[Mods]\n");

        for (int m = 0; m < modList.size(); ++m) {
            ModInfo *info = &modList[m];
            SaveSettings(info->folder.c_str());

            writeText(file, "%s=%c\n", info->folder.c_str(), info->active ? 'y' : 'n');
        }
        fClose(file);
    }
}

void RunModCallbacks(int callbackID, void *data)
{
    if (callbackID < 0 || callbackID >= MODCB_MAX)
        return;

    for (int c = 0; c < modCallbackList[callbackID].size(); ++c) {
        if (modCallbackList[callbackID][c])
            modCallbackList[callbackID][c](data);
    }
}

// Mod API
bool32 LoadModInfo(const char *folder, TextInfo *name, TextInfo *description, TextInfo *version, bool32 *active)
{
    for (int m = 0; m < modList.size(); ++m) {
        if (modList[m].folder == folder) {
            if (description)
                SetText(description, (char *)modList[m].desc.c_str(), false);
            if (description)
                SetText(description, (char *)modList[m].desc.c_str(), false);
            if (version)
                SetText(description, (char *)modList[m].version.c_str(), false);
            if (active)
                *active = modList[m].active;

            return true;
        }
    }
    return false;
}

void AddModCallback(int callbackID, ModCallback callback)
{
    if (callbackID < 0 || callbackID >= MODCB_MAX)
        return;

    modCallbackList[callbackID].push_back(callback);
}

void *AddPublicFunction(const char *folder, const char *functionName, void *functionPtr)
{
    for (int m = 0; m < modList.size(); ++m) {
        if (modList[m].folder == folder) {
            ModPublicFunctionInfo info;
            info.name = functionName;
            info.ptr  = functionPtr;
            modList[m].functionList.push_back(info);
        }
    }
    return NULL;
}

void *GetPublicFunction(const char *folder, const char *functionName)
{
    for (int m = 0; m < modList.size(); ++m) {
        if (!modList[m].active)
            break;
        if (modList[m].folder == folder) {
            for (int f = 0; f < modList[m].functionList.size(); ++f) {
                if (modList[m].functionList[f].name == functionName)
                    return modList[m].functionList[f].ptr;
            }
        }
    }
    return NULL;
}

void GetModPath(const char *id, TextInfo *result)
{
    int m;
    for (m = 0; m < modList.size(); ++m)
        if (modList[m].active && modList[m].folder == id)
            break;
    if (m == modList.size())
        return;

    char buf[0x200];
    sprintf(buf, "%smods/%s", userFileDir, id);
    SetText(result, buf, strlen(buf));
}

std::string GetModPath_i(const char *id)
{
    int m;
    for (m = 0; m < modList.size(); ++m)
        if (modList[m].active && modList[m].folder == id)
            break;
    if (m == modList.size())
        return std::string();

    return std::string(userFileDir) + "mods/" + id;
}

std::string GetSettingsValue(const char *id, const char *key)
{
    std::string skey(key);
    if (!strchr(key, ':'))
        skey = std::string(":") + key;

    std::string cat  = skey.substr(0, skey.find(":"));
    std::string rkey = skey.substr(skey.find(":") + 1);

    for (int m = 0; m < modList.size(); ++m) {
        if (!modList[m].active)
            break;
        if (modList[m].folder == id) {
            try {
                return modList[m].settings.at(cat).at(rkey);
            } catch (std::out_of_range) {
                return std::string();
            }
        }
    }
    return std::string();
}

bool32 GetSettingsBool(const char *id, const char *key, bool32 fallback)
{
    std::string v = GetSettingsValue(id, key);
    if (!v.length()) {
        SetSettingsBool(id, key, fallback);
        return fallback;
    }
    char first = v.at(0);
    if (first == 'y' || first == 'Y' || first == 't' || first == 'T' || (first = GetSettingsInteger(id, key, 0)))
        return true;
    if (first == 'n' || first == 'N' || first == 'f' || first == 'F' || !first)
        return false;
    SetSettingsBool(id, key, fallback);
    return fallback;
}

int GetSettingsInteger(const char *id, const char *key, int fallback)
{
    std::string v = GetSettingsValue(id, key);
    if (!v.length()) {
        SetSettingsInteger(id, key, fallback);
        return fallback;
    }
    try {
        return std::stoi(v, nullptr, 0);
    } catch (...) {
        SetSettingsInteger(id, key, fallback);
        return fallback;
    }
}

void GetSettingsString(const char *id, const char *key, TextInfo *result, const char *fallback)
{
    std::string v = GetSettingsValue(id, key);
    if (!v.length()) {
        SetText(result, (char *)fallback, strlen(fallback));
        SetSettingsString(id, key, result);
        return;
    }
    SetText(result, (char *)v.c_str(), v.length());
}

bool32 ForeachSettingCategory(const char *id, TextInfo *textInfo)
{
    if (!textInfo)
        return false;
    using namespace std;
    map<string, map<string, string>> settings;
    for (int m = 0; m < modList.size(); ++m) {
        if (!modList[m].active)
            break;
        if (modList[m].folder == id) {
            settings = modList[m].settings;
        }
    }
    if (!settings.size())
        return false;

    if (textInfo->text)
        ++foreachStackPtr->id;
    else {
        ++foreachStackPtr;
        foreachStackPtr->id = 0;
    }
    int sid = 0;
    string cat;
    bool32 set = false;
    if (settings[""].size() && foreachStackPtr->id == sid++) {
        set = true;
        cat = "";
    }
    if (!set) {
        for (pair<string, map<string, string>> kv : settings) {
            if (!kv.first.length())
                continue;
            if (kv.second.size() && foreachStackPtr->id == sid++) {
                set = true;
                cat = kv.first;
                break;
            }
        }
    }
    if (!set) {
        foreachStackPtr--;
        return false;
    }
    SetText(textInfo, (char *)cat.c_str(), cat.length());
    return true;
}

bool32 ForeachSetting(const char *id, TextInfo *textInfo)
{
    if (!textInfo)
        return false;
    using namespace std;
    map<string, map<string, string>> settings;
    for (int m = 0; m < modList.size(); ++m) {
        if (!modList[m].active)
            break;
        if (modList[m].folder == id) {
            settings = modList[m].settings;
        }
    }
    if (!settings.size())
        return false;

    if (textInfo->text)
        ++foreachStackPtr->id;
    else {
        ++foreachStackPtr;
        foreachStackPtr->id = 0;
    }
    int sid = 0;
    string key, cat;
    if (settings[""].size()) {
        for (pair<string, string> pair : settings[""]) {
            if (foreachStackPtr->id == sid++) {
                cat = "";
                key = pair.first;
                break;
            }
        }
    }
    if (!key.length()) {
        for (pair<string, map<string, string>> kv : settings) {
            if (!kv.first.length())
                continue;
            for (pair<string, string> pair : kv.second) {
                if (foreachStackPtr->id == sid++) {
                    cat = kv.first;
                    key = pair.first;
                    break;
                }
            }
        }
    }
    if (!key.length()) {
        foreachStackPtr--;
        return false;
    }
    string r = cat + ":" + key;
    SetText(textInfo, (char *)r.c_str(), r.length());
    return true;
}

void SetSettingsValue(const char *id, const char *key, std::string val)
{
    std::string skey(key);
    if (!strchr(key, ':'))
        skey = std::string(":") + key;

    std::string cat  = skey.substr(0, skey.find(":"));
    std::string rkey = skey.substr(skey.find(":") + 1);

    for (int m = 0; m < modList.size(); ++m) {
        if (!modList[m].active)
            break;
        if (modList[m].folder == id) {
            modList[m].settings[cat][rkey] = val;
            break;
        }
    }
}

void SetSettingsBool(const char *id, const char *key, bool32 val) { SetSettingsValue(id, key, val ? "Y" : "N"); }
void SetSettingsInteger(const char *id, const char *key, int val) { SetSettingsValue(id, key, std::to_string(val)); }
void SetSettingsString(const char *id, const char *key, TextInfo *val)
{
    char *buf = new char[val->textLength];
    GetCString(buf, val);
    SetSettingsValue(id, key, buf);
}

void SaveSettings(const char *id)
{
    using namespace std;
    for (int m = 0; m < modList.size(); ++m) {
        if (!modList[m].active)
            break;
        if (modList[m].folder == id) {
            if (!modList[m].settings.size())
                break;
            TextInfo path;
            FileIO *file = fOpen((GetModPath_i(id) + "/modSettings.ini").c_str(), "w");

            if (modList[m].settings[""].size()) {
                for (pair<string, string> pair : modList[m].settings[""]) writeText(file, "%s = %s\n", pair.first.c_str(), pair.second.c_str());
            }
            for (pair<string, map<string, string>> kv : modList[m].settings) {
                if (!kv.first.length())
                    continue;
                writeText(file, "\n[%s]\n", kv.first.c_str());
                for (pair<string, string> pair : kv.second) writeText(file, "%s = %s\n", pair.first.c_str(), pair.second.c_str());
            }
            fClose(file);
            printLog(PRINT_NORMAL, "[MOD] Saved mod settings for mod %s", id);
            return;
        }
    }
}

// i'm going to hell for this
// nvm im actually so proud of this func yall have no idea i'm insane
int superLevels = 1;
void Super(int objectID, ModSuper callback, void *data)
{
    ObjectInfo *super = &objectList[stageObjectIDs[objectID]];
    Object *before    = NULL;
    if (HASH_MATCH(super->hash, super->inherited->hash)) {
        for (int i = 0; i < superLevels; i++) {
            before = *super->type;
            if (!super->inherited) {
                superLevels = i;
                break;
            }
            // if overriding, force it all to be that object and don't set it back
            *super->inherited->type = *super->type;
            super                   = super->inherited;
        }
        ++superLevels;
    }
    else if (super->inherited) {
        // if we're just inheriting, set it back properly afterward
        before                  = *super->inherited->type;
        *super->inherited->type = *super->type;
        super                   = super->inherited;
        superLevels             = 1;
    }
    switch (callback) {
        case SUPER_CREATE:
            if (super->create)
                super->create(data);
            break;
        case SUPER_DRAW:
            if (super->draw)
                super->draw();
            break;
        case SUPER_UPDATE:
            if (super->update)
                super->update();
            break;
        case SUPER_STAGELOAD:
            if (super->stageLoad)
                super->stageLoad();
            break;
        case SUPER_LATEUPDATE:
            if (super->lateUpdate)
                super->lateUpdate();
            break;
        case SUPER_STATICUPDATE:
            if (super->staticUpdate)
                super->staticUpdate();
            break;
        case SUPER_SERIALIZE:
            if (super->serialize)
                super->serialize();
            break;
        case SUPER_EDITORLOAD:
            if (super->editorLoad)
                super->editorLoad();
            break;
        case SUPER_EDITORDRAW:
            if (super->editorDraw)
                super->editorDraw();
            break;
    }
    *super->type = before;
    superLevels  = 1;
}

void* GetGlobals() {
    return (void*)gameOptionsPtr;
}

void ModRegisterObject(Object **structPtr, const char *name, uint entitySize, uint objectSize, void (*update)(void), void (*lateUpdate)(void),
                       void (*staticUpdate)(void), void (*draw)(void), void (*create)(void *), void (*stageLoad)(void), void (*editorDraw)(void),
                       void (*editorLoad)(void), void (*serialize)(void), const char *inherited)
{
    int preCount = objectCount + 1;
    uint hash[4];
    GEN_HASH(name, hash);

    for (int i = 0; i < objectCount; ++i) {
        if (HASH_MATCH(objectList[i].hash, hash)) {
            objectCount = i;
            --preCount;
            if (!inherited)
                inherited = name;
            break;
        }
    }
    ObjectInfo *inherit = NULL;

    if (inherited) {
        uint hash[4];
        GEN_HASH(inherited, hash);
        for (int i = 0; i < preCount; ++i) {
            if (HASH_MATCH(objectList[i].hash, hash)) {
                inherit = new ObjectInfo(objectList[i]);
                break;
            }
        }
        if (inherit) {
            if (!update)
                update = []() { Super(sceneInfo.entity->objectID, SUPER_UPDATE, NULL); };
            if (!lateUpdate)
                lateUpdate = []() { Super(sceneInfo.entity->objectID, SUPER_LATEUPDATE, NULL); };
            if (!staticUpdate)
                staticUpdate = []() { Super(currentObjectID, SUPER_STATICUPDATE, NULL); };
            if (!draw)
                draw = []() { Super(sceneInfo.entity->objectID, SUPER_DRAW, NULL); };
            if (!stageLoad)
                stageLoad = []() { Super(currentObjectID, SUPER_STAGELOAD, NULL); };
            if (!serialize)
                serialize = []() { Super(currentObjectID, SUPER_SERIALIZE, NULL); };
            if (!editorDraw)
                editorDraw = []() { Super(currentObjectID, SUPER_EDITORDRAW, NULL); };
            if (!editorLoad)
                editorLoad = []() { Super(currentObjectID, SUPER_EDITORLOAD, NULL); };
            if (!create)
                create = [](void *data) { Super(sceneInfo.entity->objectID, SUPER_CREATE, data); };
        }
        else
            inherited = NULL;
    }

    RegisterObject(structPtr, name, entitySize, objectSize, update, lateUpdate, staticUpdate, draw, create, stageLoad, editorDraw, editorLoad,
                   serialize);

    if (inherited) {
        ObjectInfo *info = &objectList[objectCount - 1];
        info->inherited  = inherit;
        if (HASH_MATCH(info->hash, inherit->hash)) {
            // we override an obj and lets check for structPtr
            if (!info->type) {
                info->type       = inherit->type;
                info->objectSize = inherit->objectSize;
            }
        }
    }
    objectCount = preCount;
}

Object *GetObject(const char *name)
{
    if (int o = GetObjectByName(name))
        return *objectList[stageObjectIDs[o]].type;
    return NULL;
}
#endif