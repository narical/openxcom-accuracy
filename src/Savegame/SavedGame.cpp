/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "SavedGame.h"
#include <sstream>
#include <set>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <ctime>
#include "../Engine/Yaml.h"
#include "../version.h"
#include "../Engine/Logger.h"
#include "../Mod/Mod.h"
#include "../Engine/RNG.h"
#include "../Engine/Exception.h"
#include "../Engine/Options.h"
#include "../Engine/CrossPlatform.h"
#include "../Engine/ScriptBind.h"
#include "SavedBattleGame.h"
#include "SerializationHelper.h"
#include "GameTime.h"
#include "Country.h"
#include "Base.h"
#include "Craft.h"
#include "EquipmentLayoutItem.h"
#include "Region.h"
#include "Ufo.h"
#include "Waypoint.h"
#include "../Mod/RuleResearch.h"
#include "ResearchProject.h"
#include "ItemContainer.h"
#include "Soldier.h"
#include "Transfer.h"
#include "../Mod/RuleManufacture.h"
#include "../Mod/RuleBaseFacility.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleSoldierTransformation.h"
#include "Production.h"
#include "MissionSite.h"
#include "AlienBase.h"
#include "AlienStrategy.h"
#include "AlienMission.h"
#include "GeoscapeEvent.h"
#include "../Mod/RuleCountry.h"
#include "../Mod/RuleRegion.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/SoldierNamePool.h"
#include "BaseFacility.h"
#include "MissionStatistics.h"
#include "SoldierDeath.h"
#include "SoldierDiary.h"
#include "ResearchDiary.h"
#include "../Mod/AlienRace.h"
#include "RankCount.h"

namespace OpenXcom
{

const std::string SavedGame::AUTOSAVE_GEOSCAPE = "_autogeo_.asav",
				  SavedGame::AUTOSAVE_BATTLESCAPE = "_autobattle_.asav",
				  SavedGame::QUICKSAVE = "_quick_.asav";

namespace
{

bool researchLess(const RuleResearch *a, const RuleResearch *b)
{
	return std::less<const RuleResearch *>{}(a, b);
}

template<typename T>
void sortReserchVector(std::vector<T> &vec)
{
	std::sort(vec.begin(), vec.end(), researchLess);
}

bool haveReserchVector(const std::vector<const RuleResearch*> &vec, const RuleResearch *res)
{
	auto find = std::lower_bound(vec.begin(), vec.end(), res, researchLess);
	return find != vec.end() && *find == res;
}

bool haveReserchVector(const std::vector<const RuleResearch*> &vec,  const std::string &res)
{
	auto find = std::find_if(vec.begin(), vec.end(), [&](const RuleResearch* r){ return r->getName() == res; });
	return find != vec.end();
}

}

/**
 * Initializes a brand new saved game according to the specified difficulty.
 */
SavedGame::SavedGame() :
	_difficulty(DIFF_BEGINNER), _end(END_NONE), _ironman(false), _globeLon(0.0), _globeLat(0.0), _globeZoom(0),
	_battleGame(0), _previewBase(nullptr), _debug(false), _warned(false),
	_togglePersonalLight(true), _toggleNightVision(false), _toggleBrightness(0),
	_monthsPassed(-1), _daysPassed(0), _vehiclesLost(0), _selectedBase(0), _visibleBasesIndex(0), _autosales(), _disableSoldierEquipment(false), _alienContainmentChecked(false)
{
	_time = new GameTime(6, 1, 1, 1999, 12, 0, 0);
	_alienStrategy = new AlienStrategy();
	_funds.push_back(0);
	_maintenance.push_back(0);
	_researchScores.push_back(0);
	_incomes.push_back(0);
	_expenditures.push_back(0);
	_lastselectedArmor="STR_NONE_UC";

	for (int j = 0; j < MAX_CRAFT_LOADOUT_TEMPLATES; ++j)
	{
		_globalCraftLoadout[j] = new ItemContainer();
	}
}

/**
 * Deletes the game content from memory.
 */
SavedGame::~SavedGame()
{
	delete _time;
	for (auto* country : _countries)
	{
		delete country;
	}
	for (auto* region : _regions)
	{
		delete region;
	}
	for (auto* xbase : _bases)
	{
		delete xbase;
	}
	delete _previewBase;
	for (auto* ufo : _ufos)
	{
		delete ufo;
	}
	for (auto* wp : _waypoints)
	{
		delete wp;
	}
	for (auto* site : _missionSites)
	{
		delete site;
	}
	for (auto* ab : _alienBases)
	{
		delete ab;
	}
	delete _alienStrategy;
	for (auto* am : _activeMissions)
	{
		delete am;
	}
	for (auto* ge : _geoscapeEvents)
	{
		delete ge;
	}
	for (auto* soldier : _deadSoldiers)
	{
		delete soldier;
	}
	for (int j = 0; j < Options::oxceMaxEquipmentLayoutTemplates; ++j)
	{
		for (auto* entry : _globalEquipmentLayout[j])
		{
			delete entry;
		}
	}
	for (int j = 0; j < MAX_CRAFT_LOADOUT_TEMPLATES; ++j)
	{
		delete _globalCraftLoadout[j];
	}
	for (auto* ms : _missionStatistics)
	{
		delete ms;
	}
	for (auto* rde : _researchDiary)
	{
		delete rde;
	}

	delete _battleGame;
}

/**
 * Removes version number from a mod name, if any.
 * @param name Mod id from a savegame.
 * @return Sanitized mod name.
 */
std::string SavedGame::sanitizeModName(const std::string &name)
{
	size_t versionInfoBreakPoint = name.find(" ver: ");
	if (versionInfoBreakPoint == std::string::npos)
	{
		return name;
	}
	else
	{
		return name.substr(0, versionInfoBreakPoint);
	}
}

static bool _isCurrentGameType(const SaveInfo &saveInfo, const std::string &curMaster)
{
	bool matchMasterMod = false;
	if (saveInfo.mods.empty())
	{
		// if no mods listed in the savegame, this is an old-style
		// savegame.  assume "xcom1" as the game type.
		matchMasterMod = (curMaster == "xcom1");
	}
	else
	{
		for (const auto& modName : saveInfo.mods)
		{
			std::string name = SavedGame::sanitizeModName(modName);
			if (name == curMaster)
			{
				matchMasterMod = true;
				break;
			}
		}
	}

	if (!matchMasterMod)
	{
		Log(LOG_DEBUG) << "skipping save from inactive master: " << saveInfo.fileName;
	}

	return matchMasterMod;
}

/**
 * Gets all the info of the saves found in the user folder.
 * @param lang Loaded language.
 * @param autoquick Include autosaves and quicksaves.
 * @return List of saves info.
 */
std::vector<SaveInfo> SavedGame::getList(Language *lang, bool autoquick)
{
	std::vector<SaveInfo> info;
	std::string curMaster = Options::getActiveMaster();
	auto saves = CrossPlatform::getFolderContents(Options::getMasterUserFolder(), "sav");

	if (autoquick)
	{
		auto asaves = CrossPlatform::getFolderContents(Options::getMasterUserFolder(), "asav");
		saves.insert(saves.begin(), asaves.begin(), asaves.end());
	}
	for (const auto& tuple : saves)
	{
		const auto& filename = std::get<0>(tuple);
		try
		{
			SaveInfo saveInfo = getSaveInfo(filename, lang);
			if (!_isCurrentGameType(saveInfo, curMaster))
			{
				continue;
			}
			info.push_back(saveInfo);
		}
		catch (Exception &e)
		{
			Log(LOG_ERROR) << filename << ": " << e.what();
			continue;
		}
		catch (YAML::Exception& e)
		{
			Log(LOG_ERROR) << filename << ": " << e.what();
			continue;
		}
	}

	return info;
}

/**
 * Gets the info of a specific save file.
 * @param file Save filename.
 * @param lang Loaded language.
 */
SaveInfo SavedGame::getSaveInfo(const std::string &file, Language *lang)
{
	std::string fullname = Options::getMasterUserFolder() + file;
	YAML::YamlRootNodeReader reader(fullname, true);
	SaveInfo save;

	save.fileName = file;

	if (save.fileName == QUICKSAVE)
	{
		save.displayName = lang->getString("STR_QUICK_SAVE_SLOT");
		save.reserved = true;
	}
	else if (save.fileName == AUTOSAVE_GEOSCAPE)
	{
		save.displayName = lang->getString("STR_AUTO_SAVE_GEOSCAPE_SLOT");
		save.reserved = true;
	}
	else if (save.fileName.find(AUTOSAVE_GEOSCAPE) != std::string::npos)
	{
		GameTime time = GameTime(6, 1, 1, 1999, 12, 0, 0);
		if (reader["time"])
			time.load(reader["time"]);
		save.displayName = lang->getString("STR_AUTO_SAVE_GEOSCAPE_SLOT_WITH_NUMBER").arg(time.getDayString(lang));
		save.reserved = true;
	}
	else if (save.fileName == AUTOSAVE_BATTLESCAPE)
	{
		save.displayName = lang->getString("STR_AUTO_SAVE_BATTLESCAPE_SLOT");
		save.reserved = true;
	}
	else if (save.fileName.find(AUTOSAVE_BATTLESCAPE) != std::string::npos)
	{
		int turn = reader["turn"].readVal(0);
		save.displayName = lang->getString("STR_AUTO_SAVE_BATTLESCAPE_SLOT_WITH_NUMBER").arg(turn);
		save.reserved = true;
	}
	else
	{
		if (!reader.tryRead("name", save.displayName))
			save.displayName = CrossPlatform::noExt(file);
		save.reserved = false;
	}

	save.timestamp = CrossPlatform::getDateModified(fullname);
	std::pair<std::string, std::string> str = CrossPlatform::timeToString(save.timestamp);
	save.isoDate = str.first;
	save.isoTime = str.second;
	reader.tryRead("mods", save.mods);

	std::ostringstream details;
	if (reader["turn"])
	{
		details << lang->getString("STR_BATTLESCAPE") << ": " << lang->getString(reader["mission"].readVal<std::string>()) << ", ";
		details << lang->getString("STR_TURN").arg(reader["turn"].readVal<int>());
	}
	else
	{
		GameTime time = GameTime(6, 1, 1, 1999, 12, 0, 0);
		time.load(reader["time"]);
		details << lang->getString("STR_GEOSCAPE") << ": ";
		details << time.getDayString(lang) << " " << lang->getString(time.getMonthString()) << " " << time.getYear() << ", ";
		details << time.getHour() << ":" << std::setfill('0') << std::setw(2) << time.getMinute();
	}
	if (reader["ironman"].readVal(false))
	{
		details << " (" << lang->getString("STR_IRONMAN") << ")";
	}
	save.details = details.str();

	return save;
}

/**
 * Loads a saved game's contents from a YAML file.
 * @note Assumes the saved game is blank.
 * @param filename YAML filename.
 * @param mod Mod for the saved game.
 * @param lang Loaded language.
 */
void SavedGame::load(const std::string &filename, Mod *mod, Language *lang)
{
	std::string filepath = Options::getMasterUserFolder() + filename;
	YAML::YamlRootNodeReader documents(filepath, false, false);

	// Get brief save info
	const auto& header = documents[0];
	_time->load(header["time"]);
	header.readNode("name", _name, filename);
	header.tryRead("ironman", _ironman);

	// Get full save data
	const auto& reader = documents[1].useIndex();
	reader.tryRead("difficulty", _difficulty);
	reader.tryRead("end", _end);
	if (reader["rng"] && (_ironman || !Options::newSeedOnLoad))
		RNG::setSeed(reader["rng"].readVal<uint64_t>());
	reader.tryRead("monthsPassed", _monthsPassed);
	reader.tryRead("daysPassed", _daysPassed);
	reader.tryRead("vehiclesLost", _vehiclesLost);
	reader.tryRead("graphRegionToggles", _graphRegionToggles);
	reader.tryRead("graphCountryToggles", _graphCountryToggles);
	reader.tryRead("graphFinanceToggles", _graphFinanceToggles);
	reader.tryRead("funds", _funds);
	reader.tryRead("maintenance", _maintenance);
	reader.tryRead("userNotes", _userNotes);
	reader.tryRead("geoscapeDebugLog", _geoscapeDebugLog);
	reader.tryRead("researchScores", _researchScores);
	reader.tryRead("incomes", _incomes);
	reader.tryRead("expenditures", _expenditures);
	reader.tryRead("warned", _warned);
	reader.tryRead("togglePersonalLight", _togglePersonalLight);
	reader.tryRead("toggleNightVision", _toggleNightVision);
	reader.tryRead("toggleBrightness", _toggleBrightness);
	reader.tryRead("globeLon", _globeLon);
	reader.tryRead("globeLat", _globeLat);
	reader.tryRead("globeZoom", _globeZoom);
	reader.tryRead("ids", _ids);

	for (const auto& country : reader["countries"].children())
	{
		std::string type = country["type"].readVal<std::string>();
		if (mod->getCountry(type))
		{
			Country *c = new Country(mod->getCountry(type), false);
			c->load(country, mod->getScriptGlobal());
			_countries.push_back(c);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load country " << type;
		}
	}

	for (const auto& region : reader["regions"].children())
	{
		std::string type = region["type"].readVal<std::string>();
		if (mod->getRegion(type))
		{
			Region *r = new Region(mod->getRegion(type));
			r->load(region);
			_regions.push_back(r);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load region " << type;
		}
	}

	// Alien bases must be loaded before alien missions
	for (const auto& alienBase : reader["alienBases"].children())
	{
		std::string deployment = alienBase["deployment"].readVal<std::string>("STR_ALIEN_BASE_ASSAULT");
		if (mod->getDeployment(deployment))
		{
			AlienBase *b = new AlienBase(mod->getDeployment(deployment), 0);
			b->load(alienBase);
			_alienBases.push_back(b);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load deployment for alien base " << deployment;
		}
	}

	// Missions must be loaded before UFOs.
	for (const auto& alienMission : reader["alienMissions"].children())
	{
		std::string missionType = alienMission["type"].readVal<std::string>();
		if (mod->getAlienMission(missionType))
		{
			const RuleAlienMission &mRule = *mod->getAlienMission(missionType);
			AlienMission *mission = new AlienMission(mRule);
			mission->load(alienMission, *this, mod);
			_activeMissions.push_back(mission);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load mission " << missionType;
		}
	}

	for (const auto& ufo : reader["ufos"].children())
	{
		std::string type = ufo["type"].readVal<std::string>();
		if (mod->getUfo(type))
		{
			Ufo *u = new Ufo(mod->getUfo(type), 0);
			u->load(ufo, mod->getScriptGlobal(), *mod, *this);
			_ufos.push_back(u);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load UFO " << type;
		}
	}

	for (const auto& geoEvent : reader["geoscapeEvents"].children())
	{
		std::string eventName = geoEvent["name"].readVal<std::string>();
		if (mod->getEvent(eventName))
		{
			const RuleEvent &eventRule = *mod->getEvent(eventName);
			GeoscapeEvent *event = new GeoscapeEvent(eventRule);
			event->load(geoEvent);
			_geoscapeEvents.push_back(event);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load geoscape event " << eventName;
		}
	}

	for (const auto& waypoint : reader["waypoints"].children())
	{
		Waypoint *w = new Waypoint();
		w->load(waypoint);
		_waypoints.push_back(w);
	}

	// Backwards compatibility
	for (const auto& terrorSite : reader["terrorSites"].children())
	{
		std::string type = "STR_ALIEN_TERROR";
		std::string deployment = "STR_TERROR_MISSION";
		if (mod->getAlienMission(type) && mod->getDeployment(deployment))
		{
			MissionSite *m = new MissionSite(mod->getAlienMission(type), mod->getDeployment(deployment), nullptr);
			m->load(terrorSite);
			_missionSites.push_back(m);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load mission " << type << " deployment " << deployment;
		}
	}

	for (const auto& missionSite : reader["missionSites"].children())
	{
		std::string type = missionSite["type"].readVal<std::string>();
		std::string deployment = missionSite["deployment"].readVal<std::string>("STR_TERROR_MISSION");
		std::string alienWeaponDeploy = missionSite["missionCustomDeploy"].readVal<std::string>("");
		if (mod->getAlienMission(type) && mod->getDeployment(deployment))
		{
			MissionSite *m = new MissionSite(mod->getAlienMission(type), mod->getDeployment(deployment), mod->getDeployment(alienWeaponDeploy));
			m->load(missionSite);
			_missionSites.push_back(m);
			// link with UFO
			if (m->getUfoUniqueId() > 0)
			{
				Ufo* ufo = nullptr;
				for (auto* u : _ufos)
				{
					if (u->getUniqueId() == m->getUfoUniqueId())
					{
						ufo = u;
						break;
					}
				}
				if (ufo)
				{
					m->setUfo(ufo);
				}
			}
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load mission " << type << " deployment " << deployment;
		}
	}

	// Discovered Techs Should be loaded before Bases (e.g. for PSI evaluation)
	for (const auto& discovery : reader["discovered"].children())
	{
		std::string research = discovery.readVal<std::string>();
		if (RuleResearch* researchRule = mod->getResearch(research))
		{
			_discovered.push_back(researchRule);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load research " << research;
		}
	}
	sortReserchVector(_discovered);

	// Research Diary
	{
		std::string name;
		for (const auto& researchDiaryEntryReader : reader["researchDiary"].children())
		{
			researchDiaryEntryReader.readNode("name", name);
			// only valid topics are loaded
			if (RuleResearch* research = mod->getResearch(name, false))
			{
				ResearchDiaryEntry* entry = new ResearchDiaryEntry(research);
				entry->load(researchDiaryEntryReader, mod);
				_researchDiary.push_back(entry);
			}
		}
	}

	reader.tryRead("generatedEvents", _generatedEvents);
	loadUfopediaRuleStatus(reader["ufopediaRuleStatus"]);
	reader.tryRead("manufactureRuleStatus", _manufactureRuleStatus);
	reader.tryRead("researchRuleStatus", _researchRuleStatus);
	reader.tryRead("monthlyPurchaseLimitLog", _monthlyPurchaseLimitLog);
	reader.tryRead("hiddenPurchaseItems", _hiddenPurchaseItemsMap);
	reader.tryRead("customRuleCraftDeployments", _customRuleCraftDeployments);

	for (const auto& base : reader["bases"].children())
	{
		Base *b = new Base(mod);
		b->load(base, this, false);
		_bases.push_back(b);
	}

	// Finish loading crafts after bases (more specifically after all crafts) are loaded, because of references between crafts (i.e. friendly escorts)
	for (size_t i = 0; i < _bases.size(); ++i)
		_bases[i]->finishLoading(reader["bases"][i], this);

	// Finish loading UFOs after all craft and all other UFOs are loaded
	for (const auto& ufoReader : reader["ufos"].children())
	{
		int uniqueUfoId = ufoReader["uniqueId"].readVal(0);
		if (uniqueUfoId > 0)
		{
			Ufo *ufo = 0;
			for (auto* u : _ufos)
			{
				if (u->getUniqueId() == uniqueUfoId)
				{
					ufo = u;
					break;
				}
			}
			if (ufo)
			{
				ufo->finishLoading(ufoReader, *this);
			}
		}
	}

	for (const auto& popped : reader["poppedResearch"].children())
	{
		std::string id = popped.readVal<std::string>();
		if (mod->getResearch(id))
		{
			_poppedResearch.push_back(mod->getResearch(id));
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load popped research " << id;
		}
	}
	_alienStrategy->load(reader["alienStrategy"], mod);

	for (const auto& weHardlyKnewYe : reader["deadSoldiers"].children())
	{
		std::string type = weHardlyKnewYe["type"].readVal(mod->getSoldiersList().front());
		if (mod->getSoldier(type))
		{
			Soldier *soldier = new Soldier(mod->getSoldier(type), nullptr, 0 /*nationality*/);
			soldier->load(weHardlyKnewYe, mod, this, mod->getScriptGlobal());
			_deadSoldiers.push_back(soldier);
		}
		else
		{
			Log(LOG_ERROR) << "Failed to load dead soldier " << type;
		}
	}

	loadTemplates(reader, mod);

	for (const auto& missionStats : reader["missionStatistics"].children())
	{
		MissionStatistics *ms = new MissionStatistics();
		ms->load(missionStats);
		_missionStatistics.push_back(ms);
	}

	for (const auto& autoSale : reader["autoSales"].children())
	{
		std::string itype = autoSale.readVal<std::string>();
		if (mod->getItem(itype))
		{
			_autosales.insert(mod->getItem(itype));
		}
	}

	if (const YAML::YamlNodeReader& battle = reader["battleGame"])
	{
		_battleGame = new SavedBattleGame(mod, lang);
		_battleGame->load(battle, mod, this);
	}

	_scriptValues.load(reader, mod->getScriptGlobal());
}

void SavedGame::loadTemplates(const YAML::YamlNodeReader& reader, const Mod* mod)
{
	for (int j = 0; j < Options::oxceMaxEquipmentLayoutTemplates; ++j)
	{
		for (const auto& layout : reader[ryml::to_csubstr("globalEquipmentLayout" + std::to_string(j))].children())
		{
			try
			{
				_globalEquipmentLayout[j].push_back(new EquipmentLayoutItem(layout, mod));
			}
			catch (Exception& ex)
			{
				Log(LOG_ERROR) << "Error loading Layout: " << ex.what();
			}
		}
		if (const auto& layoutName = reader[ryml::to_csubstr("globalEquipmentLayoutName" + std::to_string(j))])
			_globalEquipmentLayoutName[j] = layoutName.readVal<std::string>();
		if (const auto& layoutArmor = reader[ryml::to_csubstr("globalEquipmentLayoutArmor" + std::to_string(j))])
			_globalEquipmentLayoutArmor[j] = layoutArmor.readVal<std::string>();
	}

	for (int j = 0; j < MAX_CRAFT_LOADOUT_TEMPLATES; ++j)
	{
		if (const auto& loadout = reader[ryml::to_csubstr("globalCraftLoadout" + std::to_string(j))])
			_globalCraftLoadout[j]->load(loadout, mod);
		if (const auto& loadoutName = reader[ryml::to_csubstr("globalCraftLoadoutName" + std::to_string(j))])
			_globalCraftLoadoutName[j] = loadoutName.readVal<std::string>();
	}
}

void SavedGame::loadUfopediaRuleStatus(const YAML::YamlNodeReader& reader)
{
	reader.tryReadVal(_ufopediaRuleStatus);
}

template <typename T, typename... Args>
void saveVector(YAML::YamlNodeWriter& writer, const std::vector<T*>& vector, const ryml::csubstr& key, Args... args)
{
	if (vector.empty())
		return;
	YAML::YamlNodeWriter sequenceWriter = writer[key];
	sequenceWriter.setAsSeq();
	for (const T* item : vector)
		item->save(sequenceWriter.write(), args...);
}

/**
 * Saves a saved game's contents to a YAML file.
 * @param filename YAML filename.
 */
void SavedGame::save(const std::string &filename, Mod *mod) const
{
	YAML::YamlRootNodeWriter headerWriter;
	headerWriter.setAsMap();
	// Saves the brief game info used in the saves list

	headerWriter.write("name", _name);
	headerWriter.write("version", OPENXCOM_VERSION_SHORT);
	headerWriter.write("engine", OPENXCOM_VERSION_ENGINE);
	std::string git_sha = OPENXCOM_VERSION_GIT;
	if (!git_sha.empty() && git_sha[0] == '.')
		git_sha.erase(0, 1);
	headerWriter.write("build", git_sha);
	_time->save(headerWriter["time"]);
	if (_battleGame != 0)
	{
		headerWriter.write("mission", _battleGame->getMissionType());
		headerWriter.write("target", _battleGame->getMissionTarget());
		headerWriter.write("craftOrBase", _battleGame->getMissionCraftOrBase()).setAsQuotedAndEscaped();
		headerWriter.write("turn", _battleGame->getTurn());
	}

	// only save mods that work with the current master
	std::vector<std::string> modsList;
	for (const auto* modInfo : Options::getActiveMods())
		modsList.push_back(modInfo->getId() + " ver: " + modInfo->getVersion());
	headerWriter.write("mods", modsList);

	if (_ironman)
		headerWriter.write("ironman", _ironman);

	// Saves the full game data to the save
	YAML::YamlRootNodeWriter writer(1000000); //1MB starting buffer
	writer.setAsMap();
	writer.write("difficulty", _difficulty);
	writer.write("end", _end);
	writer.write("monthsPassed", _monthsPassed);
	writer.write("daysPassed", _daysPassed);
	writer.write("vehiclesLost", _vehiclesLost);
	writer.write("graphRegionToggles", _graphRegionToggles);
	writer.write("graphCountryToggles", _graphCountryToggles);
	writer.write("graphFinanceToggles", _graphFinanceToggles);
	writer.write("rng", RNG::getSeed());
	writer.write("funds", _funds);
	writer.write("maintenance", _maintenance);
	writer.write("userNotes", _userNotes);
	if (Options::oxceGeoscapeDebugLogMaxEntries > 0 && _geoscapeDebugLog.size() > 0)
	{
		auto geoDebugLog = writer["geoscapeDebugLog"];
		geoDebugLog.setAsSeq();
		size_t lastEntriesToWrite = std::min(_geoscapeDebugLog.size(), (size_t)Options::oxceGeoscapeDebugLogMaxEntries);
		for (size_t j = _geoscapeDebugLog.size() - lastEntriesToWrite; j < _geoscapeDebugLog.size(); ++j)
			geoDebugLog.write(_geoscapeDebugLog[j]);
	}

	writer.write("researchScores", _researchScores);
	writer.write("incomes", _incomes);
	writer.write("expenditures", _expenditures);
	writer.write("warned", _warned);
	writer.write("togglePersonalLight", _togglePersonalLight);
	writer.write("toggleNightVision", _toggleNightVision);
	writer.write("toggleBrightness", _toggleBrightness);
	writer.write("globeLon", _globeLon);
	writer.write("globeLat", _globeLat);
	writer.write("globeZoom", _globeZoom);
	writer.write("ids", _ids);

	saveVector(writer, _countries, "countries", mod->getScriptGlobal());
	saveVector(writer, _regions, "regions");
	saveVector(writer, _bases, "bases");
	saveVector(writer, _waypoints, "waypoints");
	saveVector(writer, _missionSites, "missionSites");
	// Alien bases must be saved before alien missions.
	saveVector(writer, _alienBases, "alienBases");
	// Missions must be saved before UFOs, but after alien bases.
	saveVector(writer, _activeMissions, "alienMissions");
	// UFOs must be after missions
	saveVector(writer, _ufos, "ufos", mod->getScriptGlobal(), getMonthsPassed() == -1);
	saveVector(writer, _geoscapeEvents, "geoscapeEvents");
	if (!_discovered.empty())
	{
		auto discoveredWriter = writer["discovered"];
		discoveredWriter.setAsSeq();
		{
			auto discoveredCopy = _discovered;
			std::sort(discoveredCopy.begin(), discoveredCopy.end(), [&](const RuleResearch* a, const RuleResearch* b)
					  { return a->getName().compare(b->getName()) < 0; });
			for (const auto* research : discoveredCopy)
			{
				discoveredWriter.write(research->getName());
			}
		}
	}
	saveVector(writer, _researchDiary, "researchDiary");
	writer.write("poppedResearch", _poppedResearch,
		[](YAML::YamlNodeWriter& w, const RuleResearch* r)
		{ w.write(r->getName()); });
	writer.write("generatedEvents", _generatedEvents);
	writer.write("ufopediaRuleStatus", _ufopediaRuleStatus);
	writer.write("manufactureRuleStatus", _manufactureRuleStatus);
	writer.write("researchRuleStatus", _researchRuleStatus);
	writer.write("monthlyPurchaseLimitLog", _monthlyPurchaseLimitLog);
	writer.write("hiddenPurchaseItems", _hiddenPurchaseItemsMap);
	writer.write("customRuleCraftDeployments", _customRuleCraftDeployments);
	_alienStrategy->save(writer["alienStrategy"]);

	saveVector(writer, _deadSoldiers, "deadSoldiers", mod->getScriptGlobal());
	for (int j = 0; j < Options::oxceMaxEquipmentLayoutTemplates; ++j)
	{
		if (!_globalEquipmentLayout[j].empty())
			saveVector(writer, _globalEquipmentLayout[j], writer.saveString("globalEquipmentLayout" + std::to_string(j)));
		if (!_globalEquipmentLayoutName[j].empty())
			writer.write(writer.saveString("globalEquipmentLayoutName" + std::to_string(j)), _globalEquipmentLayoutName[j]);
		if (!_globalEquipmentLayoutArmor[j].empty())
			writer.write(writer.saveString("globalEquipmentLayoutArmor" + std::to_string(j)), _globalEquipmentLayoutArmor[j]);
	}
	for (int j = 0; j < MAX_CRAFT_LOADOUT_TEMPLATES; ++j)
	{
		if (!_globalCraftLoadout[j]->getContents()->empty())
			_globalCraftLoadout[j]->save(writer[writer.saveString("globalCraftLoadout" + std::to_string(j))]);
		if (!_globalCraftLoadoutName[j].empty())
			writer.write(writer.saveString("globalCraftLoadoutName" + std::to_string(j)), _globalCraftLoadoutName[j]);
	}
	if (Options::soldierDiaries)
		saveVector(writer, _missionStatistics, "missionStatistics");

	if (!_autosales.empty())
	{
		auto autoSales = writer["autoSales"];
		autoSales.setAsSeq();
		{
			std::vector<const RuleItem*> autosalesVector(_autosales.begin(), _autosales.end());
			std::sort(autosalesVector.begin(), autosalesVector.end(), [&](const RuleItem* a, const RuleItem* b)
				{ return a->getType().compare(b->getType()) < 0; });
			for (const auto* sale : autosalesVector)
			{
				autoSales.write(sale->getType());
			}
		}
	}
	// snapshot of the user options (just for debugging purposes)
	auto optionsWriter = writer["options"];
	optionsWriter.setAsMap();
	for (const auto& optionInfo : Options::getOptionInfo())
		optionInfo.save(optionsWriter);

	if (_battleGame)
		_battleGame->save(writer["battleGame"]);
	_scriptValues.save(writer.toBase(), mod->getScriptGlobal());

	// concatenate header + separator + body
	// per yaml standard, "bare documents" in a yaml "stream" can be separated by either a "document end" or "directives end" marker line
	YAML::YamlString headerString = headerWriter.emit();
	std::string directivesEndMarker = "---\n";
	YAML::YamlString bodyString = writer.emit();
	std::string finalString;
	finalString.reserve(headerString.yaml.size() + directivesEndMarker.size() + bodyString.yaml.size());
	finalString += headerString.yaml;
	finalString	+= directivesEndMarker;
	finalString += bodyString.yaml;

	std::string filepath = Options::getMasterUserFolder() + filename;
	if (!CrossPlatform::writeFile(filepath, finalString))
	{
		throw Exception("Failed to save " + filepath);
	}
}

/**
 * Returns the game's name shown in Save screens.
 * @return Save name.
 */
std::string SavedGame::getName() const
{
	return _name;
}

/**
 * Changes the game's name shown in Save screens.
 * @param name New name.
 */
void SavedGame::setName(const std::string &name)
{
	_name = name;
}

/**
 * Returns the game's difficulty level.
 * @return Difficulty level.
 */
GameDifficulty SavedGame::getDifficulty() const
{
	return _difficulty;
}

/**
 * Changes the game's difficulty to a new level.
 * @param difficulty New difficulty.
 */
void SavedGame::setDifficulty(GameDifficulty difficulty)
{
	_difficulty = difficulty;
}

/**
 * Returns the game's difficulty coefficient based
 * on the current level.
 * @return Difficulty coefficient.
 */
int SavedGame::getDifficultyCoefficient() const
{
	return Mod::DIFFICULTY_COEFFICIENT[std::min((int)_difficulty, 4)];
}

/**
 * Returns the game's sell price coefficient based
 * on the current difficulty level.
 * @return Sell price coefficient.
 */
int SavedGame::getSellPriceCoefficient() const
{
	return Mod::SELL_PRICE_COEFFICIENT[std::min((int)_difficulty, 4)];
}

/**
 * Returns the game's buy price coefficient based
 * on the current difficulty level.
 * @return Buy price coefficient.
 */
int SavedGame::getBuyPriceCoefficient() const
{
	return Mod::BUY_PRICE_COEFFICIENT[std::min((int)_difficulty, 4)];
}

/**
 * Returns the game's current ending.
 * @return Ending state.
 */
GameEnding SavedGame::getEnding() const
{
	return _end;
}

/**
 * Changes the game's current ending.
 * @param end New ending.
 */
void SavedGame::setEnding(GameEnding end)
{
	_end = end;
}

/**
 * Returns if the game is set to ironman mode.
 * Ironman games cannot be manually saved.
 * @return Tony Stark
 */
bool SavedGame::isIronman() const
{
	return _ironman;
}

/**
 * Changes if the game is set to ironman mode.
 * Ironman games cannot be manually saved.
 * @param ironman Tony Stark
 */
void SavedGame::setIronman(bool ironman)
{
	_ironman = ironman;
}

/**
 * Returns the player's current funds.
 * @return Current funds.
 */
int64_t SavedGame::getFunds() const
{
	return _funds.back();
}

/**
 * Returns the player's funds for the last 12 months.
 * @return funds.
 */
std::vector<int64_t> &SavedGame::getFundsList()
{
	return _funds;
}

/**
 * Changes the player's funds to a new value.
 * @param funds New funds.
 */
void SavedGame::setFunds(int64_t funds)
{
	if (_funds.back() > funds)
	{
		_expenditures.back() += _funds.back() - funds;
	}
	else
	{
		_incomes.back() += funds - _funds.back();
	}
	_funds.back() = funds;
}

/**
 * Returns the current longitude of the Geoscape globe.
 * @return Longitude.
 */
double SavedGame::getGlobeLongitude() const
{
	return _globeLon;
}

/**
 * Changes the current longitude of the Geoscape globe.
 * @param lon Longitude.
 */
void SavedGame::setGlobeLongitude(double lon)
{
	_globeLon = lon;
}

/**
 * Returns the current latitude of the Geoscape globe.
 * @return Latitude.
 */
double SavedGame::getGlobeLatitude() const
{
	return _globeLat;
}

/**
 * Changes the current latitude of the Geoscape globe.
 * @param lat Latitude.
 */
void SavedGame::setGlobeLatitude(double lat)
{
	_globeLat = lat;
}

/**
 * Returns the current zoom level of the Geoscape globe.
 * @return Zoom level.
 */
int SavedGame::getGlobeZoom() const
{
	return _globeZoom;
}

/**
 * Changes the current zoom level of the Geoscape globe.
 * @param zoom Zoom level.
 */
void SavedGame::setGlobeZoom(int zoom)
{
	_globeZoom = zoom;
}

/**
 * Gives the player his monthly funds, taking in account
 * all maintenance and profit costs.
 */
void SavedGame::monthlyFunding()
{
	auto countryFunding = getCountryFunding();
	auto baseMaintenance = getBaseMaintenance();
	_funds.back() += (countryFunding - baseMaintenance);
	_funds.push_back(_funds.back());
	_maintenance.back() = baseMaintenance;
	_maintenance.push_back(0);
	_incomes.push_back(countryFunding);
	_expenditures.push_back(baseMaintenance);
	_researchScores.push_back(0);

	if (_incomes.size() > 12)
		_incomes.erase(_incomes.begin());
	if (_expenditures.size() > 12)
		_expenditures.erase(_expenditures.begin());
	if (_researchScores.size() > 12)
		_researchScores.erase(_researchScores.begin());
	if (_funds.size() > 12)
		_funds.erase(_funds.begin());
	if (_maintenance.size() > 12)
		_maintenance.erase(_maintenance.begin());
}

/**
 * Returns the current time of the game.
 * @return Pointer to the game time.
 */
GameTime *SavedGame::getTime() const
{
	return _time;
}

/**
 * Changes the current time of the game.
 * @param time Game time.
 */
void SavedGame::setTime(const GameTime& time)
{
	_time = new GameTime(time);
}

/**
 * Returns the latest ID for the specified object
 * and increases it.
 * @param name Object name.
 * @return Latest ID number.
 */
int SavedGame::getId(const std::string &name)
{
	auto i = _ids.find(name);
	if (i != _ids.end())
	{
		return i->second++;
	}
	else
	{
		_ids[name] = 1;
		return _ids[name]++;
	}
}

/**
 * Returns the last used ID for the specified object.
 * @param name Object name.
 * @return Last used ID number.
 */
int SavedGame::getLastId(const std::string& name)
{
	auto i = _ids.find(name);
	if (i != _ids.end())
	{
		return std::max(1, i->second - 1);
	}
	else
	{
		return 0;
	}
}

/**
 * Increase a custom counter.
 * @param name Counter name.
 */
void SavedGame::increaseCustomCounter(const std::string& name)
{
	if (!name.empty())
	{
		auto i = _ids.find(name);
		if (i != _ids.end())
		{
			i->second++;
		}
		else
		{
			_ids[name] = 2; // not a typo
		}
	}
}

/**
 * Decrease a custom counter.
 * @param name Counter name.
 */
void SavedGame::decreaseCustomCounter(const std::string& name)
{
	if (!name.empty())
	{
		auto i = _ids.find(name);
		if (i != _ids.end())
		{
			// don't go below "zero" (which is saved as one)
			if (i->second > 1)
			{
				i->second--;
			}
		}
		else
		{
			_ids[name] = 1; // not a typo
		}
	}
}

/**
* Resets the list of unique object IDs.
* @param ids New ID list.
*/
const std::map<std::string, int> &SavedGame::getAllIds() const
{
	return _ids;
}

/**
 * Resets the list of unique object IDs.
 * @param ids New ID list.
 */
void SavedGame::setAllIds(const std::map<std::string, int> &ids)
{
	_ids = ids;
}

/**
 * Returns the list of countries in the game world.
 * @return Pointer to country list.
 */
std::vector<Country*> *SavedGame::getCountries()
{
	return &_countries;
}

/**
 * Adds up the monthly funding of all the countries.
 * @return Total funding.
 */
int SavedGame::getCountryFunding() const
{
	int total = 0;
	for (auto* country : _countries)
	{
		total += country->getFunding().back();
	}
	return total;
}

/**
 * Returns the list of world regions.
 * @return Pointer to region list.
 */
std::vector<Region*> *SavedGame::getRegions()
{
	return &_regions;
}

/**
 * Returns the list of player bases.
 * @return Pointer to base list.
 */
std::vector<Base*> *SavedGame::getBases()
{
	return &_bases;
}

/**
 * Returns the last selected player base.
 * @return Pointer to base.
 */
Base *SavedGame::getSelectedBase()
{
	// in case a base was destroyed or something...
	if (_selectedBase < _bases.size())
	{
		return _bases.at(_selectedBase);
	}
	else
	{
		return _bases.front();
	}
}

/**
 * Returns the last index of Visible Bases.
 * @return index for visible Bases.
 */
size_t SavedGame::getVisibleBasesIndex()
{
	return _visibleBasesIndex;
}

/**
 * Sets the last selected player base.
 * @param base number of the base.
 */
void SavedGame::setSelectedBase(size_t base)
{
	_selectedBase = base;
}

/**
 * Sets the last index of visible bases.
 * @param index number for visible bases.
 */
void SavedGame::setVisibleBasesIndex(size_t lastVisibleBasesIndex)
{
	_visibleBasesIndex = lastVisibleBasesIndex;
}

/**
 * Returns an immutable list of player bases.
 * @return Pointer to base list.
 */
const std::vector<Base*> *SavedGame::getBases() const
{
	return &_bases;
}

/**
 * Adds up the monthly maintenance of all the bases.
 * @return Total maintenance.
 */
int SavedGame::getBaseMaintenance() const
{
	int total = 0;
	for (const auto* xbase : _bases)
	{
		total += xbase->getMonthlyMaintenace();
	}
	return total;
}

/**
 * Returns the list of alien UFOs.
 * @return Pointer to UFO list.
 */
std::vector<Ufo*> *SavedGame::getUfos()
{
	return &_ufos;
}

/**
 * Returns the list of alien UFOs.
 * @return Pointer to UFO list.
 */
const std::vector<Ufo*> *SavedGame::getUfos() const
{
	return &_ufos;
}

/**
 * Returns the list of craft waypoints.
 * @return Pointer to waypoint list.
 */
std::vector<Waypoint*> *SavedGame::getWaypoints()
{
	return &_waypoints;
}

/**
 * Returns the list of mission sites.
 * @return Pointer to mission site list.
 */
std::vector<MissionSite*> *SavedGame::getMissionSites()
{
	return &_missionSites;
}

/**
 * Get pointer to the battleGame object.
 * @return Pointer to the battleGame object.
 */
SavedBattleGame *SavedGame::getSavedBattle()
{
	return _battleGame;
}

/**
 * Set battleGame object.
 * @param battleGame Pointer to the battleGame object.
 */
void SavedGame::setBattleGame(SavedBattleGame *battleGame)
{
	delete _battleGame;
	_battleGame = battleGame;
}

/**
 * Sets the status of a ufopedia rule
 * @param ufopediaRule The rule ID
 * @param newStatus Status to be set
 */
void SavedGame::setUfopediaRuleStatus(const std::string &ufopediaRule, int newStatus)
{
	_ufopediaRuleStatus[ufopediaRule] = newStatus;
}

/**
 * Sets the status of a manufacture rule
 * @param manufactureRule The rule ID
 * @param newStatus Status to be set
 */
void SavedGame::setManufactureRuleStatus(const std::string &manufactureRule, int newStatus)
{
	_manufactureRuleStatus[manufactureRule] = newStatus;
}

/**
* Sets the status of a research rule
* @param researchRule The rule ID
* @param newStatus Status to be set
*/
void SavedGame::setResearchRuleStatus(const std::string &researchRule, int newStatus)
{
	_researchRuleStatus[researchRule] = newStatus;
}

/**
 * Sets the hidden status of a purchase item
 * @param purchase item name
 * @param hidden
 */
void SavedGame::setHiddenPurchaseItemsStatus(const std::string &itemName, bool hidden)
{
	_hiddenPurchaseItemsMap[itemName] = hidden;
}

/**
 * Get the map of hidden items
 * @return map
 */
const std::map<std::string, bool> &SavedGame::getHiddenPurchaseItems()
{
	return _hiddenPurchaseItemsMap;
}

/*
 * Selects a "getOneFree" topic for the given research rule.
 * @param research Pointer to the given research rule.
 * @return Pointer to the selected getOneFree topic. Nullptr, if nothing was selected.
 */
const RuleResearch* SavedGame::selectGetOneFree(const RuleResearch* research)
{
	if (!research->getGetOneFree().empty() || !research->getGetOneFreeProtected().empty())
	{
		std::vector<const RuleResearch*> possibilities;
		for (auto* free : research->getGetOneFree())
		{
			if (isResearchRuleStatusDisabled(free->getName()))
			{
				continue; // skip disabled topics
			}
			if (!isResearched(free, false))
			{
				possibilities.push_back(free);
			}
		}
		for (auto& pair : research->getGetOneFreeProtected())
		{
			if (isResearched(pair.first, false))
			{
				for (auto* res : pair.second)
				{
					if (isResearchRuleStatusDisabled(res->getName()))
					{
						continue; // skip disabled topics
					}
					if (!isResearched(res, false))
					{
						possibilities.push_back(res);
					}
				}
			}
		}
		if (!possibilities.empty())
		{
			size_t pick = 0;
			if (!research->sequentialGetOneFree())
			{
				pick = RNG::generate(0, possibilities.size() - 1);
			}
			auto ret = possibilities.at(pick);
			return ret;
		}
	}
	return nullptr;
}

/*
 * Checks for and removes a research project from the "already discovered" list
 * @param research is the project we are checking for and removing, if necessary.
 */
void SavedGame::removeDiscoveredResearch(const RuleResearch * research)
{
	auto r = std::find(_discovered.begin(), _discovered.end(), research);
	if (r != _discovered.end())
	{
		_discovered.erase(r);
	}
}

/**
 * Make all research discovered (used in New Battle)
 * @param mod the game Mod
 */
void SavedGame::makeAllResearchDiscovered(const Mod* mod)
{
	for (auto& pair : mod->getResearchMap())
	{
		_discovered.push_back(pair.second);
	}
	sortReserchVector(_discovered);
}

/**
 * Add a ResearchProject to the list of already discovered ResearchProject
 * @param research The newly found ResearchProject
 * @param mod the game Mod
 * @param base the base, in which the project was finished
 * @param score should the score be awarded or not?
 */
void SavedGame::addFinishedResearch(const RuleResearch * research, const Mod * mod, Base * base, bool score)
{
	if (isResearchRuleStatusDisabled(research->getName()))
	{
		return;
	}

	// Not really a queue in C++ terminology (we don't need or want pop_front())
	std::vector<const RuleResearch *> queue;
	queue.push_back(research);

	size_t currentQueueIndex = 0;
	while (queue.size() > currentQueueIndex)
	{
		const RuleResearch *currentQueueItem = queue.at(currentQueueIndex);

		// 1. Find out and remember if the currentQueueItem has any undiscovered non-disabled "protected unlocks" or "getOneFree"
		bool hasUndiscoveredProtectedUnlocks = hasUndiscoveredProtectedUnlock(currentQueueItem);
		bool hasAnyUndiscoveredGetOneFrees = hasUndiscoveredGetOneFree(currentQueueItem, false);

		// 2. If the currentQueueItem was *not* already discovered before, add it to discovered research
		bool checkRelatedZeroCostTopics = true;
		if (!isResearched(currentQueueItem, false))
		{
			if (!research->isRepeatable())
			{
				_discovered.push_back(currentQueueItem);
				sortReserchVector(_discovered);
			}

			if (currentQueueItem != research)
			{
				ResearchDiaryEntry* entry = new ResearchDiaryEntry(currentQueueItem);
				entry->setDate(_time);
				entry->source.type = DiscoverySourceType::FREE_AFTER;
				entry->source.research = research;
				entry->source.name = research->getName();
				addResearchDiaryEntry(entry);
			}

			if (!hasUndiscoveredProtectedUnlocks && !hasAnyUndiscoveredGetOneFrees)
			{
				// If the currentQueueItem can't tell you anything anymore, remove it from popped research
				// Note: this is for optimisation purposes only, functionally it is *not* required...
				// ... removing it prematurely leads to bugs, maybe we should not do it at all?
				removePoppedResearch(currentQueueItem);
			}
			if (score)
			{
				addResearchScore(currentQueueItem->getPoints());
			}
			// process "disables"
			for (const auto* dis : currentQueueItem->getDisabled())
			{
				removeDiscoveredResearch(dis); // unresearch
				setResearchRuleStatus(dis->getName(), RuleResearch::RESEARCH_STATUS_DISABLED); // mark as permanently disabled
			}
		}
		else
		{
			// If the currentQueueItem *was* already discovered before, check if it has any undiscovered "protected unlocks".
			// If not, all zero-cost topics have already been processed before (during the first discovery)
			// and we can basically terminate here (i.e. skip step 3.).
			if (!hasUndiscoveredProtectedUnlocks)
			{
				checkRelatedZeroCostTopics = false;
			}
		}

		// process "re-enables": https://openxcom.org/forum/index.php?topic=12071.0
		for (const auto* ree : currentQueueItem->getReenabled())
		{
			if (isResearchRuleStatusDisabled(ree->getName()))
			{
				setResearchRuleStatus(ree->getName(), RuleResearch::RESEARCH_STATUS_NEW); // reset status
			}
		}

		// 3. If currentQueueItem is completed for the *first* time, or if it has any undiscovered "protected unlocks",
		// process all related zero-cost topics
		if (checkRelatedZeroCostTopics)
		{
			// 3a. Gather all available research projects
			std::vector<RuleResearch *> availableResearch;
			if (base)
			{
				// Note: even if two different but related projects are finished in two different bases at the same time,
				// the algorithm is robust enough to treat them *sequentially* (i.e. as if one was researched first and the other second),
				// thus calling this method for *one* base only is enough
				getAvailableResearchProjects(availableResearch, mod, base);
			}
			else
			{
				// Used in vanilla save converter only
				getAvailableResearchProjects(availableResearch, mod, 0);
			}

			// 3b. Iterate through all available projects and add zero-cost projects to the processing queue
			for (const RuleResearch* projectToTest : availableResearch)
			{
				// We are only interested in zero-cost projects!
				if (projectToTest->getCost() == 0)
				{
					// We are only interested in *new* projects (i.e. not processed or scheduled for processing yet)
					bool isAlreadyInTheQueue = false;
					for (const RuleResearch* res : queue)
					{
						if (res->getName() == projectToTest->getName())
						{
							isAlreadyInTheQueue = true;
							break;
						}
					}

					if (!isAlreadyInTheQueue)
					{
						if (projectToTest->getRequirements().empty())
						{
							// no additional checks for "unprotected" topics
							queue.push_back(projectToTest);
						}
						else
						{
							// for "protected" topics, we need to check if the currentQueueItem can unlock it or not
							for (const auto* unl : currentQueueItem->getUnlocked())
							{
								if (projectToTest == unl)
								{
									queue.push_back(projectToTest);
									break;
								}
							}
						}
					}
				}
			}
		}

		// 4. process remaining items in the queue
		++currentQueueIndex;
	}
}

void SavedGame::addResearchDiaryEntry(ResearchDiaryEntry* entry)
{
	_researchDiary.push_back(entry);
}

/**
 *  Returns the list of already discovered ResearchProject
 * @return the list of already discovered ResearchProject
 */
const std::vector<const RuleResearch *> & SavedGame::getDiscoveredResearch() const
{
	return _discovered;
}

/**
 * Does this item correspond to at least one research topic that can be researched now or in the future?
 */
bool SavedGame::isResearchable(const RuleItem* item, const Mod* mod) const
{
	for (const auto& pair : mod->getResearchMap())
	{
		if (pair.second->needItem() && pair.second->getNeededItem() == item)
		{
			// This research topic is "permanently" disabled, ignore it!
			if (isResearchRuleStatusDisabled(pair.first))
			{
				continue;
			}

			if (isResearched(pair.second, false))
			{
				if (hasUndiscoveredGetOneFree(pair.second, false))
				{
					// This research topic still has some more undiscovered non-disabled "getOneFree" topics, keep it!
					return true;
				}
				else if (hasUndiscoveredProtectedUnlock(pair.second))
				{
					// This research topic still has one or more undiscovered non-disabled "protected unlocks", keep it!
					return true;
				}
				else
				{
					// This topic can't give you anything else anymore, ignore it!
					continue;
				}
			}
			else
			{
				// This research topic is not yet researched, keep it!
				return true;
			}
		}
	}

	return false;
}

/**
 * Get the list of RuleResearch which can be researched in a Base.
 * @param projects the list of ResearchProject which are available.
 * @param mod the game Mod
 * @param base a pointer to a Base
 * @param considerDebugMode Should debug mode be considered or not.
 */
void SavedGame::getAvailableResearchProjects(std::vector<RuleResearch *> &projects, const Mod *mod, Base *base, bool considerDebugMode) const
{
	// This list is used for topics that can be researched even if *not all* dependencies have been discovered yet (e.g. STR_ALIEN_ORIGINS)
	// Note: all requirements of such topics *have to* be discovered though! This will be handled elsewhere.
	std::vector<const RuleResearch *> unlocked;
	for (const auto* research : _discovered)
	{
		for (const auto* unl : research->getUnlocked())
		{
			unlocked.push_back(unl);
		}
		sortReserchVector(unlocked);
	}

	// Create a list of research topics available for research in the given base
	for (const auto& pair : mod->getResearchMap())
	{
		// This research topic is permanently disabled, ignore it!
		if (isResearchRuleStatusDisabled(pair.first))
		{
			continue;
		}

		RuleResearch *research = pair.second;

		if ((considerDebugMode && _debug) || haveReserchVector(unlocked, research))
		{
			// Empty, these research topics are on the "unlocked list", *don't* check the dependencies!
		}
		else
		{
			// These items are not on the "unlocked list", we must check if "dependencies" are satisfied!
			if (!isResearched(research->getDependencies(), considerDebugMode))
			{
				continue;
			}
		}

		// Check if "requires" are satisfied
		// IMPORTANT: research topics with "requires" will NEVER be directly visible to the player anyway
		//   - there is an additional filter in NewResearchListState::fillProjectList(), see comments there for more info
		//   - there is an additional filter in NewPossibleResearchState::NewPossibleResearchState()
		//   - we do this check for other functionality using this method, namely SavedGame::addFinishedResearch()
		//     - Note: when called from there, parameter considerDebugMode = false
		if (!isResearched(research->getRequirements(), considerDebugMode))
		{
			continue;
		}

		// Remove the already researched topics from the list *UNLESS* they can still give you something more
		if (isResearched(research->getName(), false))
		{
			if (hasUndiscoveredGetOneFree(research, true))
			{
				// This research topic still has some more undiscovered non-disabled and *AVAILABLE* "getOneFree" topics, keep it!
			}
			else if (hasUndiscoveredProtectedUnlock(research))
			{
				// This research topic still has one or more undiscovered non-disabled "protected unlocks", keep it!
			}
			else
			{
				// This topic can't give you anything else anymore, ignore it!
				continue;
			}
		}

		if (base)
		{
			// Check if this topic is already being researched in the given base
			bool found = false;
			for (auto* ongoing : base->getResearch())
			{
				if (ongoing->getRules() == research)
				{
					found = true;
					break;
				}
			}
			if (found)
			{
				continue;
			}

			// Check for needed item in the given base
			if (research->needItem() && base->getStorageItems()->getItem(research->getNeededItem()) == 0)
			{
				continue;
			}

			// Check for required buildings/functions in the given base
			if ((~base->getProvidedBaseFunc({}) & research->getRequireBaseFunc()).any())
			{
				continue;
			}
		}
		else
		{
			// Used in vanilla save converter only
			if (research->needItem() && research->getCost() == 0)
			{
				continue;
			}
		}

		// Hallelujah, all checks passed, add the research topic to the list
		projects.push_back(research);
	}
}

/**
 * Get the list of newly available research projects once a ResearchProject has been completed.
 * @param before the list of available RuleResearch before completing new research.
 * @param after the list of available RuleResearch after completing new research.
 * @param diff the list of newly available RuleResearch after completing new research (after - before).
 */
void SavedGame::getNewlyAvailableResearchProjects(std::vector<RuleResearch *> & before, std::vector<RuleResearch *> & after, std::vector<RuleResearch *> & diff) const
{
	// History lesson:
	// Completely rewritten the original recursive algorithm, because it was inefficient, unreadable and wrong
	// a/ inefficient: it could call SavedGame::getAvailableResearchProjects() way too many times
	// b/ unreadable: because of recursion
	// c/ wrong: could end in an endless loop! in two different ways! (not in vanilla, but in mods)

	// Note:
	// We could move the sorting of "before" vector right after its creation to optimize a little bit more.
	// But sorting a short list is negligible compared to other operations we had to do to get to this point.
	// So I decided to leave it here, so that it's 100% clear what's going on.
	sortReserchVector(before);
	sortReserchVector(after);
	std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::inserter(diff, diff.begin()), researchLess);
}

/**
 * Get the list of RuleManufacture which can be manufacture in a Base.
 * @param productions the list of Productions which are available.
 * @param mod the Game Mod
 * @param base a pointer to a Base
 */
void SavedGame::getAvailableProductions (std::vector<RuleManufacture *> & productions, const Mod * mod, Base * base, ManufacturingFilterType filter) const
{
	const auto& baseProductions = base->getProductions();
	RuleBaseFacilityFunctions baseFunc = base->getProvidedBaseFunc({});

	for (const auto& manuf : mod->getManufactureList())
	{
		RuleManufacture *m = mod->getManufacture(manuf);
		if (!isResearched(m->getRequirements()))
		{
			continue;
		}
		bool found = false;
		for (auto* ongoing : baseProductions)
		{
			if (ongoing->getRules() == m)
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			continue;
		}
		if ((~baseFunc & m->getRequireBaseFunc()).any())
		{
			if (filter != MANU_FILTER_FACILITY_REQUIRED)
				continue;
		}
		else
		{
			if (filter == MANU_FILTER_FACILITY_REQUIRED)
				continue;
		}

		productions.push_back(m);
	}
}

/**
 * Get the list of newly available manufacture projects once a ResearchProject has been completed. This function check for fake ResearchProject.
 * @param dependables the list of RuleManufacture which are now available.
 * @param research The RuleResearch which has just been discovered
 * @param mod the Game Mod
 * @param base a pointer to a Base
 */
void SavedGame::getDependableManufacture (std::vector<RuleManufacture *> & dependables, const RuleResearch *research, const Mod * mod, Base *) const
{
	for (const auto& manuf : mod->getManufactureList())
	{
		// don't show previously unlocked (and seen!) manufacturing topics
		auto i = _manufactureRuleStatus.find(manuf);
		if (i != _manufactureRuleStatus.end())
		{
			if (i->second != RuleManufacture::MANU_STATUS_NEW)
				continue;
		}

		RuleManufacture *m = mod->getManufacture(manuf);
		const auto& reqs = m->getRequirements();
		if (isResearched(reqs) && std::find(reqs.begin(), reqs.end(), research) != reqs.end())
		{
			dependables.push_back(m);
		}
	}
}

/**
 * Get the list of RuleSoldierTransformation which can occur at a base.
 * @param transformations the list of Transformations which are available.
 * @param mod the Game Mod
 * @param base a pointer to a Base
 */
void SavedGame::getAvailableTransformations (std::vector<RuleSoldierTransformation *> & transformations, const Mod * mod, Base * base) const
{
	auto& list = mod->getSoldierTransformationList();
	if (list.empty())
	{
		return;
	}

	RuleBaseFacilityFunctions baseFunc = base->getProvidedBaseFunc({});

	for (const auto& transformType : list)
	{
		RuleSoldierTransformation *m = mod->getSoldierTransformation(transformType);
		if (!isResearched(m->getRequiredResearch()))
		{
			continue;
		}
		if ((~baseFunc & m->getRequiredBaseFuncs()).any())
		{
			continue;
		}

		transformations.push_back(m);
	}
}

/**
 * Get the list of newly available items to purchase once a ResearchProject has been completed.
 * @param dependables the list of RuleItem which are now available.
 * @param research The RuleResearch which has just been discovered
 * @param mod the Game Mod
 */
void SavedGame::getDependablePurchase(std::vector<RuleItem *> & dependables, const RuleResearch *research, const Mod * mod) const
{
	for (const auto& itemType : mod->getItemsList())
	{
		RuleItem *item = mod->getItem(itemType);
		if (item->getBuyCost() != 0)
		{
			const auto& reqs = item->getRequirements();
			bool found = std::find(reqs.begin(), reqs.end(), research) != reqs.end();
			const auto& reqsBuy = item->getBuyRequirements();
			bool foundBuy = std::find(reqsBuy.begin(), reqsBuy.end(), research) != reqsBuy.end();
			if (found || foundBuy)
			{
				if (isResearched(item->getBuyRequirements()) && isResearched(item->getRequirements()))
				{
					dependables.push_back(item);
				}
			}
		}
	}
}

/**
 * Get the list of newly available craft to purchase/rent once a ResearchProject has been completed.
 * @param dependables the list of RuleCraft which are now available.
 * @param research The RuleResearch which has just been discovered
 * @param mod the Game Mod
 */
void SavedGame::getDependableCraft(std::vector<RuleCraft *> & dependables, const RuleResearch *research, const Mod * mod) const
{
	for (const auto& craftType : mod->getCraftsList())
	{
		RuleCraft *craftItem = mod->getCraft(craftType);
		if (craftItem->getBuyCost() != 0)
		{
			const auto& reqs = craftItem->getRequirements();
			if (std::find(reqs.begin(), reqs.end(), research->getName()) != reqs.end())
			{
				if (isResearched(craftItem->getRequirements()))
				{
					dependables.push_back(craftItem);
				}
			}
		}
	}
}

/**
 * Get the list of newly available facilities to build once a ResearchProject has been completed.
 * @param dependables the list of RuleBaseFacility which are now available.
 * @param research The RuleResearch which has just been discovered
 * @param mod the Game Mod
 */
void SavedGame::getDependableFacilities(std::vector<RuleBaseFacility *> & dependables, const RuleResearch *research, const Mod * mod) const
{
	for (const auto& facType : mod->getBaseFacilitiesList())
	{
		RuleBaseFacility *facilityItem = mod->getBaseFacility(facType);
		const auto& reqs = facilityItem->getRequirements();
		if (std::find(reqs.begin(), reqs.end(), research->getName()) != reqs.end())
		{
			if (isResearched(facilityItem->getRequirements()))
			{
				dependables.push_back(facilityItem);
			}
		}
	}
}

/**
 * Gets the status of a ufopedia rule.
 * @param ufopediaRule Ufopedia rule ID.
 * @return Status (0=new, 1=normal).
 */
int SavedGame::getUfopediaRuleStatus(const std::string &ufopediaRule)
{
	return _ufopediaRuleStatus[ufopediaRule];
}

/**
 * Gets the status of a manufacture rule.
 * @param manufactureRule Manufacture rule ID.
 * @return Status (0=new, 1=normal, 2=hidden).
 */
int SavedGame::getManufactureRuleStatus(const std::string &manufactureRule)
{
	return _manufactureRuleStatus[manufactureRule];
}

/**
 * Gets the status of a research rule.
 * @param researchRule Research rule ID.
 * @return Status (0=new, 1=normal, 2=disabled, 3=hidden).
 */
int SavedGame::getResearchRuleStatus(const std::string &researchRule) const
{
	auto it = _researchRuleStatus.find(researchRule);
	if (it != _researchRuleStatus.end())
	{
		return it->second;
	}
	return RuleResearch::RESEARCH_STATUS_NEW; // no status = new
}

/**
 * Is the research permanently disabled?
 * @param researchRule Research rule ID.
 * @return True, if the research rule status is disabled.
 */
bool SavedGame::isResearchRuleStatusDisabled(const std::string &researchRule) const
{
	auto it = _researchRuleStatus.find(researchRule);
	if (it != _researchRuleStatus.end())
	{
		if (it->second == RuleResearch::RESEARCH_STATUS_DISABLED)
		{
			return true;
		}
	}
	return false;
}

/**
 * Returns if a research still has undiscovered non-disabled "getOneFree".
 * @param r Research to check.
 * @param checkOnlyAvailableTopics Check only available topics (=topics with discovered prerequisite) or all topics?
 * @return Whether it has any undiscovered non-disabled "getOneFree" or not.
 */
bool SavedGame::hasUndiscoveredGetOneFree(const RuleResearch * r, bool checkOnlyAvailableTopics) const
{
	if (!isResearched(r->getGetOneFree(), false, true))
	{
		return true; // found something undiscovered (and NOT disabled) already, no need to search further
	}
	else
	{
		// search through getOneFreeProtected topics too
		for (const auto& pair : r->getGetOneFreeProtected())
		{
			if (checkOnlyAvailableTopics && !isResearched(pair.first, false))
			{
				// skip this group, its prerequisite has not been discovered yet
			}
			else
			{
				if (!isResearched(pair.second, false, true))
				{
					return true; // found something undiscovered (and NOT disabled) already, no need to search further
				}
			}
		}
	}
	return false;
}

/**
 * Returns if a research still has undiscovered non-disabled "protected unlocks".
 * @param r Research to check.
 * @return Whether it has any undiscovered non-disabled "protected unlocks" or not.
 */
bool SavedGame::hasUndiscoveredProtectedUnlock(const RuleResearch * r) const
{
	// Note: checking for not yet discovered unlocks protected by "requires" (which also implies cost = 0)
	for (const auto* unlock : r->getUnlocked())
	{
		if (isResearchRuleStatusDisabled(unlock->getName()))
		{
			// ignore all disabled topics (as if they didn't exist)
			continue;
		}
		if (!unlock->getRequirements().empty())
		{
			if (!isResearched(unlock, false))
			{
				return true;
			}
		}
	}
	return false;
}

/**
 * Returns if a certain research topic has been completed.
 * @param research Research ID.
 * @param considerDebugMode Should debug mode be considered or not.
 * @return Whether it's researched or not.
 */
bool SavedGame::isResearched(const std::string &research, bool considerDebugMode) const
{
	//if (research.empty())
	//	return true;
	if (considerDebugMode && _debug)
		return true;

	return haveReserchVector(_discovered, research);
}

bool SavedGame::isResearched(const RuleResearch *research, bool considerDebugMode) const
{
	//if (research.empty())
	//	return true;
	if (considerDebugMode && _debug)
		return true;

	return haveReserchVector(_discovered, research);
}

bool SavedGame::isResearched(const std::vector<std::string> &research, bool considerDebugMode) const
{
	if (research.empty())
		return true;
	if (considerDebugMode && _debug)
		return true;

	for (const auto& res : research)
	{
		if (!haveReserchVector(_discovered, res))
		{
			return false;
		}
	}

	return true;
}

/**
 * Returns if a certain list of research topics has been completed.
 * @param research List of research IDs.
 * @param considerDebugMode Should debug mode be considered or not.
 * @param skipDisabled Should permanently disabled topics be considered or not.
 * @return Whether it's researched or not.
 */
bool SavedGame::isResearched(const std::vector<const RuleResearch *> &research, bool considerDebugMode, bool skipDisabled) const
{
	if (research.empty())
		return true;
	if (considerDebugMode && _debug)
		return true;

	for (const auto* res : research)
	{
		if (skipDisabled)
		{
			// ignore all disabled topics (as if they didn't exist)
			if (isResearchRuleStatusDisabled(res->getName()))
			{
				continue;
			}
		}
		if (!haveReserchVector(_discovered, res))
		{
			return false;
		}
	}

	return true;
}

/**
 * Returns if a certain item has been obtained, i.e. is present in the base stores or on a craft.
 * Items in transfer, worn by soldiers, etc. are ignored!!
 * @param itemType Item ID.
 * @return Whether it's obtained or not.
 */
bool SavedGame::isItemObtained(const std::string &itemType, const Mod* mod) const
{
	const RuleItem* item = mod->getItem(itemType);
	if (item)
	{
		for (auto* xbase : _bases)
		{
			if (xbase->getStorageItems()->getItem(item) > 0)
				return true;

			for (auto* xcraft : *xbase->getCrafts())
			{
				if (xcraft->getItems()->getItem(item) > 0)
					return true;
			}
		}
	}
	return false;
}
/**
 * Returns if a certain facility has been built in any base.
 * @param facilityType facility ID.
 * @return Whether it's been built or not. If false, the facility has not been built in any base.
 */
bool SavedGame::isFacilityBuilt(const std::string &facilityType) const
{
	for (auto* xbase : _bases)
	{
		for (auto* fac : *xbase->getFacilities())
		{
			if (fac->getBuildTime() == 0 && fac->getRules()->getType() == facilityType)
			{
				return true;
			}
		}
	}
	return false;
}

/**
 * Returns if a certain soldier type has been hired in any base.
 * @param soldierType soldier type ID.
 * @return Whether it's been hired (and arrived already) or not.
 */
bool SavedGame::isSoldierTypeHired(const std::string& soldierType) const
{
	for (auto* xbase : _bases)
	{
		for (auto* soldier : *xbase->getSoldiers())
		{
			if (soldier->getRules()->getType() == soldierType)
			{
				return true;
			}
		}
	}
	return false;
}

/**
 * Returns pointer to the Soldier given it's unique ID.
 * @param id A soldier's unique id.
 * @return Pointer to Soldier.
 */
Soldier *SavedGame::getSoldier(int id) const
{
	for (auto* xbase : _bases)
	{
		for (auto* soldier : *xbase->getSoldiers())
		{
			if (soldier->getId() == id)
			{
				return soldier;
			}
		}
	}
	for (auto* soldier : _deadSoldiers)
	{
		if (soldier->getId() == id)
		{
			return soldier;
		}
	}
	return 0;
}

/**
 * Handles the higher promotions (not the rookie-squaddie ones).
 * @param participants a list of soldiers that were actually present at the battle.
 * @param mod the Game Mod
 * @return Whether or not some promotions happened - to show the promotions screen.
 */
bool SavedGame::handlePromotions(std::vector<Soldier*> &participants, const Mod *mod)
{
	int soldiersPromoted = 0;
	Soldier *highestRanked = 0;
	std::vector<Soldier*> soldiers = getAllActiveSoldiers();
	RankCount rankCounts = RankCount(soldiers);

	int totalSoldiers = rankCounts.getTotalSoldiers();

	if (rankCounts[RANK_COMMANDER] == 0 && totalSoldiers >= mod->getSoldiersPerRank(RANK_COMMANDER))
	{
		highestRanked = inspectSoldiers(soldiers, participants, RANK_COLONEL);
		if (highestRanked)
		{
			// only promote one colonel to commander
			highestRanked->promoteRank();
			soldiersPromoted++;
			rankCounts[RANK_COMMANDER]++;
			rankCounts[RANK_COLONEL]--;
		}
	}

	for (SoldierRank rank : { RANK_COLONEL, RANK_CAPTAIN, RANK_SERGEANT })
	{
		while ((totalSoldiers / mod->getSoldiersPerRank(rank)) > rankCounts[rank])
		{
			highestRanked = inspectSoldiers(soldiers, participants, rank - 1);
			if (highestRanked)
			{
				highestRanked->promoteRank();
				soldiersPromoted++;
				rankCounts[rank]++;
				rankCounts[(SoldierRank)(rank - 1)]--;
			}
			else
			{
				break;
			}
		}
	}

	return (soldiersPromoted > 0);
}

/**
 * Checks how many soldiers of a rank exist and which one has the highest score.
 * @param soldiers full list of live soldiers.
 * @param participants list of participants on this mission.
 * @param rank Rank to inspect.
 * @return the highest ranked soldier
 */
Soldier *SavedGame::inspectSoldiers(std::vector<Soldier*> &soldiers, std::vector<Soldier*> &participants, int rank)
{
	int highestScore = 0;
	Soldier *highestRanked = 0;
	for (auto* soldier : soldiers)
	{
		const auto& rankStrings = soldier->getRules()->getRankStrings();
		bool rankIsMatching = (soldier->getRank() == rank);
		if (!rankStrings.empty())
		{
			// if rank is matching, but there are no more higher ranks defined for this soldier type, skip this soldier
			if (rankIsMatching && (rank >= (int)rankStrings.size() - 1))
			{
				rankIsMatching = false;
			}
		}
		if (rankIsMatching)
		{
			int score = getSoldierScore(soldier);
			if (score > highestScore && (!Options::fieldPromotions || std::find(participants.begin(), participants.end(), soldier) != participants.end()))
			{
				highestScore = score;
				highestRanked = soldier;
			}
		}
	}
	return highestRanked;
}

/**
 * Gets the (approximate) number of idle days since the soldier's last mission.
 */
int SavedGame::getSoldierIdleDays(const Soldier *soldier)
{
	int lastMissionId = -1;
	int idleDays = 999;

	if (!soldier->getDiary()->getMissionIdList().empty())
	{
		lastMissionId = soldier->getDiary()->getMissionIdList().back();
	}

	if (lastMissionId == -1)
		return idleDays;

	for (const auto* missionInfo : _missionStatistics)
	{
		if (missionInfo->id == lastMissionId)
		{
			idleDays = 0;
			idleDays += (_time->getYear() - missionInfo->time.getYear()) * 365;
			idleDays += (_time->getMonth() - missionInfo->time.getMonth()) * 30;
			idleDays += (_time->getDay() - missionInfo->time.getDay()) * 1;
			break;
		}
	}

	if (idleDays > 999)
		idleDays = 999;

	return idleDays;
}

/**
 * Evaluate the score of a soldier based on all of his stats, missions and kills.
 * @param soldier the soldier to get a score for.
 * @return this soldier's score.
 */
int SavedGame::getSoldierScore(Soldier *soldier)
{
	const UnitStats *s = soldier->getCurrentStats();
	int v1 = 2 * s->health + 2 * s->stamina + 4 * s->reactions + 4 * s->bravery;
	int v2 = v1 + 3*( s->tu + 2*( s->firing ) );
	int v3 = v2 + s->melee + s->throwing + s->strength;
	if (s->psiSkill > 0) v3 += s->psiStrength + 2 * s->psiSkill;
	return v3 + 10 * ( soldier->getMissions() + soldier->getKills() );
}

/**
  * Returns the list of alien bases.
  * @return Pointer to alien base list.
  */
std::vector<AlienBase*> *SavedGame::getAlienBases()
{
	return &_alienBases;
}

/**
 * Toggles debug mode.
 */
void SavedGame::setDebugMode()
{
	_debug = !_debug;
}

/**
 * Gets the current debug mode.
 * @return Debug mode.
 */
bool SavedGame::getDebugMode() const
{
	return _debug;
}

/**
 * Find a mission type in the active alien missions.
 * @param region The region string ID.
 * @param objective The active mission objective.
 * @param race The alien race used for more specific search (by type instead of objective). Optional.
 * @return A pointer to the mission, or 0 if no mission matched.
 */
AlienMission *SavedGame::findAlienMission(const std::string &region, MissionObjective objective, AlienRace* race) const
{
	if (race)
	{
		auto* retalWeights = race->retaliationMissionWeights(_monthsPassed);
		if (retalWeights)
		{
			auto retalNames = retalWeights->getNames();
			if (!retalNames.empty())
			{
				// if we made it this far, search by type and region
				for (const auto& missionType : retalNames)
				{
					for (auto* mission : _activeMissions)
					{
						if (mission->getRules().getType() == missionType && mission->getRegion() == region)
						{
							return mission;
						}
					}
				}
				return 0;
			}
		}
	}

	for (auto* mission : _activeMissions)
	{
		if (mission->getRules().getObjective() == objective && mission->getRegion() == region)
		{
			return mission;
		}
	}
	return 0;
}

/**
 * return the list of monthly maintenance costs
 * @return list of maintenances.
 */
std::vector<int64_t> &SavedGame::getMaintenances()
{
	return _maintenance;
}

/**
 * adds to this month's research score
 * @param score the amount to add.
 */
void SavedGame::addResearchScore(int score)
{
	_researchScores.back() += score;
}

/**
 * return the list of research scores
 * @return list of research scores.
 */
std::vector<int> &SavedGame::getResearchScores()
{
	return _researchScores;
}

/**
 * return the list of income scores
 * @return list of income scores.
 */
std::vector<int64_t> &SavedGame::getIncomes()
{
	return _incomes;
}

/**
 * return the list of expenditures scores
 * @return list of expenditures scores.
 */
std::vector<int64_t> &SavedGame::getExpenditures()
{
	return _expenditures;
}

/**
 * return if the player has been
 * warned about poor performance.
 * @return true or false.
 */
bool SavedGame::getWarned() const
{
	return _warned;
}

/**
 * sets the player's "warned" status.
 * @param warned set "warned" to this.
 */
void SavedGame::setWarned(bool warned)
{
	_warned = warned;
}

/**
 * Find the region containing this location.
 * @param lon The longitude.
 * @param lat The latitude.
 * @return Pointer to the region, or 0.
 */
Region *SavedGame::locateRegion(double lon, double lat) const
{
	Region* found = nullptr;
	for (auto* region : _regions)
	{
		if (region->getRules()->insideRegion(lon, lat))
		{
			found = region;
			break;
		}
	}
	if (found)
	{
		return found;
	}
	Log(LOG_ERROR) << "Failed to find a region at location [" << lon << ", " << lat << "].";
	return 0;
}

/**
 * Find the region containing this target.
 * @param target The target to locate.
 * @return Pointer to the region, or 0.
 */
Region *SavedGame::locateRegion(const Target &target) const
{
	return locateRegion(target.getLongitude(), target.getLatitude());
}

/**
 * Find the country containing this location.
 * @param lon The longitude.
 * @param lat The latitude.
 * @return Pointer to the country, or 0.
 */
Country* SavedGame::locateCountry(double lon, double lat) const
{
	Country* found = nullptr;
	for (auto* country : _countries)
	{
		if (country->getRules()->insideCountry(lon, lat))
		{
			found = country;
			break;
		}
	}
	if (found)
	{
		return found;
	}
	//Log(LOG_DEBUG) << "Failed to find a country at location [" << lon << ", " << lat << "].";
	return 0;
}

/**
 * Find the country containing this target.
 * @param target The target to locate.
 * @return Pointer to the country, or 0.
 */
Country* SavedGame::locateCountry(const Target& target) const
{
	return locateCountry(target.getLongitude(), target.getLatitude());
}

/**
 * Select a soldier nationality based on mod rules and location on the globe.
 */
int SavedGame::selectSoldierNationalityByLocation(const Mod* mod, const RuleSoldier* rule, const Target* target) const
{
	if (!target)
	{
		return -1;
	}

	if (mod->getHireByCountryOdds() > 0 && RNG::percent(mod->getHireByCountryOdds()))
	{
		Country* country = locateCountry(*target);
		if (country)
		{
			int nationality = 0;
			for (auto* namepool : rule->getNames())
			{
				// we assume there is only one such name pool (or none), thus we stop searching on the first hit
				if (country->getRules()->getType() == namepool->getCountry())
				{
					return nationality;
				}
				++nationality;
			}
		}
	}

	if (mod->getHireByRegionOdds() > 0 && RNG::percent(mod->getHireByRegionOdds()))
	{
		Region* region = locateRegion(*target);
		if (region)
		{
			// build a new name pool collection, filtered by the region
			std::vector<std::pair<SoldierNamePool*, int> > filteredNames;
			int totalFilteredNamePoolWeight = 0;
			int nationality = 0;
			for (auto* namepool : rule->getNames())
			{
				if (region->getRules()->getType() == namepool->getRegion())
				{
					filteredNames.push_back(std::make_pair(namepool, nationality));
					totalFilteredNamePoolWeight += namepool->getGlobalWeight();
				}
				++nationality;
			}

			// the modder wants to have regional name pools, but didn't provide any, sigh
			if (totalFilteredNamePoolWeight < 1)
			{
				return -1; // let's just ignore the modder's unfulfillable wish
			}

			// select the nationality from the filtered pool, by weight
			int tmp = RNG::generate(1, totalFilteredNamePoolWeight);
			for (const auto& namepoolPair : filteredNames)
			{
				if (tmp <= namepoolPair.first->getGlobalWeight())
				{
					return namepoolPair.second;
				}
				tmp -= namepoolPair.first->getGlobalWeight();
			}
		}
	}

	return -1;
}

/*
 * @return the month counter.
 */
int SavedGame::getMonthsPassed() const
{
	return _monthsPassed;
}

/*
 * @return the GraphRegionToggles.
 */
const std::string &SavedGame::getGraphRegionToggles() const
{
	return _graphRegionToggles;
}

/*
 * @return the GraphCountryToggles.
 */
const std::string &SavedGame::getGraphCountryToggles() const
{
	return _graphCountryToggles;
}

/*
 * @return the GraphFinanceToggles.
 */
const std::string &SavedGame::getGraphFinanceToggles() const
{
	return _graphFinanceToggles;
}

/**
 * Sets the GraphRegionToggles.
 * @param value The new value for GraphRegionToggles.
 */
void SavedGame::setGraphRegionToggles(const std::string &value)
{
	_graphRegionToggles = value;
}

/**
 * Sets the GraphCountryToggles.
 * @param value The new value for GraphCountryToggles.
 */
void SavedGame::setGraphCountryToggles(const std::string &value)
{
	_graphCountryToggles = value;
}

/**
 * Sets the GraphFinanceToggles.
 * @param value The new value for GraphFinanceToggles.
 */
void SavedGame::setGraphFinanceToggles(const std::string &value)
{
	_graphFinanceToggles = value;
}

/*
 * Increment the month counter.
 */
void SavedGame::addMonth()
{
	++_monthsPassed;

	_monthlyPurchaseLimitLog.clear();
}

/*
 * marks a research topic as having already come up as "we can now research"
 * @param research is the project we want to add to the vector
 */
void SavedGame::addPoppedResearch(const RuleResearch* research)
{
	if (!wasResearchPopped(research))
		_poppedResearch.push_back(research);
}

/*
 * checks if an unresearched topic has previously been popped up.
 * @param research is the project we are checking for
 * @return whether or not it has been popped up.
 */
bool SavedGame::wasResearchPopped(const RuleResearch* research)
{
	return (std::find(_poppedResearch.begin(), _poppedResearch.end(), research) != _poppedResearch.end());
}

/*
 * checks for and removes a research project from the "has been popped up" array
 * @param research is the project we are checking for and removing, if necessary.
 */
void SavedGame::removePoppedResearch(const RuleResearch* research)
{
	auto r = std::find(_poppedResearch.begin(), _poppedResearch.end(), research);
	if (r != _poppedResearch.end())
	{
		_poppedResearch.erase(r);
	}
}

/*
 * remembers that this event has been generated
 * @param event is the event we want to remember
 */
void SavedGame::addGeneratedEvent(const RuleEvent* event)
{
	_generatedEvents[event->getName()] += 1;
}

/*
 * checks if an event has been generated previously
 * @param eventName is the event we are checking for
 * @return whether or not it has been generated previously
 */
bool SavedGame::wasEventGenerated(const std::string& eventName)
{
	return (_generatedEvents.find(eventName) != _generatedEvents.end());
}

/**
 * Returns the list of dead soldiers.
 * @return Pointer to soldier list.
 */
std::vector<Soldier*> *SavedGame::getDeadSoldiers()
{
	return &_deadSoldiers;
}

/**
 * Calculates and returns a list of all active soldiers.
 * @return All active soldiers.
*/
std::vector<Soldier*> SavedGame::getAllActiveSoldiers() const
{
	std::vector<Soldier*> soldiers;
	for (auto* xbase : _bases)
	{
		std::vector<Soldier*> baseSoldiers = *xbase->getSoldiers();
		soldiers.insert(soldiers.end(), baseSoldiers.begin(), baseSoldiers.end());

		for (auto* transfer : *xbase->getTransfers())
		{
			if (transfer->getType() == TRANSFER_SOLDIER)
			{
				soldiers.push_back(transfer->getSoldier());
			}
		}
	}

	return soldiers;
}

/**
 * Sets the last selected armor.
 * @param value The new value for last selected armor - Armor type string.
 */

void SavedGame::setLastSelectedArmor(const std::string &value)
{
	_lastselectedArmor = value;
}

/**
 * Gets the last selected armor
 * @return last used armor type string
 */
std::string SavedGame::getLastSelectedArmor() const
{
	return _lastselectedArmor;
}

/**
* Returns the global equipment layout at specified index.
* @return Pointer to the EquipmentLayoutItem list.
*/
std::vector<EquipmentLayoutItem*> *SavedGame::getGlobalEquipmentLayout(int index)
{
	return &_globalEquipmentLayout[index];
}

/**
* Returns the name of a global equipment layout at specified index.
* @return A name.
*/
const std::string &SavedGame::getGlobalEquipmentLayoutName(int index) const
{
	return _globalEquipmentLayoutName[index];
}

/**
* Sets the name of a global equipment layout at specified index.
* @param index Array index.
* @param name New name.
*/
void SavedGame::setGlobalEquipmentLayoutName(int index, const std::string &name)
{
	_globalEquipmentLayoutName[index] = name;
}

/**
 * Returns the armor type of a global equipment layout at specified index.
 * @return Armor type.
 */
const std::string& SavedGame::getGlobalEquipmentLayoutArmor(int index) const
{
	return _globalEquipmentLayoutArmor[index];
}

/**
 * Sets the armor type of a global equipment layout at specified index.
 * @param index Array index.
 * @param armorType New armor type.
 */
void SavedGame::setGlobalEquipmentLayoutArmor(int index, const std::string& armorType)
{
	_globalEquipmentLayoutArmor[index] = armorType;
}

/**
* Returns the global craft loadout at specified index.
* @return Pointer to the ItemContainer list.
*/
ItemContainer *SavedGame::getGlobalCraftLoadout(int index)
{
	return _globalCraftLoadout[index];
}

/**
* Returns the name of a global craft loadout at specified index.
* @return A name.
*/
const std::string &SavedGame::getGlobalCraftLoadoutName(int index) const
{
	return _globalCraftLoadoutName[index];
}

/**
* Sets the name of a global craft loadout at specified index.
* @param index Array index.
* @param name New name.
*/
void SavedGame::setGlobalCraftLoadoutName(int index, const std::string &name)
{
	_globalCraftLoadoutName[index] = name;
}

/**
 * Returns the list of mission statistics.
 * @return Pointer to statistics list.
 */
std::vector<MissionStatistics*> *SavedGame::getMissionStatistics()
{
	return &_missionStatistics;
}

/**
* Adds a UFO to the ignore list.
* @param ufoId Ufo ID.
*/
void SavedGame::addUfoToIgnoreList(int ufoId)
{
	if (ufoId != 0)
	{
		_ignoredUfos.insert(ufoId);
	}
}

/**
* Checks if a UFO is on the ignore list.
* @param ufoId Ufo ID.
*/
bool SavedGame::isUfoOnIgnoreList(int ufoId)
{
	return _ignoredUfos.find(ufoId) != _ignoredUfos.end();
}

/**
 * Registers a soldier's death in the memorial.
 * @param soldier Pointer to dead soldier.
 * @param cause Pointer to cause of death, NULL if missing in action.
 */
std::vector<Soldier*>::iterator SavedGame::killSoldier(bool resetArmor, Soldier *soldier, BattleUnitKills *cause)
{
	if (resetArmor)
	{
		// OXCE: soldiers are buried in their default armor (...nicer stats in the Memorial GUI; no free armor if resurrected)
		soldier->setArmor(soldier->getRules()->getDefaultArmor());
		soldier->setReplacedArmor(0);
		soldier->setTransformedArmor(0);
	}
	else
	{
		// IMPORTANT: don't change the geoscape armor during the ongoing battle!
		// battlescape armor would reset to geoscape armor after save and reload
	}

	std::vector<Soldier*>::iterator soldierIt;
	for (auto* xbase : _bases)
	{
		for (soldierIt = xbase->getSoldiers()->begin(); soldierIt != xbase->getSoldiers()->end(); ++soldierIt)
		{
			if ((*soldierIt) == soldier)
			{
				soldier->die(new SoldierDeath(*_time, cause));
				_deadSoldiers.push_back(soldier);
				return xbase->getSoldiers()->erase(soldierIt);
			}
		}
	}
	return soldierIt;
}

/**
 * enables/disables autosell for an item type
 */
void SavedGame::setAutosell(const RuleItem *itype, const bool enabled)
{
	if (enabled)
	{
		_autosales.insert(itype);
	}
	else
	{
		_autosales.erase(itype);
	}
}
/**
 * get autosell state for an item type
 */
bool SavedGame::getAutosell(const RuleItem *itype) const
{
	if (!Options::oxceAutoSell)
	{
		return false;
	}
	return _autosales.find(itype) != _autosales.end();
}

/**
 * Removes all soldiers from a given craft.
 */
void SavedGame::removeAllSoldiersFromXcomCraft(Craft *craft)
{
	for (auto* xbase : _bases)
	{
		for (auto* soldier : *xbase->getSoldiers())
		{
			if (soldier->getCraft() == craft)
			{
				soldier->setCraft(0);
			}
		}
	}
}

/**
 * Stop hunting the given xcom craft.
 */
void SavedGame::stopHuntingXcomCraft(Craft *target)
{
	for (auto* ufo : _ufos)
	{
		ufo->resetOriginalDestination(target);
	}
}

/**
 * Stop hunting all xcom craft from a given xcom base.
 */
void SavedGame::stopHuntingXcomCrafts(Base *base)
{
	for (auto* xcraft : *base->getCrafts())
	{
		for (auto* ufo : _ufos)
		{
			ufo->resetOriginalDestination(xcraft);
		}
	}
}

/**
 * Should all xcom soldiers have completely empty starting inventory when doing base equipment?
 */
bool SavedGame::getDisableSoldierEquipment() const
{
	return _disableSoldierEquipment;
}

/**
 * Sets the corresponding flag.
 */
void SavedGame::setDisableSoldierEquipment(bool disableSoldierEquipment)
{
	_disableSoldierEquipment = disableSoldierEquipment;
}

/**
 * Is the mana feature already unlocked?
 */
bool SavedGame::isManaUnlocked(Mod *mod) const
{
	auto researchName = mod->getManaUnlockResearch();
	if (Mod::isEmptyRuleName(researchName) || isResearched(researchName))
	{
		return true;
	}
	return false;
}

/**
 * Gets the current score based on research score and xcom/alien activity in regions.
 */
int SavedGame::getCurrentScore(int monthsPassed) const
{
	size_t invertedEntry = _funds.size() - 1;
	int scoreTotal = _researchScores.at(invertedEntry);
	if (monthsPassed > 1)
		scoreTotal += 400;
	for (auto region : _regions)
	{
		scoreTotal += region->getActivityXcom().at(invertedEntry) - region->getActivityAlien().at(invertedEntry);
	}
	return scoreTotal;
}

/**
 * Clear links for the given alien base. Use this before deleting the alien base.
 */
void SavedGame::clearLinksForAlienBase(AlienBase* alienBase, const Mod* mod)
{
	// Take care to remove supply missions for this base.
	for (auto am : _activeMissions)
	{
		if (am->getAlienBase() == alienBase)
		{
			am->setAlienBase(0);

			// if this is an Earth-based operation, losing the base means mission cannot continue anymore
			if (am->getRules().getOperationType() != AMOT_SPACE)
			{
				am->setInterrupted(true);
			}
		}
	}
	// If there was a pact with this base, cancel it?
	if (mod->getAllowCountriesToCancelAlienPact() && !alienBase->getPactCountry().empty())
	{
		for (auto* country : _countries)
		{
			if (country->getRules()->getType() == alienBase->getPactCountry())
			{
				country->setCancelPact();
				break;
			}
		}
	}
}

/**
 * Delete the given retaliation mission.
 */
void SavedGame::deleteRetaliationMission(AlienMission* am, Base* base)
{
	for (auto iter = _ufos.begin(); iter != _ufos.end();)
	{
		Ufo* ufo = (*iter);
		if (ufo->getMission() == am)
		{
			delete ufo;
			iter = _ufos.erase(iter);
		}
		else
		{
			++iter;
		}
	}
	for (auto iter = _activeMissions.begin(); iter != _activeMissions.end(); ++iter)
	{
		AlienMission* alienMission = (*iter);
		if (alienMission == am)
		{
			delete alienMission;
			_activeMissions.erase(iter);
			break;
		}
	}
	if (base)
	{
		base->setRetaliationMission(nullptr);
	}
}

/**
 * Spawn a Geoscape event from the event rules.
 * @return True if successful.
 */
bool SavedGame::spawnEvent(const RuleEvent* eventRules)
{
	if (!eventRules)
	{
		return false;
	}

	GeoscapeEvent* newEvent = new GeoscapeEvent(*eventRules);
	int minutes = (eventRules->getTimer() + (RNG::generate(0, eventRules->getTimerRandom()))) / 30 * 30;
	if (minutes < 60) minutes = 60; // just in case
	newEvent->setSpawnCountdown(minutes);
	_geoscapeEvents.push_back(newEvent);

	// remember that it has been generated
	addGeneratedEvent(eventRules);

	if (Options::oxceGeoscapeDebugLogMaxEntries > 0)
	{
		std::ostringstream ss;
		ss << "gameTime: " << _time->getFullString();
		ss << " eventSpawn: " << newEvent->getRules().getName();
		ss << " days/hours: " << (minutes / 60) / 24 << "/" << (minutes / 60) % 24;
		_geoscapeDebugLog.push_back(ss.str());
	}

	return true;
}

/**
 * Checks if an instant Geoscape event can be spawned.
 */
bool SavedGame::canSpawnInstantEvent(const RuleEvent* eventRules)
{
	if (!eventRules)
	{
		return false;
	}

	bool interrupted = false;
	if (!eventRules->getInterruptResearch().empty())
	{
		if (isResearched(eventRules->getInterruptResearch(), false))
		{
			interrupted = true;
		}
	}

	if (!interrupted)
	{
		addGeneratedEvent(eventRules);
		return true;
	}

	return false;
}

/**
 * Handles research unlocked by successful/failed missions and despawned mission sites.
 * 1. Adds the research topic to finished research list. Silently.
 * 2. Adds also getOneFree bonus and possible lookup(s). Also silently.
 * 3. Handles alien mission interruption.
 */
bool SavedGame::handleResearchUnlockedByMissions(const RuleResearch* research, const Mod* mod, const AlienDeployment* deployment)
{
	if (!research)
	{
		return false;
	}
	if (_bases.empty())
	{
		return false; // all bases lost, game over
	}
	Base* base = _bases.front();

	auto addResearchDiaryEntryForMission = [&](const RuleResearch* discoveredResearch, DiscoverySourceType sourceType, const AlienDeployment* sourceMission, const RuleResearch* sourceResearch)
	{
		if (!isResearched(discoveredResearch, false) && !isResearchRuleStatusDisabled(discoveredResearch->getName()))
		{
			ResearchDiaryEntry* entry = new ResearchDiaryEntry(discoveredResearch);
			entry->setDate(_time);
			entry->source.type = sourceType;
			if (sourceType == DiscoverySourceType::MISSION)
			{
				entry->source.mission = sourceMission;
				entry->source.name = sourceMission->getType();
			}
			else // sourceType == DiscoverySourceType::FREE_FROM
			{
				entry->source.research = sourceResearch;
				entry->source.name = sourceResearch->getName();
			}
			addResearchDiaryEntry(entry);
		}
	};

	std::vector<const RuleResearch*> researchVec;
	researchVec.push_back(research);
	addResearchDiaryEntryForMission(research, DiscoverySourceType::MISSION, deployment, nullptr);
	addFinishedResearch(research, mod, base, true);
	if (!research->getLookup().empty())
	{
		researchVec.push_back(mod->getResearch(research->getLookup(), true));
		addResearchDiaryEntryForMission(researchVec.back(), DiscoverySourceType::MISSION, deployment, nullptr);
		addFinishedResearch(researchVec.back(), mod, base, true);
	}

	if (auto bonus = selectGetOneFree(research))
	{
		researchVec.push_back(bonus);
		addResearchDiaryEntryForMission(bonus, DiscoverySourceType::FREE_FROM, nullptr, research);
		addFinishedResearch(bonus, mod, base, true);
		if (!bonus->getLookup().empty())
		{
			researchVec.push_back(mod->getResearch(bonus->getLookup(), true));
			addResearchDiaryEntryForMission(researchVec.back(), DiscoverySourceType::FREE_FROM, nullptr, research);
			addFinishedResearch(researchVec.back(), mod, base, true);
		}
	}

	// check and interrupt alien missions if necessary (based on unlocked research)
	for (auto* am : _activeMissions)
	{
		const auto& interruptResearchName = am->getRules().getInterruptResearch();
		if (!interruptResearchName.empty())
		{
			auto* interruptResearch = mod->getResearch(interruptResearchName, true);
			if (std::find(researchVec.begin(), researchVec.end(), interruptResearch) != researchVec.end())
			{
				am->setInterrupted(true);
			}
		}
	}

	return true;
}

/**
 * Handles research side effects for primary research sources.
 */
void SavedGame::handlePrimaryResearchSideEffects(const std::vector<const RuleResearch*> &topicsToCheck, const Mod* mod, Base* base)
{
	for (auto* myResearchRule : topicsToCheck)
	{
		// 3j. now iterate through all the bases and remove this project from their labs (unless it can still yield more stuff!)
		for (Base* otherBase : _bases)
		{
			for (ResearchProject* otherProject : otherBase->getResearch())
			{
				if (myResearchRule == otherProject->getRules())
				{
					if (hasUndiscoveredGetOneFree(myResearchRule, true))
					{
						// This research topic still has some more undiscovered non-disabled and *AVAILABLE* "getOneFree" topics, keep it!
					}
					else if (hasUndiscoveredProtectedUnlock(myResearchRule))
					{
						// This research topic still has one or more undiscovered non-disabled "protected unlocks", keep it!
					}
					else
					{
						// This topic can't give you anything else anymore, remove it!
						otherBase->removeResearch(otherProject);
						break;
					}
				}
			}
		}
		// 3k. handle spawned items
		RuleItem* spawnedItem = mod->getItem(myResearchRule->getSpawnedItem());
		if (spawnedItem)
		{
			Transfer* t = new Transfer(1);
			t->setItems(spawnedItem, std::max(1, myResearchRule->getSpawnedItemCount()));
			base->getTransfers()->push_back(t);
		}
		for (const auto& spawnedItemName2 : myResearchRule->getSpawnedItemList())
		{
			RuleItem* spawnedItem2 = mod->getItem(spawnedItemName2);
			if (spawnedItem2)
			{
				Transfer* t = new Transfer(1);
				t->setItems(spawnedItem2);
				base->getTransfers()->push_back(t);
			}
		}
		// 3l. handle spawned events
		RuleEvent* spawnedEventRule = mod->getEvent(myResearchRule->getSpawnedEvent());
		spawnEvent(spawnedEventRule);
		// 3m. handle counters
		for (auto& inc : myResearchRule->getIncreaseCounter())
		{
			increaseCustomCounter(inc);
		}
		for (auto& dec : myResearchRule->getDecreaseCounter())
		{
			decreaseCustomCounter(dec);
		}
	}
}

////////////////////////////////////////////////////////////
//					Script binding
////////////////////////////////////////////////////////////

namespace
{

void getRandomScript(SavedGame* sg, RNG::RandomState*& r)
{
	if (sg)
	{
		r = &RNG::globalRandomState();
	}
	else
	{
		r = nullptr;
	}
}

void getTimeScript(const SavedGame* sg, const GameTime*& r)
{
	if (sg)
	{
		r = sg->getTime();
	}
	else
	{
		r = nullptr;
	}
}

void randomChanceScript(RNG::RandomState* rs, int& val)
{
	if (rs)
	{
		val = rs->generate(0, 99) < val;
	}
	else
	{
		val = 0;
	}
}

void randomRangeScript(RNG::RandomState* rs, int& val, int min, int max)
{
	if (rs && max >= min)
	{
		val = rs->generate(min, max);
	}
	else
	{
		val = 0;
	}
}

void randomRangeSymmetricScript(RNG::RandomState* rs, int& val, int max)
{
	if (rs && max >= 0)
	{
		val = rs->generate(-max, max);
	}
	else
	{
		val = 0;
	}
}

void difficultyLevelScript(const SavedGame* sg, int& val)
{
	if (sg)
	{
		val = sg->getDifficulty();
	}
	else
	{
		val = 0;
	}
}

void getDaysPastEpochScript(const GameTime* p, int& val)
{
	if (p)
	{
		std::tm time = {  };
		time.tm_year = p->getYear() - 1900;
		time.tm_mon = p->getMonth() - 1;
		time.tm_mday = p->getDay();
		time.tm_hour = p->getHour();
		time.tm_min = p->getMinute();
		time.tm_sec = p->getSecond();
		val = (int)(std::mktime(&time) / (60 * 60 * 24));
	}
	else
	{
		val = 0;
	}
}

void getSecondsPastMidnightScript(const GameTime* p, int& val)
{
	if (p)
	{
		val = p->getSecond() +
			60 * p->getMinute() +
			60 * 60 * p->getHour();
	}
	else
	{
		val = 0;
	}
}

std::string debugDisplayScript(const RNG::RandomState* p)
{
	if (p)
	{
		std::string s;
		s += "RandomState";
		s += "(seed: \"";
		s += std::to_string(p->getSeed());
		s += "\")";
		return s;
	}
	else
	{
		return "null";
	}
}

std::string debugDisplayScript(const GameTime* p)
{
	if (p)
	{
		auto zeroPrefix = [](int i)
		{
			if (i < 10)
			{
				return "0" + std::to_string(i);
			}
			else
			{
				return std::to_string(i);
			}
		};

		std::string s;
		s += "Time";
		s += "(\"";
		s += std::to_string(p->getYear());
		s += "-";
		s += zeroPrefix(p->getMonth());
		s += "-";
		s += zeroPrefix(p->getDay());
		s += " ";
		s += zeroPrefix(p->getHour());
		s += ":";
		s += zeroPrefix(p->getMinute());
		s += ":";
		s += zeroPrefix(p->getSecond());
		s += "\")";
		return s;
	}
	else
	{
		return "null";
	}
}

void isResearchedScript(const SavedGame* sg, int& val, const RuleResearch* name)
{
	if (sg)
	{
		if (sg->isResearched(name, false))
		{
			val = 1;
			return;
		}
	}
	val = 0;
}

bool filterCountryConstScript(const SavedGame*, const Country*)
{
	return true;
}

bool filterCountryScript(SavedGame*, Country*)
{
	return true;
}

std::string debugDisplayScript(const SavedGame* p)
{
	if (p)
	{
		std::string s;
		s += SavedGame::ScriptName;
		s += "(fileName: \"";
		s += p->getName();
		s += "\" time: ";
		s += debugDisplayScript(p->getTime());
		s += ")";
		return s;
	}
	else
	{
		return "null";
	}
}

}

/**
 * Register SavedGame in script parser.
 * @param parser Script parser.
 */
void SavedGame::ScriptRegister(ScriptParserBase* parser)
{
	parser->registerPointerType<Country>();

	{
		const auto name = std::string{ "RandomState" };
		parser->registerRawPointerType<RNG::RandomState>(name);
		Bind<RNG::RandomState> rs = { parser, name };

		rs.add<&randomChanceScript>("randomChance", "Change value from range 0-100 to 0-1 based on probability");
		rs.add<&randomRangeScript>("randomRange", "Return random value from defined range");
		rs.add<&randomRangeSymmetricScript>("randomRangeSymmetric", "Return random value from negative to positive of given max value");

		rs.addDebugDisplay<&debugDisplayScript>();
	}

	{
		const auto name = std::string{ "Time" };
		parser->registerRawPointerType<GameTime>(name);
		Bind<GameTime> t = { parser, name };

		t.add<&GameTime::getSecond>("getSecond");
		t.add<&GameTime::getMinute>("getMinute");
		t.add<&GameTime::getHour>("getHour");
		t.add<&GameTime::getDay>("getDay");
		t.add<&GameTime::getMonth>("getMonth");
		t.add<&GameTime::getYear>("getYear");
		t.add<&getDaysPastEpochScript>("getDaysPastEpoch", "Days past 1970-01-01");
		t.add<&getSecondsPastMidnightScript>("getSecondsPastMidnight", "Seconds past 00:00");

		t.addDebugDisplay<&debugDisplayScript>();
	}

	Bind<SavedGame> sgg = { parser };

	sgg.add<&getTimeScript>("getTime", "Get global time that is Greenwich Mean Time");
	sgg.add<&getRandomScript>("getRandomState");

	sgg.add<&difficultyLevelScript>("difficultyLevel", "Get difficulty level");
	sgg.add<&SavedGame::getMonthsPassed>("getMonthsPassed", "Number of months passed from start");
	sgg.add<&SavedGame::getDaysPassed>("getDaysPassed", "Number of days passed from start");

	sgg.addList<&filterCountryConstScript, &SavedGame::_countries>("getCountries");
	sgg.addList<&filterCountryScript, &SavedGame::_countries>("getCountries");

	sgg.add<&isResearchedScript>("isResearched");

	sgg.addScriptValue<&SavedGame::_scriptValues>();

	sgg.addDebugDisplay<&debugDisplayScript>();
}

}
