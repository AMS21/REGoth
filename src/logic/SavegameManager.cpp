#include "SavegameManager.h"
#include <utils/Utils.h>
#include <utils/logger.h>
#include <json/json.hpp>
#include <fstream>
#include "engine/GameEngine.h"

using json = nlohmann::json;
using namespace Engine;

/**
 * Gameengine-instance pointer
 */
Engine::GameEngine* gameEngine;

// Enures that all folders to save into the given savegame-slot exist
void ensureSavegameFolders(int idx)
{
    std::string userdata = Utils::getUserDataLocation();

	if(!Utils::mkdir(userdata))
		LogError() << "Failed to create userdata-directory at: " << userdata;

    std::string gameType = "/" + SavegameManager::gameSpecificSubFolderName();

    if (!Utils::mkdir(userdata + gameType))
        LogError() << "Failed to create gametype-directory at: " << userdata + gameType;

    if(!Utils::mkdir(SavegameManager::buildSavegamePath(idx)))
		LogError() << "Failed to create savegame-directory at: " << SavegameManager::buildSavegamePath(idx);
}

std::string SavegameManager::buildSavegamePath(int idx)
{
    std::string userdata = Utils::getUserDataLocation();
    return userdata + "/" + SavegameManager::gameSpecificSubFolderName() + "/savegame_" + std::to_string(idx);
}

std::vector<std::string> SavegameManager::getSavegameWorlds(int idx)
{
    std::vector<std::string> worlds;

    Utils::forEachFile(buildSavegamePath(idx), [&](const std::string& path, const std::string& name, const std::string& ext){

        // Check if file is empty
        if(Utils::getFileSize(path + "/" + name) == 0)
            return; // Empty, don't bother

        // Valid worldfile
        worlds.push_back(name);
    });

    return worlds;
}

void SavegameManager::clearSavegame(int idx)
{
    if(!isSavegameAvailable(idx))
        return; // Don't touch any files if we don't have to...

    Utils::forEachFile(buildSavegamePath(idx), [](const std::string& path, const std::string& name, const std::string& ext)
    {
        // Make sure this is a REGoth-file
        bool isRegothFile = Utils::endsWith(name, ".json") &&
                (Utils::startsWith(name, "regoth_")
                 || Utils::startsWith(name, "world_")
                 || Utils::startsWith(name, "player")
                 || Utils::startsWith(name, "dialogmanager")
                 || Utils::startsWith(name, "scriptengine"));

        if(!isRegothFile)
            return; // Better not touch that one

        // Empty the file
        FILE* f = fopen(path.c_str(), "w");
        if(!f)
        {
            LogWarn() << "Failed to clear file: " << path;
            return;
        }

        fclose(f);

    }, false); // For the love of god, dont recurse in case something really goes wrong!
}

bool SavegameManager::isSavegameAvailable(int idx)
{
    return Utils::getFileSize(buildSavegamePath(idx) + "/regoth_save.json") > 0;
}

bool SavegameManager::writeSavegameInfo(int idx, const SavegameInfo& info)
{
    std::string infoFile = buildSavegamePath(idx) + "/regoth_save.json";

    ensureSavegameFolders(idx);

    json j;
    j["version"] = info.LATEST_KNOWN_VERSION;
    j["name"] = info.name;
    j["world"] = info.world;
    j["timePlayed"] = info.timePlayed;

    LogInfo() << "Writing savegame-info: " << infoFile;

    // Save
    std::ofstream f(infoFile);

    if(!f.is_open())
    {
        LogWarn() << "Failed to save data! Could not open file: " << buildSavegamePath(idx) + "/regoth_save.json";
        return false;
    }

    f << j.dump(4);
    f.close();

    return true;
}
        
Engine::SavegameManager::SavegameInfo SavegameManager::readSavegameInfo(int idx)
{
    std::string info = buildSavegamePath(idx) + "/regoth_save.json";

    if(!Utils::getFileSize(info))
        return SavegameInfo();

    LogInfo() << "Reading savegame-info: " << info;

    std::string infoContents = Utils::readFileContents(info);
    json j = json::parse(infoContents); 

    unsigned int version = 0;
    // check can be removed if backwards compability with save games without version number is not needed anymore
    if (j.find("version") != j.end())
    {
        version = j["version"];
    }
    SavegameInfo o;
    o.version = version;
    o.name = j["name"];
    o.world = j["world"];
    o.timePlayed = j["timePlayed"];

    return o;
}

bool SavegameManager::writePlayer(int idx, const std::string& playerName, const nlohmann::json& player)
{
    return writeFileInSlot(idx, playerName + ".json", Utils::iso_8859_1_to_utf8(player.dump()));
}

std::string SavegameManager::readPlayer(int idx, const std::string& playerName)
{
    return SavegameManager::readFileInSlot(idx, playerName + ".json");
}

bool SavegameManager::writeWorld(int idx, const std::string& worldName, const nlohmann::json& world)
{
    return writeFileInSlot(idx, "world_" + worldName + ".json", Utils::iso_8859_1_to_utf8(world.dump(4)));
}

std::string SavegameManager::readWorld(int idx, const std::string& worldName)
{
    return SavegameManager::readFileInSlot(idx,  "world_" + worldName + ".json");
}
        
std::string SavegameManager::buildWorldPath(int idx, const std::string& worldName)
{
   return buildSavegamePath(idx) + "/world_" + worldName + ".json";
}

void Engine::SavegameManager::init(Engine::GameEngine& engine)
{
    gameEngine = &engine;
}

std::vector<std::shared_ptr<const std::string>> SavegameManager::gatherAvailableSavegames()
{
    int numSlots = maxSlots();

    std::vector<std::shared_ptr<const std::string>> names(numSlots, nullptr);

    // Try every slot
    for (int i = 0; i < numSlots; ++i)
    {
        if (isSavegameAvailable(i))
        {
            SavegameInfo info = readSavegameInfo(i);
            names[i] = std::make_shared<const std::string>(info.name);
        } 
    }
    // for log purpose only
    {
        std::vector<std::string> names2;
        for (auto& namePtr : names)
        {
            if (namePtr)
                names2.push_back(*namePtr);
            else
                names2.push_back("");
        }
        LogInfo() << "Available savegames: " << names2;
    }

    return names;
}

std::string Engine::SavegameManager::loadSaveGameSlot(int index) {
    ExcludeFrameTime exclude(*gameEngine);
    // Lock to number of savegames
    assert(index >= 0 && index < maxSlots());

    if(!isSavegameAvailable(index))
    {
        return "Savegame at slot " + std::to_string(index) + " not available!";
    }

    // Read general information about the saved game. Most importantly the world the player saved in
    SavegameInfo info = readSavegameInfo(index);

    std::string worldFileData = SavegameManager::readWorld(index, info.world);
    // Sanity check, if we really got a safe for this world. Otherwise we would end up in the fresh version
    // if it was missing. Also, IF the player saved there, there should be a save for this.
    if(worldFileData.empty())
    {
        return "Target world-file invalid: " + buildWorldPath(index, info.world);
    }
    json worldJson = json::parse(worldFileData);
    // TODO catch json exception when emtpy file is parsed or parser crashes
    json scriptEngine = json::parse(SavegameManager::readFileInSlot(index, "scriptengine.json"));
    json dialogManager = json::parse(SavegameManager::readFileInSlot(index, "dialogmanager.json"));

    gameEngine->resetSession();
    gameEngine->getSession().setCurrentSlot(index);
    // TODO import scriptEngine and DialogManager
    Handle::WorldHandle worldHandle = gameEngine->getSession().addWorld("", worldJson, scriptEngine, dialogManager);
    if (worldHandle.isValid())
    {
        gameEngine->getSession().setMainWorld(worldHandle);
        json playerJson = json::parse(readPlayer(index, "player"));
        gameEngine->getMainWorld().get().importVobAndTakeControl(playerJson);
        gameEngine->getGameClock().setTotalSeconds(info.timePlayed);
    }
    return "";
}

int Engine::SavegameManager::maxSlots() {
    switch(gameEngine->getBasicGameType())
    {
        case Daedalus::GameType::GT_Gothic1:
            return G1_MAX_SLOTS;
        case Daedalus::GameType::GT_Gothic2:
            return G2_MAX_SLOTS;
        default:
            return G2_MAX_SLOTS;
    }
}

void Engine::SavegameManager::saveToSaveGameSlot(int index, std::string savegameName) {
    assert(false); // deprecated
}

std::string Engine::SavegameManager::gameSpecificSubFolderName() {
    if (gameEngine->getBasicGameType() == Daedalus::GameType::GT_Gothic1)
        return  "Gothic";
    else
        return  "Gothic 2";
}

std::string Engine::SavegameManager::readFileInSlot(int idx, const std::string &relativePath) {
    std::string file = buildSavegamePath(idx) + "/" + relativePath;

    if(!Utils::getFileSize(file))
        return ""; // Not found or empty

    LogInfo() << "Reading save-file: " << file;
    return Utils::readFileContents(file);
}

bool Engine::SavegameManager::writeFileInSlot(int idx, const std::string& relativePath, const std::string& data) {
    std::string file = buildSavegamePath(idx) + "/" + relativePath;
    ensureSavegameFolders(idx);

    LogInfo() << "Writing save-file: " << file;

    std::ofstream f(file);
    if(!f.is_open())
    {
        LogWarn() << "Failed to save data! Could not open file: " + file;
        return false;
    }

    f << data;

    return true;
}

