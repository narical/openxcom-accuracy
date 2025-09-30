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
#include <assert.h>
#include <vector>
#include "BattleItem.h"
#include "ItemContainer.h"
#include "Base.h"
#include "Craft.h"
#include "SavedBattleGame.h"
#include "SavedGame.h"
#include "Tile.h"
#include "HitLog.h"
#include "Node.h"
#include "../Mod/MapDataSet.h"
#include "../Battlescape/Pathfinding.h"
#include "../Battlescape/TileEngine.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Battlescape/Inventory.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Engine/Game.h"
#include "../Engine/Sound.h"
#include "../Mod/RuleInventory.h"
#include "../Battlescape/AIModule.h"
#include "../Engine/RNG.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"
#include "../Engine/ScriptBind.h"
#include "SerializationHelper.h"
#include "../Mod/RuleStartingCondition.h"
#include "../Mod/RuleEnviroEffects.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/RuleSoldierBonus.h"
#include "../Mod/RuleWeaponSet.h"
#include "../fallthrough.h"
#include "../fmath.h"
#include "../Engine/Language.h"

namespace OpenXcom
{

/**
 * Initializes a brand new battlescape saved game.
 */
SavedBattleGame::SavedBattleGame(Mod *rule, Language *lang, bool isPreview) :
	_isPreview(isPreview), _craftPos(), _craftZ(0), _craftForPreview(nullptr),
	_battleState(0), _rule(rule), _mapsize_x(0), _mapsize_y(0), _mapsize_z(0), _selectedUnit(0), _undoUnit(nullptr),
	_lastSelectedUnit(0), _pathfinding(0), _tileEngine(0),
	_reinforcementsItemLevel(0), _startingCondition(nullptr), _enviroEffects(nullptr), _ecEnabledFriendly(false), _ecEnabledHostile(false), _ecEnabledNeutral(false),
	_globalShade(0), _side(FACTION_PLAYER), _turn(0), _bughuntMinTurn(20), _animFrame(0), _nameDisplay(false),
	_debugMode(false), _bughuntMode(false), _aborted(false), _itemId(0),
	_vipEscapeType(ESCAPE_NONE), _vipSurvivalPercentage(0), _vipsSaved(0), _vipsLost(0), _vipsWaitingOutside(0), _vipsSavedScore(0), _vipsLostScore(0), _vipsWaitingOutsideScore(0),
	_objectiveType(-1), _objectivesDestroyed(0), _objectivesNeeded(0),
	_unitsFalling(false), _cheating(false), _tuReserved(BA_NONE), _kneelReserved(false), _depth(0),
	_ambience(-1), _ambientVolume(0.5), _minAmbienceRandomDelay(20), _maxAmbienceRandomDelay(60), _currentAmbienceDelay(0),
	_turnLimit(0), _cheatTurn(20), _chronoTrigger(FORCE_LOSE), _beforeGame(true),
	_togglePersonalLight(true), _toggleNightVision(false), _toggleBrightness(0)
{
	_tileSearch.resize(11*11);
	for (int i = 0; i < 121; ++i)
	{
		_tileSearch[i].x = ((i%11) - 5);
		_tileSearch[i].y = ((i/11) - 5);
	}
	_baseItems = new ItemContainer();
	_hitLog = new HitLog(lang);

	setRandomHiddenMovementBackground(_rule);
}

/**
 * Deletes the game content from memory.
 */
SavedBattleGame::~SavedBattleGame()
{
	for (auto* mds : _mapDataSets)
	{
		mds->unloadData();
	}
	for (auto* node : _nodes)
	{
		delete node;
	}
	for (auto* bu : _units)
	{
		delete bu;
	}
	for (auto bi : _items)
	{
		delete bi;
	}
	for (auto* bi : _recoverGuaranteed)
	{
		delete bi;
	}
	for (auto* bi : _recoverConditional)
	{
		delete bi;
	}
	for (auto* bi : _deleted)
	{
		delete bi;
	}
	delete _pathfinding;
	delete _tileEngine;
	delete _baseItems;
	delete _hitLog;
}

/**
 * Loads the saved battle game from a YAML file.
 * @param node YAML node.
 * @param mod for the saved game.
 * @param savedGame Pointer to saved game.
 */
void SavedBattleGame::load(const YAML::YamlNodeReader& node, Mod *mod, SavedGame* savedGame)
{
	const auto& reader = node.useIndex();
	int mapsize_x = reader["width"].readVal(_mapsize_x);
	int mapsize_y = reader["length"].readVal(_mapsize_y);
	int mapsize_z = reader["height"].readVal(_mapsize_z);
	initMap(mapsize_x, mapsize_y, mapsize_z);

	reader.tryRead("missionType", _missionType);
	reader.tryRead("strTarget", _strTarget);
	reader.tryRead("strCraftOrBase", _strCraftOrBase);
	if (reader["startingConditionType"])
	{
		std::string startingConditionType = reader["startingConditionType"].readVal<std::string>();
		_startingCondition = mod->getStartingCondition(startingConditionType);
	}
	if (reader["enviroEffectsType"])
	{
		std::string enviroEffectsType = reader["enviroEffectsType"].readVal<std::string>();
		_enviroEffects = mod->getEnviroEffects(enviroEffectsType);
	}
	reader.tryRead("nameDisplay", _nameDisplay);
	reader.tryRead("ecEnabledFriendly", _ecEnabledFriendly);
	reader.tryRead("ecEnabledHostile", _ecEnabledHostile);
	reader.tryRead("ecEnabledNeutral", _ecEnabledNeutral);
	reader.tryRead("alienCustomDeploy", _alienCustomDeploy);
	reader.tryRead("alienCustomMission", _alienCustomMission);
	reader.tryRead("alienItemLevel", _alienItemLevel);
	reader.tryRead("lastUsedMapScript", _lastUsedMapScript);
	reader.tryRead("reinforcementsDeployment", _reinforcementsDeployment);
	reader.tryRead("reinforcementsRace", _reinforcementsRace);
	reader.tryRead("reinforcementsItemLevel", _reinforcementsItemLevel);
	reader.tryRead("reinforcementsMemory", _reinforcementsMemory);
	reader.tryRead("reinforcementsBlocks", _reinforcementsBlocks);
	reader.tryRead("flattenedMapTerrainNames", _flattenedMapTerrainNames);
	reader.tryRead("flattenedMapBlockNames", _flattenedMapBlockNames);
	reader.tryRead("globalshade", _globalShade);
	reader.tryRead("turn", _turn);
	reader.tryRead("bughuntMinTurn", _bughuntMinTurn);
	reader.tryRead("bughuntMode", _bughuntMode);
	reader.tryRead("depth", _depth);
	reader.tryRead("animFrame", _animFrame);
	int selectedUnitId = reader["selectedUnit"].readVal<int>();
	int undoUnitId = reader["undoUnit"].readVal<int>(-1);

	for (const auto& mdsReader : reader["mapdatasets"].children())
	{
		std::string name = mdsReader.readVal<std::string>();
		MapDataSet *mds = mod->getMapDataSet(name);
		_mapDataSets.push_back(mds);
	}

	if (!reader["tileTotalBytesPer"])
	{
		// binary tile data not found, load old-style text tiles :(
		for (const auto& tile : reader["tiles"].children())
		{
			Position pos = tile["position"].readVal<Position>();
			getTile(pos)->load(tile);
		}
	}
	else
	{
		// load key to how the tile data was saved
		Tile::SerializationKey serKey;
		size_t totalTiles = reader["totalTiles"].readVal<size_t>();

		memset(&serKey, 0, sizeof(Tile::SerializationKey));
		serKey.index = reader["tileIndexSize"].readVal<char>(serKey.index);
		serKey.totalBytes = reader["tileTotalBytesPer"].readVal<Uint32>(serKey.totalBytes);
		serKey._fire = reader["tileFireSize"].readVal<char>(serKey._fire);
		serKey._smoke = reader["tileSmokeSize"].readVal<char>(serKey._smoke);
		serKey._mapDataID = reader["tileIDSize"].readVal<char>(serKey._mapDataID);
		serKey._mapDataSetID = reader["tileSetIDSize"].readVal<char>(serKey._mapDataSetID);
		serKey.boolFields = reader["tileBoolFieldsSize"].readVal<char>(1); // boolean flags used to be stored in an unmentioned byte (Uint8) :|
		serKey._lastExploredByPlayer = reader["lastExploredByPlayer"].readVal<char>(serKey._lastExploredByPlayer);
		serKey._lastExploredByHostile = reader["lastExploredByHostile"].readVal<char>(serKey._lastExploredByHostile);
		serKey._lastExploredByNeutral = reader["lastExploredByNeutral"].readVal<char>(serKey._lastExploredByNeutral);
		

		// load binary tile data!
		std::vector<char> binTiles = reader["binTiles"].readValBase64();

		Uint8* ptr = (Uint8*)binTiles.data();
		Uint8* dataEnd = ptr + totalTiles * serKey.totalBytes;

		while (ptr < dataEnd)
		{
			int index = unserializeInt(&ptr, serKey.index);
			assert(index >= 0 && index < _mapsize_x * _mapsize_z * _mapsize_y);
			_tiles[index].loadBinary(ptr, serKey); // loadBinary's privileges to advance *r have been revoked
			ptr += serKey.totalBytes - serKey.index; // r is now incremented strictly by totalBytes in case there are obsolete fields present in the data
		}
	}

	if (_missionType == "STR_BASE_DEFENSE")
	{
		if (!reader.tryRead("moduleMap", _baseModules))
		{
			// backwards compatibility: imperfect solution, modules that were completely destroyed
			// prior to saving and updating builds will be counted as indestructible.
			calculateModuleMap();
		}
	}
	for (const auto& nodeConfig : reader["nodes"].children())
	{
		Node *n = new Node();
		n->load(nodeConfig);
		_nodes.push_back(n);
	}

	//always reserve the sizes of your collections if you can
	_units.reserve(reader["units"].childrenCount());
	_items.reserve(reader["items"].childrenCount() + reader["itemsSpecial"].childrenCount());
	_recoverConditional.reserve(reader["recoverConditional"].childrenCount());
	_recoverGuaranteed.reserve(reader["recoverGuaranteed"].childrenCount());
	// int -> BattleUnit; dictionaries for O(1) ID searching
	std::unordered_map<int, BattleUnit*> unitIndex;
	std::unordered_map<int, BattleItem*> itemIndex;
	unitIndex.reserve(_units.capacity());
	itemIndex.reserve(_items.capacity() + _recoverConditional.capacity() + _recoverGuaranteed.capacity());

	auto findUnitById = [&](const YAML::YamlNodeReader& r) -> BattleUnit*
	{
		int id = r.readVal(-1);
		if (id == -1 || !unitIndex.count(id))
			return nullptr;
		return unitIndex.at(id);
	};

	// units 1st pass
	for (const auto& unitReader : reader["units"].children())
	{
		UnitFaction faction = unitReader["faction"].readVal<UnitFaction>();
		UnitFaction originalFaction = unitReader["originalFaction"].readVal(faction);
		int id = unitReader["id"].readVal<int>();
		BattleUnit* unit;
		if (id < BattleUnit::MAX_SOLDIER_ID) // Unit is linked to a geoscape soldier
			unit = new BattleUnit(mod, savedGame->getSoldier(id), _depth, nullptr); // look up the matching soldier
		else
		{
			std::string type = unitReader["genUnitType"].readVal<std::string>();
			std::string armor = unitReader["genUnitArmor"].readVal<std::string>();
			// create a new Unit.
			if (!mod->getUnit(type) || !mod->getArmor(armor))
				continue;
			unit = new BattleUnit(mod, mod->getUnit(type), originalFaction, id, nullptr, mod->getArmor(armor), mod->getStatAdjustment(savedGame->getDifficulty()), _depth, nullptr);
		}
		unit->load(unitReader, this->getMod(), this->getMod()->getScriptGlobal());
		// Handling of special built-in weapons will be done during and after the load of items
		// unit->setSpecialWeapon(this, true);
		if (faction == FACTION_PLAYER)
		{
			if ((unit->getId() == selectedUnitId) || (_selectedUnit == 0 && !unit->isOut()))
				_selectedUnit = unit;
			if (unit->getId() == undoUnitId)
				_undoUnit = unit;
		}
		else if (unit->getStatus() != STATUS_DEAD && !unit->isIgnored())
		{
			if (const auto& ai = unitReader["AI"])
			{
				AIModule* aiModule = new AIModule(this, unit, 0);
				aiModule->load(ai);
				unit->setAIModule(aiModule);
			}
		}
		unitIndex[id] = unit;
		_units.push_back(unit);
	}

	std::pair<const char*, std::vector<BattleItem*>*> itemKeysAndVectors[] =
	{
		{ "items", &_items },
		{ "recoverConditional", &_recoverConditional },
		{ "recoverGuaranteed", &_recoverGuaranteed },
		{ "itemsSpecial", &_items },
	};
	// items 1st pass
	for (auto& keyAndVector : itemKeysAndVectors)
	{
		for (const auto& itemReader : reader[keyAndVector.first].children())
		{
			std::string type = itemReader["type"].readVal<std::string>();
			if (!mod->getItem(type))
			{
				Log(LOG_ERROR) << "Failed to load item " << type;
				continue;
			}
			int id = itemReader["id"].readVal<int>();
			BattleItem* item = new BattleItem(mod->getItem(type), &id); //passing id as a pointer to serve as a counter is no longer used
			item->load(itemReader, mod, this->getMod()->getScriptGlobal());

			if (BattleUnit* owner = findUnitById(itemReader["owner"]))
			{
				item->setOwner(owner);
				if (item->isSpecialWeapon())
					owner->addLoadedSpecialWeapon(item);
				else
					owner->getInventory()->push_back(item);
			}
			item->setPreviousOwner(findUnitById(itemReader["previousOwner"]));
			item->setUnit(findUnitById(itemReader["unit"]));

			// match up items and tiles
			if (item->getSlot() && item->getSlot()->getType() == INV_GROUND)
			{
				Position pos = itemReader["position"].readVal(Position(-1, -1, -1));
				if (pos.x != -1)
					getTile(pos)->addItem(item, item->getSlot());
			}
			_itemId = std::max(_itemId, item->getId());
			itemIndex[item->getId()] = item;
			keyAndVector.second->push_back(item);
		}
	}
	_itemId++;

	// units 2nd pass
	for (const auto& unitReader : reader["units"].children())
	{
		if (BattleUnit* bu = findUnitById(unitReader["id"])) //not guaranteed that the unit was created
		{
			if (!bu->isIgnored() && bu->getStatus() != STATUS_DEAD)
				bu->setSpecialWeapon(this, true); // Note: this is for backwards-compatibility with older saves
			bu->setPreviousOwner(findUnitById(unitReader["previousOwner"]));
		}
	}

	// items 2nd pass
	for (auto& keyAndVector : itemKeysAndVectors)
	{
		for (const auto& itemReader : reader[keyAndVector.first].children())
		{
			std::string type = itemReader["type"].readVal<std::string>();
			if (!mod->getItem(type))
				continue;
			BattleItem* item = itemIndex.at(itemReader["id"].readVal<int>());
			// if "ammoItemSlots" node exists, then link ammo items for all slots, else try the backwards-compatibility node
			if (const auto& slotsReader = itemReader["ammoItemSlots"])
				for (size_t slotIndex = 0; slotIndex < RuleItem::AmmoSlotMax; slotIndex++)
				{
					int itemId = slotsReader[slotIndex].readVal(-1);
					if (itemId > -1 && itemIndex.count(itemId))
						item->setAmmoForSlot(slotIndex, itemIndex.at(itemId));
				}
			else if (const auto& slotReader = itemReader["ammoItem"])
				item->setAmmoForSlot(0, itemIndex.at(slotReader.readVal(0)));
		}
	}

	// restore order like before save
	// is this necessary? who knows...
	for (auto& keyAndVector : itemKeysAndVectors)
	{
		std::sort(
			keyAndVector.second->begin(), keyAndVector.second->end(),
			[](const BattleItem* itemA, const BattleItem* itemB)
			{
				return itemA->getId() < itemB->getId();
			}
		);
	}

	reader.tryRead("vipEscapeType", _vipEscapeType);
	reader.tryRead("vipSurvivalPercentage", _vipSurvivalPercentage);
	reader.tryRead("vipsSaved", _vipsSaved);
	reader.tryRead("vipsLost", _vipsLost);
	reader.tryRead("vipsWaitingOutside", _vipsWaitingOutside);
	reader.tryRead("vipsSavedScore", _vipsSavedScore);
	reader.tryRead("vipsLostScore", _vipsLostScore);
	reader.tryRead("vipsWaitingOutsideScore", _vipsWaitingOutsideScore);
	reader.tryRead("objectiveType", _objectiveType);
	reader.tryRead("objectivesDestroyed", _objectivesDestroyed);
	reader.tryRead("objectivesNeeded", _objectivesNeeded);
	reader.tryRead("tuReserved", _tuReserved);
	reader.tryRead("kneelReserved", _kneelReserved);
	reader.tryRead("ambience", _ambience);
	reader.tryRead("ambientVolume", _ambientVolume);
	reader.tryRead("ambienceRandom", _ambienceRandom);
	reader.tryRead("minAmbienceRandomDelay", _minAmbienceRandomDelay);
	reader.tryRead("maxAmbienceRandomDelay", _maxAmbienceRandomDelay);
	reader.tryRead("currentAmbienceDelay", _currentAmbienceDelay);
	reader.tryRead("music", _music);
	_baseItems->load(reader["baseItems"], mod);
	reader.tryRead("turnLimit", _turnLimit);
	reader.tryRead("chronoTrigger", _chronoTrigger);
	reader.tryRead("cheatTurn", _cheatTurn);
	reader.tryRead("togglePersonalLight", _togglePersonalLight);
	reader.tryRead("toggleNightVision", _toggleNightVision);
	reader.tryRead("toggleBrightness", _toggleBrightness);
	_scriptValues.load(reader, _rule->getScriptGlobal());

	// Sanity checks
	for (auto* unit : _units)
	{
		switch (unit->getStatus())
		{
		case STATUS_STANDING:
		case STATUS_DEAD:
		case STATUS_UNCONSCIOUS:
		case STATUS_IGNORE_ME:
			break;
		default:
			Log(LOG_ERROR) << "Save '" << savedGame->getName()
				<< "' is corrupted. Unit " << unit->getType()
				<< " (id: " << unit->getId()
				<< ") has an invalid 'status: " << unit->getStatus() << "'";
			break;
		}
	}
}

/**
 * Loads the resources required by the map in the battle save.
 * @param mod Pointer to the mod.
 */
void SavedBattleGame::loadMapResources(Mod *mod)
{
	for (auto* mds : _mapDataSets)
	{
		mds->loadData(mod->getMCDPatch(mds->getName()));
	}

	int mdsID, mdID;

	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		for (int part = O_FLOOR; part < O_MAX; part++)
		{
			TilePart tp = (TilePart)part;
			_tiles[i].getMapData(&mdID, &mdsID, tp);
			if (mdID != -1 && mdsID != -1)
			{
				_tiles[i].setMapData(_mapDataSets[mdsID]->getObject(mdID), mdID, mdsID, tp);
			}
			else
			{
				_tiles[i].setMapData(nullptr, -1, -1, tp);
			}
		}
	}

	initUtilities(mod);
	// matches up tiles and units
	resetUnitTiles();
	getTileEngine()->calculateLighting(LL_AMBIENT, TileEngine::invalid, 0, true);
	getTileEngine()->recalculateFOV();
}

/**
 * Saves the saved battle game to a YAML file.
 * @return YAML node.
 */
void SavedBattleGame::save(YAML::YamlNodeWriter writer) const
{
	writer.setAsMap();
	if (_vipSurvivalPercentage > 0)
	{
		writer.write("vipEscapeType", _vipEscapeType);
		writer.write("vipSurvivalPercentage", _vipSurvivalPercentage);
		writer.write("vipsSaved", _vipsSaved);
		writer.write("vipsLost", _vipsLost);
		writer.write("vipsWaitingOutside", _vipsWaitingOutside);
		writer.write("vipsSavedScore", _vipsSavedScore);
		writer.write("vipsLostScore", _vipsLostScore);
		writer.write("vipsWaitingOutsideScore", _vipsWaitingOutsideScore);
	}
	if (_objectivesNeeded)
	{
		writer.write("objectivesDestroyed", _objectivesDestroyed);
		writer.write("objectivesNeeded", _objectivesNeeded);
		writer.write("objectiveType", _objectiveType);
	}
	writer.write("width", _mapsize_x);
	writer.write("length", _mapsize_y);
	writer.write("height", _mapsize_z);
	writer.write("missionType", _missionType);
	writer.write("strTarget", _strTarget);
	writer.write("strCraftOrBase", _strCraftOrBase).setAsQuotedAndEscaped();
	if (_startingCondition)
	{
		writer.write("startingConditionType", _startingCondition->getType());
	}
	if (_enviroEffects)
	{
		writer.write("enviroEffectsType", _enviroEffects->getType());
	}
	writer.write("nameDisplay", _nameDisplay);
	writer.write("ecEnabledFriendly", _ecEnabledFriendly);
	writer.write("ecEnabledHostile", _ecEnabledHostile);
	writer.write("ecEnabledNeutral", _ecEnabledNeutral);
	writer.write("alienCustomDeploy", _alienCustomDeploy);
	writer.write("alienCustomMission", _alienCustomMission);
	writer.write("alienItemLevel", _alienItemLevel);
	writer.write("lastUsedMapScript", _lastUsedMapScript);
	writer.write("reinforcementsDeployment", _reinforcementsDeployment);
	writer.write("reinforcementsRace", _reinforcementsRace);
	writer.write("reinforcementsItemLevel", _reinforcementsItemLevel);
	writer.write("reinforcementsMemory", _reinforcementsMemory);
	writer.write("reinforcementsBlocks", _reinforcementsBlocks);
	writer.write("flattenedMapTerrainNames", _flattenedMapTerrainNames);
	writer.write("flattenedMapBlockNames", _flattenedMapBlockNames);
	writer.write("globalshade", _globalShade);
	writer.write("turn", _turn);
	writer.write("bughuntMinTurn", _bughuntMinTurn);
	writer.write("animFrame", _animFrame);
	writer.write("bughuntMode", _bughuntMode);
	writer.write("selectedUnit", (_selectedUnit ? _selectedUnit->getId() : -1));
	writer.write("undoUnit", (_undoUnit ? _undoUnit->getId() : -1));

	writer.write("mapdatasets", _mapDataSets,
		[](YAML::YamlNodeWriter& w, MapDataSet* mds)
		{ w.write(mds->getName()); });
#if 0
	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		if (!_tiles[i].isVoid())
		{
			node["tiles"].push_back(_tiles[i].save());
		}
	}
#else
	// first, write out the field sizes we're going to use to write the tile data
	writer.write("tileIndexSize", static_cast<char>(Tile::serializationKey.index)).setAsQuotedAndEscaped();
	writer.write("tileTotalBytesPer", Tile::serializationKey.totalBytes);
	writer.write("tileFireSize", static_cast<char>(Tile::serializationKey._fire)).setAsQuotedAndEscaped();
	writer.write("tileSmokeSize", static_cast<char>(Tile::serializationKey._smoke)).setAsQuotedAndEscaped();
	writer.write("tileIDSize", static_cast<char>(Tile::serializationKey._mapDataID)).setAsQuotedAndEscaped();
	writer.write("tileSetIDSize", static_cast<char>(Tile::serializationKey._mapDataSetID)).setAsQuotedAndEscaped();
	writer.write("tileBoolFieldsSize", static_cast<char>(Tile::serializationKey.boolFields)).setAsQuotedAndEscaped();
	writer.write("lastExploredByPlayer", static_cast<char>(Tile::serializationKey._lastExploredByPlayer)).setAsQuotedAndEscaped();
	writer.write("lastExploredByNeutral", static_cast<char>(Tile::serializationKey._lastExploredByNeutral)).setAsQuotedAndEscaped();
	writer.write("lastExploredByHostile", static_cast<char>(Tile::serializationKey._lastExploredByHostile)).setAsQuotedAndEscaped();

	size_t tileDataSize = Tile::serializationKey.totalBytes * _mapsize_z * _mapsize_y * _mapsize_x;
	Uint8* tileData = (Uint8*) calloc(tileDataSize, 1);
	Uint8* ptr = tileData;

	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		if (!_tiles[i].isVoid())
		{
			serializeInt(&ptr, Tile::serializationKey.index, i);
			_tiles[i].saveBinary(&ptr);
		}
		else
		{
			tileDataSize -= Tile::serializationKey.totalBytes;
		}
	}
	writer.write("totalTiles", tileDataSize / Tile::serializationKey.totalBytes); // not strictly necessary, just convenient
	writer.writeBase64("binTiles", (char*)tileData, tileDataSize);
	free(tileData);
#endif

	writer.write("nodes", _nodes,
		[](YAML::YamlNodeWriter& w, Node* n)
		{ n->save(w.write()); });
	if (_missionType == "STR_BASE_DEFENSE")
		writer.write("moduleMap", _baseModules);
	writer.write("units", _units,
		[&](YAML::YamlNodeWriter& w, BattleUnit* bu)
		{ bu->save(w.write(), this->getMod()->getScriptGlobal()); });
	writer.write("items", _items,
		[&](YAML::YamlNodeWriter& w, BattleItem* bi)
		{ if (!bi->isSpecialWeapon()) bi->save(w.write(), this->getMod()->getScriptGlobal()); });
	writer.write("itemsSpecial", _items,
		[&](YAML::YamlNodeWriter& w, BattleItem* bi)
		{ if (bi->isSpecialWeapon()) bi->save(w.write(), this->getMod()->getScriptGlobal()); });
	writer.write("tuReserved", (int)_tuReserved);
	writer.write("kneelReserved", _kneelReserved);
	writer.write("depth", _depth);
	writer.write("ambience", _ambience);
	writer.write("ambientVolume", _ambientVolume);
	writer.write("ambienceRandom", _ambienceRandom);
	writer.write("minAmbienceRandomDelay", _minAmbienceRandomDelay);
	writer.write("maxAmbienceRandomDelay", _maxAmbienceRandomDelay);
	writer.write("currentAmbienceDelay", _currentAmbienceDelay);
	writer.write("recoverGuaranteed", _recoverGuaranteed,
		[&](YAML::YamlNodeWriter& w, BattleItem* bi)
		{ bi->save(w.write(), this->getMod()->getScriptGlobal()); });
	writer.write("recoverConditional", _recoverConditional,
		[&](YAML::YamlNodeWriter& w, BattleItem* bi)
		{ bi->save(w.write(), this->getMod()->getScriptGlobal()); });
	writer.write("music", _music);
	_baseItems->save(writer["baseItems"]);
	writer.write("turnLimit", _turnLimit);
	writer.write("chronoTrigger", _chronoTrigger);
	writer.write("cheatTurn", _cheatTurn);
	writer.write("togglePersonalLight", _togglePersonalLight);
	writer.write("toggleNightVision", _toggleNightVision);
	writer.write("toggleBrightness", _toggleBrightness);
	_scriptValues.save(writer, _rule->getScriptGlobal());
}

/**
 * Initializes the array of tiles and creates a pathfinding object.
 * @param mapsize_x
 * @param mapsize_y
 * @param mapsize_z
 */
void SavedBattleGame::initMap(int mapsize_x, int mapsize_y, int mapsize_z, bool resetTerrain)
{
	// Clear old map data
	for (auto* node : _nodes)
	{
		delete node;
	}

	_nodes.clear();

	if (resetTerrain)
	{
		_mapDataSets.clear();
	}

	// Create tile objects
	_mapsize_x = mapsize_x;
	_mapsize_y = mapsize_y;
	_mapsize_z = mapsize_z;

	_tiles.clear();
	_tiles.reserve(_mapsize_z * _mapsize_y * _mapsize_x);
	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		_tiles.push_back(Tile(getTileCoords(i), this));
	}

}

/**
 * Initializes the map utilities.
 * @param mod Pointer to mod.
 */
void SavedBattleGame::initUtilities(Mod *mod, bool craftInventory)
{
	delete _pathfinding;
	delete _tileEngine;
	_baseCraftInventory = craftInventory;
	_pathfinding = craftInventory ? nullptr : new Pathfinding(this);
	_tileEngine = new TileEngine(this, mod);
}

/**
 * Gets if this is craft pre-equip phase in base view.
 * @return True if it in base equip screen.
 */
bool SavedBattleGame::isBaseCraftInventory()
{
	return _baseCraftInventory;
}

/**
 * Sets the mission type.
 * @param missionType The mission type.
 */
void SavedBattleGame::setMissionType(const std::string &missionType)
{
	_missionType = missionType;
}

/**
 * Gets the mission type.
 * @return The mission type.
 */
const std::string &SavedBattleGame::getMissionType() const
{
	return _missionType;
}

/**
* Returns the list of items in the base storage rooms BEFORE the mission.
* Does NOT return items assigned to craft or in transfer.
* @return Pointer to the item list.
*/
ItemContainer *SavedBattleGame::getBaseStorageItems()
{
	return _baseItems;
}

/**
 * Applies the enviro effects.
 * @param enviroEffects The enviro effects.
 */
void SavedBattleGame::applyEnviroEffects(const RuleEnviroEffects* enviroEffects)
{
	_enviroEffects = enviroEffects;

	_ecEnabledFriendly = false;
	_ecEnabledHostile = false;
	_ecEnabledNeutral = false;

	if (_enviroEffects)
	{
		_ecEnabledFriendly = RNG::percent(_enviroEffects->getEnvironmetalCondition("STR_FRIENDLY").globalChance);
		_ecEnabledHostile = RNG::percent(_enviroEffects->getEnvironmetalCondition("STR_HOSTILE").globalChance);
		_ecEnabledNeutral = RNG::percent(_enviroEffects->getEnvironmetalCondition("STR_NEUTRAL").globalChance);
	}
}

/**
 * Gets the enviro effects.
 * @return The enviro effects.
 */
const RuleEnviroEffects* SavedBattleGame::getEnviroEffects() const
{
	return _enviroEffects;
}

/**
 * Are environmental conditions (for a given faction) enabled?
 * @param faction The relevant faction.
 * @return True, if enabled.
 */
bool SavedBattleGame::getEnvironmentalConditionsEnabled(UnitFaction faction) const
{
	if (faction == FACTION_PLAYER)
	{
		return _ecEnabledFriendly;
	}
	else if (faction == FACTION_HOSTILE)
	{
		return _ecEnabledHostile;
	}
	else if (faction == FACTION_NEUTRAL)
	{
		return _ecEnabledNeutral;
	}
	return false;
}

/**
 *  Sets the custom alien data.
 */
void SavedBattleGame::setAlienCustom(const std::string &deploy, const std::string &mission)
{
	_alienCustomDeploy = deploy;
	_alienCustomMission = mission;
}

/**
 *  Gets the custom alien deploy.
 */
const std::string &SavedBattleGame::getAlienCustomDeploy() const
{
	return _alienCustomDeploy;
}

/**
 *  Gets the custom mission definition
 */
const std::string &SavedBattleGame::getAlienCustomMission() const
{
	return _alienCustomMission;
}

/**
 * Sets the global shade.
 * @param shade The global shade.
 */
void SavedBattleGame::setGlobalShade(int shade)
{
	_globalShade = shade;
}

/**
 * Gets the global shade.
 * @return The global shade.
 */
int SavedBattleGame::getGlobalShade() const
{
	return _globalShade;
}

/**
 * Pre-calculate all valid tiles for later use in map drawing.
 */
void SavedBattleGame::calculateCraftTiles()
{
	if (_craftForPreview && !_craftForPreview->getRules()->getDeployment().empty() && !_craftForPreview->getRules()->useAllStartTiles())
	{
		for (auto& vec : _craftForPreview->getRules()->getDeployment())
		{
			if (vec.size() >= 3)
			{
				Position tmp = Position(vec[0] + _craftPos.x * 10, vec[1] + _craftPos.y * 10, vec[2] + _craftZ);
				_craftTiles.push_back(tmp);
			}
		}
	}
	else
	{
		for (int i = 0; i < getMapSizeXYZ(); ++i)
		{
			auto* tile = getTile(i);
			if (tile &&
				tile->getFloorSpecialTileType() == START_POINT &&
				!tile->getMapData(O_OBJECT) &&
				tile->getMapData(O_FLOOR) && // for clarity this is checked again, first time was in `getFloorSpecialTileType`
				tile->getMapData(O_FLOOR)->getTUCost(MT_WALK) != Pathfinding::INVALID_MOVE_COST)
			{
				_craftTiles.push_back(tile->getPosition());
			}
		}
	}
}

/**
 * Converts a tile index to coordinates.
 * @param index The (unique) tile index.
 * @param x Pointer to the X coordinate.
 * @param y Pointer to the Y coordinate.
 * @param z Pointer to the Z coordinate.
 */
Position SavedBattleGame::getTileCoords(int index) const
{
	if (index == -1)
		return TileEngine::invalid;
	Position p;
	p.z = index / (_mapsize_y * _mapsize_x);
	p.y = (index % (_mapsize_y * _mapsize_x)) / _mapsize_x;
	p.x = (index % (_mapsize_y * _mapsize_x)) % _mapsize_x;
	return p;
}

/**
 * Gets the currently selected unit
 * @return Pointer to BattleUnit.
 */
BattleUnit *SavedBattleGame::getSelectedUnit() const
{
	return _selectedUnit;
}

/**
 * Sets the currently selected unit.
 * @param unit Pointer to BattleUnit.
 */
void SavedBattleGame::setSelectedUnit(BattleUnit *unit)
{
	_selectedUnit = unit;
}

/**
 * Clear state that given unit is selected.
 */
void SavedBattleGame::clearUnitSelection(BattleUnit *unit)
{
	if (_selectedUnit == unit)
	{
		_selectedUnit = nullptr;
	}
	if (_undoUnit == unit)
	{
		_undoUnit = nullptr;
	}
	if (_lastSelectedUnit == unit)
	{
		_lastSelectedUnit = nullptr;
	}
}

/**
 * Selects the previous player unit.
 * @param checkReselect Whether to check if we should reselect a unit.
 * @param setReselect Don't reselect a unit.
 * @param checkInventory Whether to check if the unit has an inventory.
 * @return Pointer to new selected BattleUnit, NULL if none can be selected.
 * @sa selectPlayerUnit
 */
BattleUnit *SavedBattleGame::selectPreviousPlayerUnit(bool checkReselect, bool setReselect, bool checkInventory)
{
	return selectPlayerUnit(-1, checkReselect, setReselect, checkInventory);
}

/**
 * Selects the next player unit.
 * @param checkReselect Whether to check if we should reselect a unit.
 * @param setReselect Don't reselect a unit.
 * @param checkInventory Whether to check if the unit has an inventory.
 * @return Pointer to new selected BattleUnit, NULL if none can be selected.
 * @sa selectPlayerUnit
 */
BattleUnit *SavedBattleGame::selectNextPlayerUnit(bool checkReselect, bool setReselect, bool checkInventory)
{
	return selectPlayerUnit(+1, checkReselect, setReselect, checkInventory);
}

/**
 * Selects the next player unit in a certain direction.
 * @param dir Direction to select, eg. -1 for previous and 1 for next.
 * @param checkReselect Whether to check if we should reselect a unit.
 * @param setReselect Don't reselect a unit.
 * @param checkInventory Whether to check if the unit has an inventory.
 * @return Pointer to new selected BattleUnit, NULL if none can be selected.
 */
BattleUnit *SavedBattleGame::selectPlayerUnit(int dir, bool checkReselect, bool setReselect, bool checkInventory)
{
	if (_selectedUnit != 0 && setReselect)
	{
		_selectedUnit->dontReselect();
	}
	if (_units.empty())
	{
		return 0;
	}

	std::vector<BattleUnit*>::iterator begin, end;
	if (dir > 0)
	{
		begin = _units.begin();
		end = _units.end()-1;
	}
	else if (dir < 0)
	{
		begin = _units.end()-1;
		end = _units.begin();
	}

	auto i = std::find(_units.begin(), _units.end(), _selectedUnit);
	do
	{
		// no unit selected
		if (i == _units.end())
		{
			i = begin;
			continue;
		}
		if (i != end)
		{
			i += dir;
		}
		// reached the end, wrap-around
		else
		{
			i = begin;
		}
		// back to where we started... no more units found
		if (*i == _selectedUnit)
		{
			if (checkReselect && !_selectedUnit->reselectAllowed())
				_selectedUnit = 0;
			return _selectedUnit;
		}
		else if (_selectedUnit == 0 && i == begin)
		{
			return _selectedUnit;
		}
	}
	while (!(*i)->isSelectable(_side, checkReselect, checkInventory));

	_selectedUnit = (*i);
	return _selectedUnit;
}

/**
 * Selects the next closest player unit.
 * @param checkReselect Whether to check if we should reselect a unit.
 * @param setReselect Don't reselect a unit.
 * @param checkInventory Whether to check if the unit has an inventory.
 * @return Pointer to new selected BattleUnit, NULL if none can be selected.
 */
BattleUnit* SavedBattleGame::selectNextPlayerUnitByDistance(bool checkReselect, bool setReselect, bool checkInventory)
{
	BattleUnit* backup = _selectedUnit;
	if (_selectedUnit != 0 && setReselect)
	{
		_selectedUnit->dontReselect();
		_selectedUnit = 0;
	}

	std::vector< std::pair<int, BattleUnit*> > candidates;
	for (auto* unit : _units)
	{
		if (unit != _selectedUnit && unit->isSelectable(_side, checkReselect, checkInventory))
		{
			int distance = backup ? backup->distance3dToUnitSq(unit) : 0;
			candidates.push_back(std::make_pair(distance, unit));
		}
	}

	if (!candidates.empty())
	{
		std::sort(candidates.begin(), candidates.end(),
			[](const std::pair<int, BattleUnit*>& a, const std::pair<int, BattleUnit*>& b)
			{ return a.first < b.first; });

		_selectedUnit = candidates.front().second;
	}
	return _selectedUnit;
}

/**
 * Selects the unit at the given position on the map.
 * @param pos Position.
 * @return Pointer to a BattleUnit, or 0 when none is found.
 */
BattleUnit *SavedBattleGame::selectUnit(Position pos)
{
	BattleUnit *bu = getTile(pos)->getUnit();

	if (bu && bu->isOut())
	{
		return 0;
	}
	else
	{
		return bu;
	}
}

/**
 * Gets the list of nodes.
 * @return Pointer to the list of nodes.
 */
std::vector<Node*> *SavedBattleGame::getNodes()
{
	return &_nodes;
}

/**
 * Gets the list of units.
 * @return Pointer to the list of units.
 */
std::vector<BattleUnit*> *SavedBattleGame::getUnits()
{
	return &_units;
}

/**
 * Gets the list of items.
 * @return Pointer to the list of items.
 */
std::vector<BattleItem*> *SavedBattleGame::getItems()
{
	return &_items;
}

/**
 * Gets the pathfinding object.
 * @return Pointer to the pathfinding object.
 */
Pathfinding *SavedBattleGame::getPathfinding() const
{
	return _pathfinding;
}

/**
 * Gets the terrain modifier object.
 * @return Pointer to the terrain modifier object.
 */
TileEngine *SavedBattleGame::getTileEngine() const
{
	return _tileEngine;
}

/**
 * Gets the array of mapblocks.
 * @return Pointer to the array of mapblocks.
 */
std::vector<MapDataSet*> *SavedBattleGame::getMapDataSets()
{
	return &_mapDataSets;
}

/**
 * Gets the side currently playing.
 * @return The unit faction currently playing.
 */
UnitFaction SavedBattleGame::getSide() const
{
	return _side;
}

/**
 * Test if weapon is usable by unit.
 * @param weapon
 * @param unit
 * @return Unit can shoot/use it.
 */
bool SavedBattleGame::canUseWeapon(const BattleItem* weapon, const BattleUnit* unit, bool isBerserking, BattleActionType actionType, std::string* message) const
{
	if (!weapon || !unit) return false;

	const RuleItem *rule = weapon->getRules();

	const BattleItem* ammoItem;
	if (actionType != BA_NONE)
	{
		// Applies to:
		// 1. action type selected by the player from the UI
		// 2. leeroy jenkins AI
		// 3. all reaction fire
		// 4. all unit berserking
		ammoItem = weapon->getAmmoForAction(actionType);
	}
	else
	{
		// Applies to:
		// 5. standard AI - action type (and thus ammoItem) is unknown when the check is done
		ammoItem = nullptr;
	}

	if (unit->getFaction() == FACTION_HOSTILE && getTurn() < rule->getAIUseDelay(getMod()))
	{
		return false;
	}
	if (unit->getOriginalFaction() == FACTION_PLAYER && !_battleState->getGame()->getSavedGame()->isResearched(rule->getRequirements()))
	{
		return false;
	}
	if (rule->isPsiRequired() && unit->getBaseStats()->psiSkill <= 0)
	{
		return false;
	}
	if (rule->isManaRequired() && unit->getOriginalFaction() == FACTION_PLAYER)
	{
		if (!_rule->isManaFeatureEnabled() || !_battleState->getGame()->getSavedGame()->isManaUnlocked(_rule))
		{
			return false;
		}
	}
	if (getDepth() == 0)
	{
		if (rule->isWaterOnly() || (ammoItem && ammoItem->getRules()->isWaterOnly()) )
		{
			if (message) *message = "STR_UNDERWATER_EQUIPMENT";
			return false;
		}
	}
	else // if (getDepth() != 0)
	{
		if (rule->isLandOnly() || (ammoItem && ammoItem->getRules()->isLandOnly()) )
		{
			if (message) *message = "STR_LAND_EQUIPMENT";
			return false;
		}
	}
	if (rule->isBlockingBothHands() && unit->getFaction() == FACTION_PLAYER && !isBerserking && unit->getLeftHandWeapon() != 0 && unit->getRightHandWeapon() != 0)
	{
		if (message) *message = "STR_MUST_USE_BOTH_HANDS";
		return false;
	}
	return true;
}

/**
 * Gets the current turn number.
 * @return The current turn.
 */
int SavedBattleGame::getTurn() const
{
	return _turn;
}

/**
* Sets the bug hunt turn number.
*/
void SavedBattleGame::setBughuntMinTurn(int bughuntMinTurn)
{
	_bughuntMinTurn = bughuntMinTurn;
}

/**
* Gets the bug hunt turn number.
* @return The bug hunt turn.
*/
int SavedBattleGame::getBughuntMinTurn() const
{
	return _bughuntMinTurn;
}

/**
 * Start first turn of battle.
 */
void SavedBattleGame::startFirstTurn()
{
	// this should be first tile with all items, even if unit is in reality on other tile.
	Tile *inventoryTile = getSelectedUnit()->getTile();

	randomizeItemLocations(inventoryTile);

	resetUnitTiles();

	// check what unit is still on this tile after reset.
	if (inventoryTile->getUnit())
	{
		// make sure we select the unit closest to the ramp.
		setSelectedUnit(inventoryTile->getUnit());
	}


	// initialize xcom units for battle
	for (auto* bu : *getUnits())
	{
		if (bu->getOriginalFaction() != FACTION_PLAYER || bu->isOut())
		{
			continue;
		}

		bu->prepareNewTurn(false);
	}

	_turn = 1;

	newTurnUpdateScripts();
}

/**
 * Scripts that are run at begining of new turn.
 */
void SavedBattleGame::newTurnUpdateScripts()
{
	if (_isPreview)
	{
		return;
	}

	for (auto* bu : _units)
	{
		if (bu->isIgnored())
		{
			continue;
		}

		ModScript::scriptCallback<ModScript::NewTurnUnit>(bu->getArmor(), bu, this, this->getTurn(), _side);
	}

	for (auto* item : _items)
	{
		if (item->isOwnerIgnored())
		{
			continue;
		}

		ModScript::scriptCallback<ModScript::NewTurnItem>(item->getRules(), item, this, this->getTurn(), _side);
	}

	reviveUnconsciousUnits(false);

	getBattleGame()->convertInfected();
}

/**
 * Tallies the units in the craft deployment preview.
 */
BattlescapeTally SavedBattleGame::tallyUnitsForPreview()
{
	BattlescapeTally tally = { };

	bool custom = _isPreview && _craftForPreview && !_craftTiles.empty();
	Position tmp;

	for (auto* unit : _units)
	{
		if (unit->getOriginalFaction() == FACTION_PLAYER)
		{
			if (unit->isSummonedPlayerUnit())
			{
				continue;
			}
			if (custom)
			{
				bool placementOk = true;
				for (int x = 0; x < unit->getArmor()->getSize(); ++x)
				{
					for (int y = 0; y < unit->getArmor()->getSize(); ++y)
					{
						tmp = Position(x + unit->getPosition().x, y + unit->getPosition().y, unit->getPosition().z);
						bool found = false;
						for (const auto& pos : _craftTiles)
						{
							if (pos == tmp)
							{
								found = true;
								break;
							}
						}
						if (!found)
						{
							placementOk = false;
						}
					}
				}
				if (placementOk)
				{
					tally.inEntrance++;
				}
				else
				{
					tally.inField++;
				}
			}
			else
			{
				if (unit->isInExitArea(START_POINT))
				{
					tally.inEntrance++;
				}
				else
				{
					tally.inField++;
				}
			}
		}
	}

	return tally;
}

/**
 * Saves the custom craft deployment.
 */
void SavedBattleGame::saveCustomCraftDeployment()
{
	_craftForPreview->resetCustomDeployment();
	auto& customSoldierDeployment = _craftForPreview->getCustomSoldierDeployment();
	auto& customVehicleDeployment = _craftForPreview->getCustomVehicleDeployment();

	Position tmp;
	for (const auto* unit : _units)
	{
		if (unit->getOriginalFaction() == FACTION_PLAYER)
		{
			if (unit->isSummonedPlayerUnit())
			{
				continue;
			}
			tmp = Position(unit->getPosition().x - _craftPos.x * 10, unit->getPosition().y - _craftPos.y * 10, unit->getPosition().z - _craftZ);
			if (unit->getGeoscapeSoldier())
			{
				customSoldierDeployment[unit->getGeoscapeSoldier()->getId()] = std::make_pair(tmp, unit->getDirection());
			}
			else
			{
				VehicleDeploymentData v;
				v.type = unit->getType();
				v.pos = tmp;
				v.dir = unit->getDirection();
				v.used = false; // irrelevant now, this will be used only in BattlescapeGenerator::addXCOMUnit()
				customVehicleDeployment.push_back(v);
			}
		}
	}
}

/**
 * Saves the custom RuleCraft deployment. Invalidates corresponding custom craft deployments.
 */
void SavedBattleGame::saveDummyCraftDeployment()
{
	auto* save = getGeoscapeSave();

	// don't forget to invalidate custom deployments of all real craft of this type
	for (auto* xbase : *save->getBases())
	{
		for (auto* xcraft : *xbase->getCrafts())
		{
			if (xcraft->getRules() == _craftForPreview->getRules())
			{
				xcraft->resetCustomDeployment();
			}
		}
	}

	auto& data = save->getCustomRuleCraftDeployments();

	if (isCtrlPressed(true))
	{
		// delete
		data.erase(_craftForPreview->getRules()->getType());
	}
	else
	{
		// save
		RuleCraftDeployment customDeployment;
		Position tmp;
		for (const auto* unit : _units)
		{
			if (unit->getOriginalFaction() == FACTION_PLAYER)
			{
				if (unit->isSummonedPlayerUnit())
				{
					continue;
				}
				tmp = Position(unit->getPosition().x - _craftPos.x * 10, unit->getPosition().y - _craftPos.y * 10, unit->getPosition().z - _craftZ);
				if (unit->getGeoscapeSoldier())
				{
					customDeployment.push_back({ tmp.x, tmp.y, tmp.z, unit->getDirection() });
				}
			}
		}
		data[_craftForPreview->getRules()->getType()] = customDeployment;
	}
}

/**
 * Does the given craft type have a custom deployment?
 */
bool SavedBattleGame::hasCustomDeployment(const RuleCraft* rule) const
{
	// first check the fallback
	if (!rule->getDeployment().empty())
	{
		return true;
	}
	// then the override
	auto* save = getGeoscapeSave();
	auto& data = save->getCustomRuleCraftDeployments();
	if (!data.empty())
	{
		auto find = data.find(rule->getType());
		if (find != data.end())
		{
			return true;
		}
	}
	return false;
}

/**
 * Gets a custom deployment for the given craft type.
 */
const RuleCraftDeployment& SavedBattleGame::getCustomDeployment(const RuleCraft* rule) const
{
	// first try the override
	auto* save = getGeoscapeSave();
	auto& data = save->getCustomRuleCraftDeployments();
	if (!data.empty())
	{
		auto find = data.find(rule->getType());
		if (find != data.end())
		{
			return find->second;
		}
	}
	// this is the fallback
	return rule->getDeployment();
}

/**
 * Ends the current turn and progresses to the next one.
 */
void SavedBattleGame::endTurn()
{
	// reset turret direction for all hostile and neutral units (as it may have been changed during reaction fire)
	for (auto* bu : _units)
	{
		if (bu->getOriginalFaction() != FACTION_PLAYER)
		{
			bu->setDirection(bu->getDirection()); // this is not pointless, the method sets more than just the direction
		}
	}

	if (_side == FACTION_PLAYER)
	{
		if (_selectedUnit && _selectedUnit->getOriginalFaction() == FACTION_PLAYER)
			_lastSelectedUnit = _selectedUnit;
		else
			_lastSelectedUnit = nullptr;
		_selectedUnit =  0;
		_undoUnit = nullptr;
		_side = FACTION_HOSTILE;
	}
	else if (_side == FACTION_HOSTILE)
	{
		_selectedUnit =  0;
		_undoUnit = nullptr;
		_side = FACTION_NEUTRAL;
		// if there is no neutral team, we skip this and instantly prepare the new turn for the player
//		if (selectNextPlayerUnit() == 0)
//		{
//			prepareNewTurn();
//			_turn++;
//			_side = FACTION_PLAYER;
//			if (_lastSelectedUnit && _lastSelectedUnit->isSelectable(FACTION_PLAYER, false, false))
//				_selectedUnit = _lastSelectedUnit;
//			else
//				selectNextPlayerUnit();
//			while (_selectedUnit && _selectedUnit->getFaction() != FACTION_PLAYER)
//				selectNextPlayerUnit();
//		}
	}
	else if (_side == FACTION_NEUTRAL)
	{
		prepareNewTurn();
		_turn++;
		_side = FACTION_PLAYER;
		if (_lastSelectedUnit && _lastSelectedUnit->isSelectable(FACTION_PLAYER, false, false))
			_selectedUnit = _lastSelectedUnit;
		else
			selectNextPlayerUnit();
		while (_selectedUnit && _selectedUnit->getFaction() != FACTION_PLAYER)
			selectNextPlayerUnit();

		_undoUnit = nullptr;
		_lastSelectedUnit = nullptr;
	}

	auto tally = _battleState->getBattleGame()->tallyUnits();

	if ((_turn > _cheatTurn / 2 && tally.liveAliens <= 2) || _turn > _cheatTurn)
	{
		_cheating = true;
	}

	if (_side == FACTION_PLAYER)
	{
		// update the "number of turns since last spotted" and the number of turns left sniper AI knows about player units
		for (auto* bu : _units)
		{
			if (bu->isIgnored())
			{
				continue;
			}

			bu->updateTurnsSince();
			if (_cheating && bu->getFaction() == FACTION_PLAYER && !bu->isOut())
			{
				bu->setTurnsSinceSpotted(0);
			}
			if (bu->getAIModule())
			{
				bu->getAIModule()->reset(); // clean up AI state
			}
		}
	}

	// I want this to happen on either player's half-turn, so the aliens don't remember locations of people that were briefly visible to them on the player's turn
	for (std::vector<BattleUnit *>::iterator i = _units.begin(); i != _units.end(); ++i)
	{
		if ((*i)->getTurnsSinceSeen(_side) < 255)
		{
			(*i)->setTurnsSinceSeen((*i)->getTurnsSinceSeen(_side) + 1, _side);
		}
	}

	// hide all aliens (VOF calculations below will turn them visible again)
	for (auto* bu : _units)
	{
		if (bu->isIgnored())
		{
			continue;
		}

		// Disclaimer: hardcoded!
		if (bu->getFaction() == FACTION_NEUTRAL && bu->getOriginalFaction() == FACTION_HOSTILE)
		{
			if (_side != FACTION_PLAYER)
			{
				// a hostile converted to a neutral during the player turn, just keep them mostly "as is" during the hostile/neutral turn...
				if (_side == FACTION_HOSTILE)
				{
					bu->updateUnitStats(false, true);
				}
				bu->setVisible(false);
			}
			else
			{
				// ...and convert them back at the beginning of the player turn
				bu->prepareNewTurn();

				if (bu->getAIModule())
				{
					// don't forget to rewire them back to attack the player again
					bu->getAIModule()->setTargetFaction(FACTION_PLAYER);
				}
			}
			// skip the standard logic
			continue;
		}

		if (bu->getFaction() == _side)
		{
			bu->prepareNewTurn();
		}
		else if (bu->getOriginalFaction() == _side)
		{
			bu->updateUnitStats(false, true);
		}
		if (bu->getFaction() != FACTION_PLAYER)
		{
			bu->setVisible(false);
		}
	}

	//danger state must be cleared after each player due to autoplay also setting it
	for (int i = 0; i < _mapsize_x * _mapsize_y * _mapsize_z; ++i)
	{
		getTile(i)->setDangerous(false);
	}

	//scripts update
	newTurnUpdateScripts();

	//fov check will be done by `BattlescapeGame::endTurn`

	if (_side != FACTION_PLAYER)
		selectNextPlayerUnit();
}

/**
 * Get current animation frame number.
 * @return Frame number.
 */
int SavedBattleGame::getAnimFrame() const
{
	return _animFrame;
}

/**
 * Increase animation frame with wrap around 705600.
 */
void SavedBattleGame::nextAnimFrame()
{
	_animFrame = (_animFrame + 1) % (64 * 3*3 * 5*5 * 7*7);
}

/**
 * Turns on debug mode.
 */
void SavedBattleGame::setDebugMode()
{
	revealMap();

	_debugMode = true;
}

void SavedBattleGame::revealMap()
{
	for (int i = 0; i < _mapsize_z * _mapsize_y * _mapsize_x; ++i)
	{
		_tiles[i].setDiscovered(true, O_FLOOR);
	}
}

/**
 * Gets the current debug mode.
 * @return Debug mode.
 */
bool SavedBattleGame::getDebugMode() const
{
	return _debugMode;
}

/**
* Sets the bug hunt mode.
*/
void SavedBattleGame::setBughuntMode(bool bughuntMode)
{
	_bughuntMode = bughuntMode;
}

/**
* Gets the current bug hunt mode.
* @return Bug hunt mode.
*/
bool SavedBattleGame::getBughuntMode() const
{
	return _bughuntMode;
}

/**
 * Gets the BattlescapeState.
 * @return Pointer to the BattlescapeState.
 */
BattlescapeState *SavedBattleGame::getBattleState()
{
	return _battleState;
}

/**
 * Gets the BattlescapeState.
 * @return Pointer to the BattlescapeState.
 */
const BattlescapeState *SavedBattleGame::getBattleState() const
{
	return _battleState;
}

/**
 * Gets the BattlescapeState.
 * @return Pointer to the BattlescapeState.
 */
BattlescapeGame *SavedBattleGame::getBattleGame()
{
	return _battleState->getBattleGame();
}

/**
 * Gets the BattlescapeState.
 * @return Pointer to the BattlescapeState.
 */
const BattlescapeGame *SavedBattleGame::getBattleGame() const
{
	return _battleState->getBattleGame();
}

/**
 * Is BattlescapeState busy?
 * @return True, if busy.
 */
bool SavedBattleGame::isBattlescapeStateBusy() const
{
	if (_battleState)
	{
		return _battleState->isBusy();
	}
	return false;
}

/**
 * Sets the BattlescapeState.
 * @param bs A Pointer to a BattlescapeState.
 */
void SavedBattleGame::setBattleState(BattlescapeState *bs)
{
	_battleState = bs;
}

/**
 * Is CTRL pressed?
 */
bool SavedBattleGame::isCtrlPressed(bool considerTouchButtons) const
{
	if (_battleState)
	{
		return _battleState->getGame()->isCtrlPressed(considerTouchButtons);
	}
	return false;
}

/**
 * Is ALT pressed?
 */
bool SavedBattleGame::isAltPressed(bool considerTouchButtons) const
{
	if (_battleState)
	{
		return _battleState->getGame()->isAltPressed(considerTouchButtons);
	}
	return false;
}

/**
 * Is SHIFT pressed?
 */
bool SavedBattleGame::isShiftPressed(bool considerTouchButtons) const
{
	if (_battleState)
	{
		return _battleState->getGame()->isShiftPressed(considerTouchButtons);
	}
	return false;
}

/**
 * Resets all the units to their current standing tile(s).
 */
void SavedBattleGame::resetUnitTiles()
{
	for (auto* bu : _units)
	{
		if (!bu->isOut())
		{
			bu->setTile(getTile(bu->getPosition()), this);
		}
		if (bu->getFaction() == FACTION_PLAYER)
		{
			bu->setVisible(true);
		}
	}
	_beforeGame = false;
}

/**
 * Gives access to the "storage space" vector, for distribution of items in base defense missions.
 * @return Vector of storage positions.
 */
std::vector<Position> &SavedBattleGame::getStorageSpace()
{
	return _storageSpace;
}

/**
 * Move all the leftover items in base defense missions to random locations in the storage facilities
 * @param t the tile where all our goodies are initially stored.
 */
void SavedBattleGame::randomizeItemLocations(Tile *t)
{
	// remove position of Tile t from the vector (because of potential endless loop)
	Collections::removeIf(_storageSpace, [&](Position& p) { return p == t->getPosition(); });

	if (!_storageSpace.empty())
	{
		for (auto iter = t->getInventory()->begin(); iter != t->getInventory()->end();)
		{
			BattleItem* bi = (*iter);
			if (bi->getSlot()->getType() == INV_GROUND)
			{
				getTile(_storageSpace.at(RNG::generate(0, _storageSpace.size() -1)))->addItem(bi, bi->getSlot());
				iter = t->getInventory()->erase(iter);
			}
			else
			{
				++iter;
			}
		}
	}
}

/**
 * Add item to delete list, usually when removing item from game or build in weapons
 * @param item Item to delete after game end.
 */
void SavedBattleGame::deleteList(BattleItem* item)
{
	_deleted.push_back(item);
}

/**
 * Removes an item from the game. Eg. when ammo item is depleted.
 * @param item The Item to remove.
 */
void SavedBattleGame::removeItem(BattleItem *item)
{
	auto purge = [](std::vector<BattleItem*> &inventory, BattleItem* forDelete)
	{
		auto begin = inventory.begin();
		auto end = inventory.end();
		for (auto i = begin; i != end; ++i)
		{
			if (*i == forDelete)
			{
				inventory.erase(i);
				return true;
			}
		}
		return false;
	};

	if (item->isSpecialWeapon())
	{
		// we cannot remove it because load() would create a new one
		// only when a unit is killed or set to "timeout", we can remove its items.
		return;
	}

	if (!purge(_items, item))
	{
		return;
	}

	// due to strange design, the item has to be removed from the tile it is on too (if it is on a tile)
	item->moveToOwner(nullptr);

	deleteList(item);

	for (int slot = 0; slot < RuleItem::AmmoSlotMax; ++slot)
	{
		auto ammo = item->getAmmoForSlot(slot);
		if (ammo && ammo != item)
		{
			if (purge(_items, ammo))
			{
				deleteList(ammo);
			}
		}
	}
}

/**
 * Add built-in items from list to unit.
 * @param unit Unit that should get weapon.
 * @param fixed List of built-in items.
 */
void SavedBattleGame::addFixedItems(BattleUnit *unit, const std::vector<const RuleItem*> &fixed)
{
	if (!fixed.empty())
	{
		std::vector<const RuleItem*> ammo;
		for (const auto* ruleItem : fixed)
		{
			if (ruleItem)
			{
				if (ruleItem->getBattleType() == BT_AMMO)
				{
					ammo.push_back(ruleItem);
					continue;
				}
				createItemForUnit(ruleItem, unit, true);
			}
		}
		for (const auto* ruleItem : ammo)
		{
			createItemForUnit(ruleItem, unit, true);
		}
	}
}

/**
 * Create all fixed items for new created unit.
 * @param unit Unit to equip.
 */
void SavedBattleGame::initUnit(BattleUnit *unit, size_t itemLevel)
{
	if (_isPreview)
	{
		return;
	}

	unit->setSpecialWeapon(this, false);
	Unit* rule = unit->getUnitRules();
	const Armor* armor = unit->getArmor();
	// Built in weapons: the unit has this weapon regardless of loadout or what have you.
	addFixedItems(unit, armor->getBuiltInWeapons());

	// For aliens and HWP
	if (rule)
	{
		auto &buildin = rule->getBuiltInWeapons();
		if (!buildin.empty())
		{
			int idx = itemLevel >= buildin.size() ? buildin.size() - 1 : itemLevel;
			// Built in weapons: the unit has this weapon regardless of loadout or what have you.
			addFixedItems(unit, buildin.at(idx));
		}

		auto& buildin2 = rule->getWeightedBuiltInWeapons();
		if (!buildin2.empty())
		{
			int idx2 = itemLevel >= buildin2.size() ? buildin2.size() - 1 : itemLevel;
			auto* weights = buildin2.at(idx2);
			auto* weaponSetRule = _rule->getWeaponSet(weights->choose());
			addFixedItems(unit, weaponSetRule->getWeapons());
		}

		// terrorist alien's equipment is a special case - they are fitted with a weapon which is the alien's name with suffix _WEAPON
		if (rule->isLivingWeapon())
		{
			std::string terroristWeapon = rule->getRace().substr(4);
			terroristWeapon += "_WEAPON";
			RuleItem *ruleItem = _rule->getItem(terroristWeapon);
			if (ruleItem)
			{
				BattleItem *item = createItemForUnit(ruleItem, unit);
				if (item)
				{
					unit->setTurretType(item->getRules()->getTurretType());
				}
			}
		}
	}

	ModScript::scriptCallback<ModScript::CreateUnit>(armor, unit, this, this->getTurn());

	if (auto solder = unit->getGeoscapeSoldier())
	{
		for (const auto* bonus : *solder->getBonuses(nullptr))
		{
			ModScript::scriptCallback<ModScript::ApplySoldierBonuses>(bonus, unit, this, bonus);
		}
	}
}

/**
 * Init new created item.
 * @param item
 */
void SavedBattleGame::initItem(BattleItem *item, BattleUnit *unit)
{
	if (_isPreview)
	{
		return;
	}

	ModScript::scriptCallback<ModScript::CreateItem>(item->getRules(), item, unit, this, this->getTurn());
}

/**
 * Create new item for unit.
 */
BattleItem *SavedBattleGame::createItemForUnit(const RuleItem *rule, BattleUnit *unit, bool fixedWeapon)
{
	if (_isPreview)
	{
		return nullptr;
	}

	BattleItem *item = new BattleItem(rule, getCurrentItemId());
	if (!unit->addItem(item, _rule, false, fixedWeapon, fixedWeapon))
	{
		delete item;
		item = nullptr;
	}
	else
	{
		_items.push_back(item);
		initItem(item, unit);
	}
	return item;
}

/**
 * Create new special built-in item for unit.
 */
BattleItem *SavedBattleGame::createItemForUnitSpecialBuiltin(const RuleItem *rule, BattleUnit *unit)
{
	if (_isPreview)
	{
		return nullptr;
	}

	BattleItem *item = new BattleItem(rule, getCurrentItemId());
	item->setOwner(unit);
	item->setSlot(nullptr);
	_items.push_back(item);
	initItem(item, unit);
	return item;
}
/**
 * Create new item for tile.
 */
BattleItem *SavedBattleGame::createItemForTile(const std::string& type, Tile *tile)
{
	return createItemForTile(_rule->getItem(type, true), tile);
}

/**
 * Create new item for tile;
 */
BattleItem *SavedBattleGame::createItemForTile(const RuleItem *rule, Tile *tile, BattleUnit *corpseFor)
{
	// Note: this is allowed also in preview mode; for items spawned from map blocks (and friendly units spawned from such items)

	BattleItem *item = new BattleItem(rule, getCurrentItemId());
	if (tile)
	{
		RuleInventory *ground = _rule->getInventoryGround();
		tile->addItem(item, ground);
	}
	item->setUnit(corpseFor);
	_items.push_back(item);
	initItem(item);
	return item;
}

/**
 * Create new temporary item. Item is not added to unit list and is not initialized fully.
 * @param rule Item config
 * @return New temporary item.
 */
BattleItem *SavedBattleGame::createTempItem(const RuleItem *rule)
{
	return new BattleItem(rule, getCurrentItemId());
}

/**
 * Create new temporary unit. Unit is not added to unit list and is not initialized fully.
 * @param rules Unit config
 * @param faction Faction of unit
 * @param nextUnitId Optional next id of unit, if not given then it try get value based on last unit.
 * @return New temporary unit.
 */
BattleUnit *SavedBattleGame::createTempUnit(const Unit *rules, UnitFaction faction, int nextUnitId)
{
	BattleUnit *newUnit = new BattleUnit(
		getMod(),
		const_cast<Unit*>(rules),
		faction,
		nextUnitId > 0 ? nextUnitId : getUnits()->back()->getId() + 1,
		getEnviroEffects(),
		rules->getArmor(),
		faction == FACTION_HOSTILE ? _rule->getStatAdjustment(getGeoscapeSave()->getDifficulty()) : nullptr,
		getDepth(),
		getStartingCondition());

	if (faction == FACTION_PLAYER)
	{
		//Tanks are created with predefined `nextUnitId` value, if we do not have this value, its summoned unit.
		if (nextUnitId <= 0)
		{
			newUnit->setSummonedPlayerUnit(true);
		}
	}
	else
	{
		newUnit->setAIModule(new AIModule(this, newUnit, 0));
	}

	return newUnit;
}

/**
 * Converts a unit into a unit of another type.
 * @param unit The unit to convert.
 * @return Pointer to the new unit.
 */
BattleUnit *SavedBattleGame::convertUnit(BattleUnit *unit)
{
	// only ever respawn once
	unit->setAlreadyRespawned(true);

	bool visible = unit->getVisible();

	clearUnitSelection(unit);

	// in case the unit was unconscious
	removeUnconsciousBodyItem(unit);

	unit->instaKill();

	auto tile = unit->getTile();
	if (tile == nullptr)
	{
		auto pos = unit->getPosition();
		if (pos != TileEngine::invalid)
		{
			tile = getTile(pos);
		}
	}

	// in case of unconscious unit someone could stand on top of it, or take current unit to inventory, then we skip spawning anything
	if (!tile || (tile->getUnit() != nullptr && tile->getUnit() != unit))
	{
		return nullptr;
	}

	getTileEngine()->itemDropInventory(tile, unit, false, true);

	// remove unit-tile link
	unit->setTile(nullptr, this);

	const Unit* type = unit->getSpawnUnit();

	BattleUnit *newUnit = createTempUnit(type, unit->getSpawnUnitFaction());
	newUnit->setSpawnUnitFaction(unit->getSpawnUnitFaction()); // allied/neutral zombie should spawn an allied/neutral chryssalid?

	newUnit->clearTimeUnits();
	newUnit->setVisible(visible);
	newUnit->setTile(tile, this);
	newUnit->setPosition(unit->getPosition());
	newUnit->setDirection(unit->getDirection());
	getUnits()->push_back(newUnit);
	initUnit(newUnit);

	getTileEngine()->calculateFOV(newUnit->getPosition());  //happens fairly rarely, so do a full recalc for units in range to handle the potential unit visible cache issues.
	getTileEngine()->applyGravity(newUnit->getTile());
	newUnit->dontReselect();
	return newUnit;
}



/**
 * Returns whether the battlescape should display the names of the soldiers or their callsigns.
 * @return True, if the battlescape should show player names + statstrings (default behaviour), or false, if the battlescape should display callsigns.
 */
bool SavedBattleGame::isNameDisplay() const
{
	return _nameDisplay;
}

/**
 * Sets whether the player names (true) or their callsigns (false) are displayed
 * @param displayName True, if the battlescape should show player names + statstrings (default behaviour), or false, if the battlescape should display callsigns.
 */
void SavedBattleGame::setNameDisplay(bool displayName)
{
	_nameDisplay = displayName;
}

/**
 * Sets whether the mission was aborted or successful.
 * @param flag True, if the mission was aborted, or false, if the mission was successful.
 */
void SavedBattleGame::setAborted(bool flag)
{
	_aborted = flag;
}

/**
 * Returns whether the mission was aborted or successful.
 * @return True, if the mission was aborted, or false, if the mission was successful.
 */
bool SavedBattleGame::isAborted() const
{
	return _aborted;
}

/**
 * increments the number of objectives to be destroyed.
 */
void SavedBattleGame::setObjectiveCount(int counter)
{
	_objectivesNeeded = counter;
	_objectivesDestroyed = 0;
}

/**
 * Sets whether the objective is destroyed.
 */
void SavedBattleGame::addDestroyedObjective()
{
	if (!allObjectivesDestroyed())
	{
		_objectivesDestroyed++;
		if (allObjectivesDestroyed())
		{
			if (getObjectiveType() == MUST_DESTROY)
			{
				_battleState->getBattleGame()->autoEndBattle();
			}
			else
			{
				_battleState->getBattleGame()->missionComplete();
			}
		}
	}
}

/**
 * Returns whether the objectives are destroyed.
 * @return True if the objectives are destroyed.
 */
bool SavedBattleGame::allObjectivesDestroyed() const
{
	return (_objectivesNeeded > 0 && _objectivesDestroyed == _objectivesNeeded);
}

/**
 * Gets the current item ID.
 * @return Current item ID pointer.
 */
int *SavedBattleGame::getCurrentItemId()
{
	return &_itemId;
}

/**
 * Finds a fitting node where a unit can spawn.
 * @param nodeRank Rank of the node (this is not the rank of the alien!).
 * @param unit Pointer to the unit (to get its position).
 * @return Pointer to the chosen node.
 */
Node *SavedBattleGame::getSpawnNode(int nodeRank, BattleUnit *unit)
{
	int highestPriority = -1;
	std::vector<Node*> compliantNodes;

	for (auto* node : *getNodes())
	{
		if (node->isDummy())
		{
			continue;
		}
		if (node->getRank() == nodeRank								// ranks must match
			&& (!(node->getType() & Node::TYPE_SMALL)
				|| unit->isSmallUnit())								// the small unit bit is not set or the unit is small
			&& (!(node->getType() & Node::TYPE_FLYING)
				|| unit->getMovementType() == MT_FLY)				// the flying unit bit is not set or the unit can fly
			&& node->getPriority() > 0								// priority 0 is no spawn place
			&& setUnitPosition(unit, node->getPosition(), true))	// check if not already occupied
		{
			if (node->getPriority() > highestPriority)
			{
				highestPriority = node->getPriority();
				compliantNodes.clear(); // drop the last nodes, as we found a higher priority now
			}
			if (node->getPriority() == highestPriority)
			{
				compliantNodes.push_back(node);
			}
		}
	}

	if (compliantNodes.empty()) return 0;

	int n = RNG::generate(0, compliantNodes.size() - 1);

	return compliantNodes[n];
}

/**
 * Finds a fitting node where a unit can patrol to.
 * @param scout Is the unit scouting?
 * @param unit Pointer to the unit (to get its position).
 * @param fromNode Pointer to the node the unit is at.
 * @return Pointer to the chosen node.
 */
Node *SavedBattleGame::getPatrolNode(bool scout, BattleUnit *unit, Node *fromNode)
{
	std::vector<Node *> compliantNodes;
	Node *preferred = 0;

	if (fromNode == 0)
	{
		if (Options::traceAI) { Log(LOG_INFO) << "This alien got lost. :("; }
		fromNode = getNodes()->at(RNG::generate(0, getNodes()->size() - 1));
		while (fromNode->isDummy())
		{
			fromNode = getNodes()->at(RNG::generate(0, getNodes()->size() - 1));
		}
	}

	// scouts roam all over while all others shuffle around to adjacent nodes at most:
	const int end = scout ? getNodes()->size() : fromNode->getNodeLinks()->size();

	for (int i = 0; i < end; ++i)
	{
		if (!scout && fromNode->getNodeLinks()->at(i) < 1) continue;

		Node *n = getNodes()->at(scout ? i : fromNode->getNodeLinks()->at(i));
		if ( !n->isDummy()																				// don't consider dummy nodes.
			&& (n->getFlags() > 0 || n->getRank() > 0 || scout)											// for non-scouts we find a node with a desirability above 0
			&& (!(n->getType() & Node::TYPE_SMALL) || unit->isSmallUnit())								// the small unit bit is not set or the unit is small
			&& (!(n->getType() & Node::TYPE_FLYING) || unit->getMovementType() == MT_FLY)	// the flying unit bit is not set or the unit can fly
			&& !n->isAllocated()																		// check if not allocated
			&& !(n->getType() & Node::TYPE_DANGEROUS)													// don't go there if an alien got shot there; stupid behavior like that
			&& setUnitPosition(unit, n->getPosition(), true)											// check if not already occupied
			&& getTile(n->getPosition()) && !getTile(n->getPosition())->getFire()						// you are not a firefighter; do not patrol into fire
			&& (unit->getFaction() != FACTION_HOSTILE || !getTile(n->getPosition())->getDangerous())	// aliens don't run into a grenade blast
			&& (!scout || n != fromNode)																// scouts push forward
			&& n->getPosition().x > 0 && n->getPosition().y > 0)
		{
			if (!preferred
				|| (unit->getRankInt() >=0 &&
					preferred->getRank() == Node::nodeRank[unit->getRankInt()][0] &&
					preferred->getFlags() < n->getFlags())
				|| preferred->getFlags() < n->getFlags())
			{
				preferred = n;
			}
			compliantNodes.push_back(n);
		}
	}

	if (compliantNodes.empty())
	{
		if (Options::traceAI) { Log(LOG_INFO) << (scout ? "Scout " : "Guard") << " found on patrol node! XXX XXX XXX"; }
		if (unit->isBigUnit() && !scout)
		{
			return getPatrolNode(true, unit, fromNode); // move dammit
		}
		else
			return 0;
	}

	if (scout)
	{
		// scout picks a random destination:
		return compliantNodes[RNG::generate(0, compliantNodes.size() - 1)];
	}
	else
	{
		if (!preferred) return 0;

		// non-scout patrols to highest value unoccupied node that's not fromNode
		if (Options::traceAI) { Log(LOG_INFO) << "Choosing node flagged " << preferred->getFlags(); }
		return preferred;
	}
}

/**
 * Carries out new turn preparations such as fire and smoke spreading.
 */
void SavedBattleGame::prepareNewTurn()
{
	std::vector<Tile*> tilesOnFire;
	std::vector<Tile*> tilesOnSmoke;

	// prepare a list of tiles on fire
	for (int i = 0; i < _mapsize_x * _mapsize_y * _mapsize_z; ++i)
	{
		if (getTile(i)->getFire() > 0)
		{
			tilesOnFire.push_back(getTile(i));
		}
	}

	// first: fires spread
	for (auto* tileOnFire : tilesOnFire)
	{
		// if we haven't added fire here this turn
		if (tileOnFire->getOverlaps() == 0)
		{
			// reduce the fire timer
			tileOnFire->setFire(tileOnFire->getFire() -1);

			// if we're still burning
			if (tileOnFire->getFire())
			{
				// propagate in four cardinal directions (0, 2, 4, 6)
				for (int dir = 0; dir <= 6; dir += 2)
				{
					Position pos;
					Pathfinding::directionToVector(dir, &pos);
					Tile *t = getTile(tileOnFire->getPosition() + pos);
					// if there's no wall blocking the path of the flames...
					if (t && getTileEngine()->horizontalBlockage(tileOnFire, t, DT_IN) == 0)
					{
						// attempt to set this tile on fire
						t->ignite(tileOnFire->getSmoke());
					}
				}
			}
			// fire has burnt out
			else
			{
				tileOnFire->setSmoke(0);
				// burn this tile, and any object in it, if it's not fireproof/indestructible.
				if (tileOnFire->getMapData(O_OBJECT))
				{
					if (tileOnFire->getMapData(O_OBJECT)->getFlammable() != 255 && tileOnFire->getMapData(O_OBJECT)->getArmor() != 255)
					{
						if (tileOnFire->destroy(O_OBJECT, getObjectiveType()))
						{
							addDestroyedObjective();
						}
						if (tileOnFire->destroy(O_FLOOR, getObjectiveType()))
						{
							addDestroyedObjective();
						}
					}
				}
				else if (tileOnFire->getMapData(O_FLOOR))
				{
					if (tileOnFire->getMapData(O_FLOOR)->getFlammable() != 255 && tileOnFire->getMapData(O_FLOOR)->getArmor() != 255)
					{
						if (tileOnFire->destroy(O_FLOOR, getObjectiveType()))
						{
							addDestroyedObjective();
						}
					}
				}
				getTileEngine()->applyGravity(tileOnFire);
			}
		}
	}

	// prepare a list of tiles on fire/with smoke in them (smoke acts as fire intensity)
	for (int i = 0; i < _mapsize_x * _mapsize_y * _mapsize_z; ++i)
	{
		if (getTile(i)->getSmoke() > 0)
		{
			tilesOnSmoke.push_back(getTile(i));
		}
		getTile(i)->setDangerous(false);
	}

	// now make the smoke spread.
	for (auto* tileOnSmoke : tilesOnSmoke)
	{
		// smoke and fire follow slightly different rules.
		if (tileOnSmoke->getFire() == 0)
		{
			// reduce the smoke counter
			tileOnSmoke->setSmoke(tileOnSmoke->getSmoke() - 1);
			// if we're still smoking
			if (tileOnSmoke->getSmoke())
			{
				// spread in four cardinal directions
				for (int dir = 0; dir <= 6; dir += 2)
				{
					Position pos;
					Pathfinding::directionToVector(dir, &pos);
					Tile *t = getTile(tileOnSmoke->getPosition() + pos);
					// as long as there are no walls blocking us
					if (t && getTileEngine()->horizontalBlockage(tileOnSmoke, t, DT_SMOKE) == 0)
					{
						// only add smoke to empty tiles, or tiles with no fire, and smoke that was added this turn
						if (t->getSmoke() == 0 || (t->getFire() == 0 && t->getOverlaps() != 0))
						{
							t->addSmoke(tileOnSmoke->getSmoke());
						}
					}
				}
			}
		}
		else
		{
			// smoke from fire spreads upwards one level if there's no floor blocking it.
			Position pos = Position(0,0,1);
			Tile *t = getTile(tileOnSmoke->getPosition() + pos);
			if (t && t->hasNoFloor(this))
			{
				// only add smoke equal to half the intensity of the fire
				t->addSmoke(tileOnSmoke->getSmoke()/2);
			}
			// then it spreads in the four cardinal directions.
			for (int dir = 0; dir <= 6; dir += 2)
			{
				Pathfinding::directionToVector(dir, &pos);
				t = getTile(tileOnSmoke->getPosition() + pos);
				if (t && getTileEngine()->horizontalBlockage(tileOnSmoke, t, DT_SMOKE) == 0)
				{
					t->addSmoke(tileOnSmoke->getSmoke()/2);
				}
			}
		}
	}

	if (!tilesOnFire.empty() || !tilesOnSmoke.empty())
	{
		// do damage to units, average out the smoke, etc.
		for (int i = 0; i < _mapsize_x * _mapsize_y * _mapsize_z; ++i)
		{
			if (getTile(i)->getSmoke() != 0)
				getTile(i)->prepareNewTurn(getDepth() == 0);
		}
	}

	Mod *mod = getBattleState()->getGame()->getMod();
	for (auto* bu : *getUnits())
	{
		bu->calculateEnviDamage(mod, this);
	}

	//fov and light udadates are done in `BattlescapeGame::endTurn`
}

/**
 * Checks for units that are unconscious and revives them if they shouldn't be.
 *
 * Revived units need a tile to stand on. If the unit's current position is occupied, then
 * all directions around the tile are searched for a free tile to place the unit in.
 * If no free tile is found the unit stays unconscious.
 */
void SavedBattleGame::reviveUnconsciousUnits(bool noTU)
{
	for (auto* bu : *getUnits())
	{
		if (bu->isSmallUnit() && !bu->isIgnored())
		{
			Position originalPosition = bu->getPosition();
			if (originalPosition == Position(-1, -1, -1))
			{
				for (auto* bi : _items)
				{
					if (bi->getUnit() && bi->getUnit() == bu && bi->getOwner())
					{
						originalPosition = bi->getOwner()->getPosition();
					}
				}
			}
			if (bu->getStatus() == STATUS_UNCONSCIOUS && !bu->isOutThresholdExceed())
			{
				Tile *targetTile = getTile(originalPosition);
				bool largeUnit = targetTile && targetTile->getUnit() && targetTile->getUnit() != bu && targetTile->getUnit()->isBigUnit();
				if (placeUnitNearPosition(bu, originalPosition, largeUnit))
				{
					// recover from unconscious
					bu->setNotificationShown(0);
					bu->turn(false); // makes the unit stand up again
					bu->kneel(false);
					bu->setAlreadyExploded(false);
					if (noTU)
					{
						bu->clearTimeUnits();
					}
					else
					{
						bu->updateUnitStats(true, false);
						if (getMod()->getTURecoveryWakeUpNewTurn() < 100)
						{
							int newTU = bu->getTimeUnits() * getMod()->getTURecoveryWakeUpNewTurn() / 100;
							bu->setTimeUnits(newTU);
						}
					}
					removeUnconsciousBodyItem(bu);
				}
			}
		}
	}
}

/**
 * Removes the body item that corresponds to the unit.
 */
void SavedBattleGame::removeUnconsciousBodyItem(BattleUnit *bu)
{
	int size = bu->getArmor()->getSize();
	size *= size;
	// remove the unconscious body item corresponding to this unit
	for (auto iter = getItems()->begin(); iter != getItems()->end(); )
	{
		if ((*iter)->getUnit() == bu)
		{
			removeItem((*iter)); //TODO: if used on anything other that corpse it will crash as it could remove MORE than one item from `getItems()` and them go pass `!= end()`
			if (--size == 0) break;
		}
		else
		{
			++iter;
		}
	}
}

/**
 * Places units on the map. Handles large units that are placed on multiple tiles.
 * @param bu The unit to be placed.
 * @param position The position to place the unit.
 * @param testOnly If true then just checks if the unit can be placed at the position.
 * @return True if the unit could be successfully placed.
 */
bool SavedBattleGame::setUnitPosition(BattleUnit *bu, Position position, bool testOnly)
{
	int size = bu->getArmor()->getSize() - 1;
	Position zOffset (0,0,0);
	// first check if the tiles are occupied
	for (int x = size; x >= 0; x--)
	{
		for (int y = size; y >= 0; y--)
		{
			Tile *t = getTile(position + Position(x,y,0) + zOffset);
			if (t == 0 ||
				(t->getUnit() != 0 && t->getUnit() != bu) ||
				t->getTUCost(O_OBJECT, bu->getMovementType()) == Pathfinding::INVALID_MOVE_COST ||
				(t->hasNoFloor(this) && bu->getMovementType() != MT_FLY) ||
				(t->getMapData(O_OBJECT) && t->getMapData(O_OBJECT)->getBigWall() && t->getMapData(O_OBJECT)->getBigWall() <= 3))
			{
				return false;
			}
			// move the unit up to the next level (desert and seabed terrains)
			if (t && t->getTerrainLevel() == -24)
			{
				zOffset.z += 1;
				x = size;
				y = size + 1;
			}
		}
	}

	if (size > 0)
	{
		getPathfinding()->setUnit(bu); //TODO: remove as was required by `isBlockedDirection`
		for (int dir = 2; dir <= 4; ++dir)
		{
			if (getPathfinding()->isBlockedDirection(bu, getTile(position + zOffset), dir))
				return false;
		}
	}

	if (testOnly) return true;

	bu->setTile(getTile(position + zOffset), this);
	bu->setPosition(position + zOffset);

	return true;
}

/**
 * @brief Checks whether anyone on a particular faction is looking at the unit.
 *
 * Similar to getSpottingUnits() but returns a bool and stops searching if one positive hit is found.
 *
 * @param faction Faction to check through.
 * @param unit Whom to spot.
 * @return True when the unit can be seen
 */
bool SavedBattleGame::eyesOnTarget(UnitFaction faction, BattleUnit* unit)
{
	for (auto* bu : *getUnits())
	{
		if (bu->getFaction() != faction) continue;

		auto* vis = bu->getVisibleUnits();
		if (std::find(vis->begin(), vis->end(), unit) != vis->end()) return true;
		// aliens know the location of all XCom agents sighted by all other aliens due to sharing locations over their space-walkie-talkies
	}

	return false;
}

/**
 * Adds this unit to the vector of falling units,
 * if it doesn't already exist.
 * @param unit The unit.
 * @return Was the unit added?
 */
bool SavedBattleGame::addFallingUnit(BattleUnit* unit)
{
	bool add = true;
	for (auto* bu : _fallingUnits)
	{
		if (unit == bu)
		{
			add = false;
			break;
		}
	}
	if (add)
	{
		_fallingUnits.push_front(unit);
		_unitsFalling = true;
	}
	return add;
}

/**
 * Gets all units in the battlescape that are falling.
 * @return The falling units in the battlescape.
 */
std::list<BattleUnit*> *SavedBattleGame::getFallingUnits()
{
	return &_fallingUnits;
}

/**
 * Toggles the switch that says "there are units falling, start the fall state".
 * @param fall True if there are any units falling in the battlescape.
 */
void SavedBattleGame::setUnitsFalling(bool fall)
{
	_unitsFalling = fall;
}

/**
 * Returns whether there are any units falling in the battlescape.
 * @return True if there are any units falling in the battlescape.
 */
bool SavedBattleGame::getUnitsFalling() const
{
	return _unitsFalling;
}

/**
 * Gets the highest ranked, living XCom unit.
 * @return The highest ranked, living XCom unit.
 */
BattleUnit* SavedBattleGame::getHighestRankedXCom()
{
	BattleUnit* highest = 0;
	for (auto* bu : _units)
	{
		if (bu->getOriginalFaction() == FACTION_PLAYER && !bu->isOut())
		{
			if (highest == 0 || bu->getRankInt() > highest->getRankInt())
			{
				highest = bu;
			}
		}
	}
	return highest;
}

/**
 * Gets morale modifier of unit.
 * @param unit
 * @return Morale modifier.
 */
int SavedBattleGame::getUnitMoraleModifier(BattleUnit* unit)
{
	int result = 100;

	if (unit->getOriginalFaction() == FACTION_PLAYER)
	{
		switch (unit->getRankInt())
		{
		case 5:
			result += 25;
			FALLTHROUGH;
		case 4:
			result += 20;
			FALLTHROUGH;
		case 3:
			result += 10;
			FALLTHROUGH;
		case 2:
			result += 20;
			FALLTHROUGH;
		default:
			break;
		}
	}

	return result;
}

/**
 * Gets the morale loss modifier (by unit type) of the killed unit.
 * @param unit
 * @return Morale modifier.
 */
int SavedBattleGame::getMoraleLossModifierWhenKilled(BattleUnit* unit)
{
	int result = 100;

	if (!unit)
		return result;

	if (unit->getGeoscapeSoldier())
	{
		result = unit->getGeoscapeSoldier()->getRules()->getMoraleLossWhenKilled();
	}
	else if (unit->getUnitRules())
	{
		result = unit->getUnitRules()->getMoraleLossWhenKilled();
	}

	// it's a morale loss, there's no way I am allowing modders to make it a morale gain!
	return std::max(0, result);
}

/**
 * Gets the morale modifier for XCom based on the highest ranked, living XCom unit,
 * or Alien modifier based on they number.
 * @param hostile modifier for player or hostile?
 * @return The morale modifier.
 */
int SavedBattleGame::getFactionMoraleModifier(bool player)
{
	if (player)
	{
		BattleUnit *leader = getHighestRankedXCom();
		int result = 100;
		if (leader)
		{
			switch (leader->getRankInt())
			{
			case 5:
				result += 25;
				FALLTHROUGH;
			case 4:
				result += 10;
				FALLTHROUGH;
			case 3:
				result += 5;
				FALLTHROUGH;
			case 2:
				result += 10;
				FALLTHROUGH;
			default:
				break;
			}
		}
		return result;
	}
	else
	{
		int number = 0;
		for (auto* bu : _units)
		{
			if (bu->getOriginalFaction() == FACTION_HOSTILE && !bu->isOut())
			{
				++number;
			}
		}
		return std::max(6 * number, 100);
	}
}

/**
 * Places a unit on or near a position.
 * @param unit The unit to place.
 * @param entryPoint The position around which to attempt to place the unit.
 * @return True if the unit was successfully placed.
 */
bool SavedBattleGame::placeUnitNearPosition(BattleUnit *unit, const Position& entryPoint, bool largeFriend)
{
	if (setUnitPosition(unit, entryPoint))
	{
		return true;
	}

	int me = 0 - unit->getArmor()->getSize();
	int you = largeFriend ? 2 : 1;
	int xArray[8] = {0, you, you, you, 0, me, me, me};
	int yArray[8] = {me, me, 0, you, you, you, 0, me};
	for (int dir = 0; dir <= 7; ++dir)
	{
		Position offset = Position (xArray[dir], yArray[dir], 0);
		Tile *t = getTile(entryPoint + offset);
		if (t && !getPathfinding()->isBlockedDirection(unit, getTile(entryPoint + (offset / 2)), dir)
			&& setUnitPosition(unit, entryPoint + offset))
		{
			return true;
		}
	}

	if (unit->getMovementType() == MT_FLY)
	{
		Tile *t = getTile(entryPoint + Position(0, 0, 1));
		if (t && t->hasNoFloor(this) && setUnitPosition(unit, entryPoint + Position(0, 0, 1)))
		{
			return true;
		}
	}
	return false;
}

/**
 * Resets the turn counter.
 */
void SavedBattleGame::resetTurnCounter()
{
	_turn = 0;
	_cheating = false;
	_side = FACTION_PLAYER;
	_beforeGame = true;
}

/**
 * Resets visibility of all the tiles on the map.
 */
void SavedBattleGame::resetTiles()
{
	for (int i = 0; i != getMapSizeXYZ(); ++i)
	{
		_tiles[i].setDiscovered(false, O_WESTWALL);
		_tiles[i].setDiscovered(false, O_NORTHWALL);
		_tiles[i].setDiscovered(false, O_FLOOR);
	}
}

/**
 * @return the tile search vector for use in AI functions.
 */
const std::vector<Position> &SavedBattleGame::getTileSearch() const
{
	return _tileSearch;
}

/**
 * is the AI allowed to cheat?
 * @return true if cheating.
 */
bool SavedBattleGame::isCheating() const
{
	return _cheating;
}

/**
 * Gets the TU reserved type.
 * @return A battle action type.
 */
BattleActionType SavedBattleGame::getTUReserved() const
{
	return _tuReserved;
}

/**
 * Sets the TU reserved type.
 * @param reserved A battle action type.
 */
void SavedBattleGame::setTUReserved(BattleActionType reserved)
{
	_tuReserved = reserved;
}

/**
 * Gets the kneel reservation setting.
 * @return Should we reserve an extra 4 TUs to kneel?
 */
bool SavedBattleGame::getKneelReserved() const
{
	return _kneelReserved;
}

/**
 * Sets the kneel reservation setting.
 * @param reserved Should we reserve an extra 4 TUs to kneel?
 */
void SavedBattleGame::setKneelReserved(bool reserved)
{
	_kneelReserved = reserved;
}

/**
 * Return a reference to the base module destruction map
 * this map contains information on how many destructible base modules
 * remain at any given grid reference in the basescape, using [x][y] format.
 * -1 for "no items" 0 for "destroyed" and any actual number represents how many left.
 * @return the base module damage map.
 */
std::vector< std::vector<std::pair<int, int> > > &SavedBattleGame::getModuleMap()
{
	return _baseModules;
}

/**
 * calculate the number of map modules remaining by counting the map objects
 * on the top floor who have the baseModule flag set. we store this data in the grid
 * as outlined in the comments above, in pairs representing initial and current values.
 */
void SavedBattleGame::calculateModuleMap()
{
	_baseModules.resize((_mapsize_x / 10), std::vector<std::pair<int, int> >((_mapsize_y / 10), std::make_pair(-1, -1)));

	for (int x = 0; x != _mapsize_x; ++x)
	{
		for (int y = 0; y != _mapsize_y; ++y)
		{
			for (int z = 0; z != _mapsize_z; ++z)
			{
				Tile *tile = getTile(Position(x,y,z));
				if (tile && tile->getMapData(O_OBJECT) && tile->getMapData(O_OBJECT)->isBaseModule())
				{
					_baseModules[x/10][y/10].first += _baseModules[x/10][y/10].first > 0 ? 1 : 2;
					_baseModules[x/10][y/10].second = _baseModules[x/10][y/10].first;
				}
			}
		}
	}
}

/**
 * get a pointer to the geoscape save
 * @return a pointer to the geoscape save.
 */
SavedGame *SavedBattleGame::getGeoscapeSave() const
{
	return _battleState->getGame()->getSavedGame();
}

/**
 * check the depth of the battlescape.
 * @return depth.
 */
int SavedBattleGame::getDepth() const
{
	return _depth;
}

/**
 * set the depth of the battlescape game.
 * @param depth the intended depth 0-3.
 */
void SavedBattleGame::setDepth(int depth)
{
	_depth = depth;
}

/**
 * uses the depth variable to choose a palette.
 * @param state the state to set the palette for.
 */
void SavedBattleGame::setPaletteByDepth(State *state)
{
	if (_depth == 0)
	{
		state->setStandardPalette("PAL_BATTLESCAPE");
	}
	else
	{
		std::ostringstream ss;
		ss << "PAL_BATTLESCAPE_" << _depth;
		state->setStandardPalette(ss.str());
	}
}

/**
 * set the ambient battlescape sound effect.
 * @param sound the intended sound.
 */
void SavedBattleGame::setAmbientSound(int sound)
{
	_ambience = sound;
}

/**
 * get the ambient battlescape sound effect.
 * @return the intended sound.
 */
int SavedBattleGame::getAmbientSound() const
{
	return _ambience;
}

/**
 * Reset the current random ambient sound delay.
 */
void SavedBattleGame::resetCurrentAmbienceDelay()
{
	_currentAmbienceDelay = RNG::seedless(_minAmbienceRandomDelay * 10, _maxAmbienceRandomDelay * 10);
	if (_currentAmbienceDelay < 10)
		_currentAmbienceDelay = 10; // at least 1 second
}

/**
 * Play a random ambient sound.
 */
void SavedBattleGame::playRandomAmbientSound()
{
	if (!_ambienceRandom.empty())
	{
		int soundIndex = RNG::seedless(0, _ambienceRandom.size() - 1);
		getMod()->getSoundByDepth(_depth, _ambienceRandom.at(soundIndex))->play(3); // use fixed ambience channel; don't check if previous sound is still playing or not
	}
}

/**
 * get the list of items we're guaranteed to take with us (ie: items that were in the skyranger)
 * @return the list of items we're guaranteed.
 */
std::vector<BattleItem*> *SavedBattleGame::getGuaranteedRecoveredItems()
{
	return &_recoverGuaranteed;
}

/**
 * get the list of items we're not guaranteed to take with us (ie: items that were NOT in the skyranger)
 * @return the list of items we might get.
 */
std::vector<BattleItem*> *SavedBattleGame::getConditionalRecoveredItems()
{
	return &_recoverConditional;
}

/**
 * Get the music track for the current battle.
 * @return the name of the music track.
 */
const std::string &SavedBattleGame::getMusic() const
{
	return _music;
}

/**
 * Set the music track for this battle.
 * @param track the track name.
 */
void SavedBattleGame::setMusic(const std::string &track)
{
	_music = track;
}

/**
 * Sets the VIP escape type.
 */
void SavedBattleGame::setVIPEscapeType(EscapeType vipEscapeType)
{
	_vipEscapeType = vipEscapeType;
}

/**
 * Gets the VIP escape type.
 */
EscapeType SavedBattleGame::getVIPEscapeType() const
{
	return _vipEscapeType;
}

/**
 * Sets the percentage of VIPs that must survive in order to accomplish the mission.
 * If the mission has multiple stages, the biggest setting is used at the end.
 */
void SavedBattleGame::setVIPSurvivalPercentage(int vipSurvivalPercentage)
{
	_vipSurvivalPercentage = std::max(_vipSurvivalPercentage, vipSurvivalPercentage);
}

/**
 * Gets the percentage of VIPs that must survive in order to accomplish the mission.
 */
int SavedBattleGame::getVIPSurvivalPercentage() const
{
	return _vipSurvivalPercentage;
}

/**
 * Increase the saved VIPs counter and score.
 */
void SavedBattleGame::addSavedVIP(int score)
{
	_vipsSaved++;
	_vipsSavedScore += score;
}

/**
 * Gets the saved VIPs counter.
 */
int SavedBattleGame::getSavedVIPs() const
{
	return _vipsSaved;
}

/**
 * Gets the saved VIPs total score.
 */
int SavedBattleGame::getSavedVIPsScore() const
{
	return _vipsSavedScore;
}

/**
 * Increase the lost VIPs counter and score.
 */
void SavedBattleGame::addLostVIP(int score)
{
	_vipsLost++;
	_vipsLostScore -= score;
}

/**
 * Gets the lost VIPs counter.
 */
int SavedBattleGame::getLostVIPs() const
{
	return _vipsLost;
}

/**
 * Gets the lost VIPs total score.
 */
int SavedBattleGame::getLostVIPsScore() const
{
	return _vipsLostScore;
}

/**
 * Increase the waiting outside VIPs counter and score.
 */
void SavedBattleGame::addWaitingOutsideVIP(int score)
{
	_vipsWaitingOutside++;
	_vipsWaitingOutsideScore += score;
}

/**
 * Corrects the VIP stats based on the final mission outcome.
 */
void SavedBattleGame::correctVIPStats(bool success, bool retreated)
{
	if (success)
	{
		// if we won, all waiting VIPs are saved
		_vipsSaved += _vipsWaitingOutside;
		_vipsWaitingOutside = 0;

		_vipsSavedScore += _vipsWaitingOutsideScore;
		_vipsWaitingOutsideScore = 0;
	}
	else
	{
		// if we lost, all waiting VIPs are lost
		_vipsLost += _vipsWaitingOutside;
		_vipsWaitingOutside = 0;

		_vipsLostScore -= _vipsWaitingOutsideScore;
		_vipsWaitingOutsideScore = 0;

		if (retreated)
		{
			// if we retreated, keep all VIPs waiting in the craft alive
		}
		else
		{
			// if nobody from xcom survived, all VIPs are lost too
			_vipsLost += _vipsSaved;
			_vipsSaved = 0;

			_vipsLostScore -= _vipsSavedScore;
			_vipsSavedScore = 0;
		}
	}
}

/**
 * Set the objective type for the current battle.
 * @param the objective type.
 */
void SavedBattleGame::setObjectiveType(int type)
{
	_objectiveType = type;
}

/**
 * Get the objective type for the current battle.
 * @return the objective type.
 */
SpecialTileType SavedBattleGame::getObjectiveType() const
{
	return (SpecialTileType)(_objectiveType);
}



/**
 * Sets the ambient sound effect volume.
 * @param volume the ambient volume.
 */
void SavedBattleGame::setAmbientVolume(double volume)
{
	_ambientVolume = volume;
}

/**
 * Gets the ambient sound effect volume.
 * @return the ambient sound volume.
 */
double SavedBattleGame::getAmbientVolume() const
{
	return _ambientVolume;
}

/**
 * Gets the maximum number of turns we have before this mission ends.
 * @return the turn limit.
 */
int SavedBattleGame::getTurnLimit() const
{
	return _turnLimit;
}

/**
 * Gets the action type to perform when the timer expires.
 * @return the action type to perform.
 */
ChronoTrigger SavedBattleGame::getChronoTrigger() const
{
	return _chronoTrigger;
}

/**
 * Sets the turn limit for this mission.
 * @param limit the turn limit.
 */
void SavedBattleGame::setTurnLimit(int limit)
{
	_turnLimit = limit;
}

/**
 * Sets the action type to occur when the timer runs out.
 * @param trigger the action type to perform.
 */
void SavedBattleGame::setChronoTrigger(ChronoTrigger trigger)
{
	_chronoTrigger = trigger;
}

/**
 * Sets the turn at which the players become exposed to the AI.
 * @param turn the turn to start cheating.
 */
void SavedBattleGame::setCheatTurn(int turn)
{
	_cheatTurn = turn;
}

bool SavedBattleGame::isBeforeGame() const
{
	return _beforeGame;
}

/**
 * Randomly chooses hidden movement background.
 */
void SavedBattleGame::setRandomHiddenMovementBackground(const Mod *mod)
{
	if (mod && !mod->getHiddenMovementBackgrounds().empty())
	{
		int rng = RNG::seedless(0, mod->getHiddenMovementBackgrounds().size() - 1);
		_hiddenMovementBackground = mod->getHiddenMovementBackgrounds().at(rng);
	}
	else
	{
		_hiddenMovementBackground = "TAC00.SCR";
	}
}

/**
 * Gets the hidden movement background ID.
 * @return hidden movement background ID
 */
const std::string& SavedBattleGame::getHiddenMovementBackground() const
{
	return _hiddenMovementBackground;
}

/**
 * Appends a given entry to the hit log. Works only during the player's turn.
 */
void SavedBattleGame::appendToHitLog(HitLogEntryType type, UnitFaction faction)
{
	if (_side != FACTION_PLAYER) return;
	_hitLog->appendToHitLog(type, faction);
}

/**
 * Appends a given entry to the hit log. Works only during the player's turn.
 */
void SavedBattleGame::appendToHitLog(HitLogEntryType type, UnitFaction faction, const std::string &text)
{
	if (_side != FACTION_PLAYER) return;
	_hitLog->appendToHitLog(type, faction, text);
}

/**
 * Gets the hit log.
 * @return hit log
 */
const HitLog *SavedBattleGame::getHitLog() const
{
	return _hitLog;
}

/**
 * Resets all unit hit state flags.
 */
void SavedBattleGame::resetUnitHitStates()
{
	for (auto* bu : _units)
	{
		bu->resetHitState();
	}
}

////////////////////////////////////////////////////////////
//					Script binding
////////////////////////////////////////////////////////////

namespace
{

template<typename... Args>
void flashMessageVariadicScriptImpl(SavedBattleGame* sbg, ScriptText message, Args... args)
{
	if (!sbg || !sbg->getBattleState())
	{
		return;
	}
	const Language *lang = sbg->getBattleState()->getGame()->getLanguage();
	LocalizedText translated = lang->getString(message);
	(translated.arg(args), ...);
	sbg->getBattleState()->warningRaw(translated);
}

template<typename... Args>
void flashLongMessageVariadicScriptImpl(SavedBattleGame* sbg, ScriptText message, Args... args)
{
	if (!sbg || !sbg->getBattleState())
	{
		return;
	}
	const Language *lang = sbg->getBattleState()->getGame()->getLanguage();
	LocalizedText translated = lang->getString(message);
	(translated.arg(args), ...);
	sbg->getBattleState()->warningLongRaw(translated);
}



void randomChanceScript(SavedBattleGame* sbg, int& val)
{
	if (sbg)
	{
		val = RNG::percent(val);
	}
	else
	{
		val = 0;
	}
}

void randomRangeScript(SavedBattleGame* sbg, int& val, int min, int max)
{
	if (sbg && max >= min)
	{
		val = RNG::generate(min, max);
	}
	else
	{
		val = 0;
	}
}

void difficultyLevelScript(const SavedBattleGame* sbg, int& val)
{
	if (sbg)
	{
		val = sbg->getGeoscapeSave()->getDifficulty();
	}
	else
	{
		val = 0;
	}
}

void turnSideScript(const SavedBattleGame* sbg, int& val)
{
	if (sbg)
	{
		val = sbg->getSide();
	}
	else
	{
		val = 0;
	}
}

void getGeoscapeSaveScript(const SavedBattleGame* sbg, const SavedGame*& val)
{
	if (sbg)
	{
		val = sbg->getGeoscapeSave();
	}
	else
	{
		val = nullptr;
	}
}

void getGeoscapeSaveScript(SavedBattleGame* sbg, SavedGame*& val)
{
	if (sbg)
	{
		val = sbg->getGeoscapeSave();
	}
	else
	{
		val = nullptr;
	}
}

void getTileScript(const SavedBattleGame* sbg, const Tile*& t, int x, int y, int z)
{
	if (sbg)
	{
		t = sbg->getTile(Position(x, y, z));
	}
	else
	{
		t = nullptr;
	}
}

void getTileEditableScript(SavedBattleGame* sbg, Tile*& t, int x, int y, int z)
{
	if (sbg)
	{
		t = sbg->getTile(Position(x, y, z));
	}
	else
	{
		t = nullptr;
	}
}


bool filterUnitScript(SavedBattleGame* sbg, BattleUnit* unit)
{
	return unit && !unit->isIgnored() && unit->getStatus() != STATUS_DEAD;
}

bool filterUnitFactionScript(SavedBattleGame* sbg, BattleUnit* unit, int i)
{
	return filterUnitScript(sbg, unit) && unit->getFaction() == i;
}


bool filterItemScript(SavedBattleGame* sbg, BattleItem* item)
{
	return item && !item->isOwnerIgnored();
}


void setAlienItemLevelScript(SavedBattleGame* sbg, int val)
{
	if (sbg)
	{
		sbg->setAlienItemLevel(Clamp(val, 0, (int)sbg->getMod()->getAlienItemLevels().size()));
	}
}

void setReinforcementsItemLevelScript(SavedBattleGame* sbg, int val)
{
	if (sbg)
	{
		sbg->setReinforcementsItemLevel(Clamp(val, 0, (int)sbg->getMod()->getAlienItemLevels().size()));
	}
}

void tryConcealUnitScript(SavedBattleGame* sbg, BattleUnit* bu, int& val)
{
	if (sbg && bu)
	{
		val = sbg->getTileEngine()->tryConcealUnit(bu);
	}
	else
	{
		val = 0;
	}
}

void isAltPressedScript(const SavedBattleGame* sbg, int& val)
{
	if (sbg)
	{
		val = sbg->isAltPressed(true);
	}
	else
	{
		val = 0;
	}
}

void isCtrlPressedScript(const SavedBattleGame* sbg, int& val)
{
	if (sbg)
	{
		val = sbg->isCtrlPressed(true);
	}
	else
	{
		val = 0;
	}
}

void isShiftPressedScript(const SavedBattleGame* sbg, int& val)
{
	if (sbg)
	{
		val = sbg->isShiftPressed(true);
	}
	else
	{
		val = 0;
	}
}



std::string debugDisplayScript(const SavedBattleGame* p)
{
	if (p)
	{
		std::string s;
		s += "BattleGame";
		s += "(missionType: \"";
		s += p->getMissionType();
		s += "\" missionTarget: \"";
		s += p->getMissionTarget();
		s += "\" turn: ";
		s += std::to_string(p->getTurn());
		s += ")";
		return s;
	}
	else
	{
		return "null";
	}
}

} // namespace

/**
 * Register Armor in script parser.
 * @param parser Script parser.
 */
void SavedBattleGame::ScriptRegister(ScriptParserBase* parser)
{
	parser->registerPointerType<SavedGame>();
	parser->registerPointerType<Tile>();

	Bind<SavedBattleGame> sbg = { parser };

	sbg.add<&SavedBattleGame::getTurn>("getTurn", "Current turn, 0 - before battle, 1 - first turn, each stage reset this value.");
	sbg.add<&SavedBattleGame::getAnimFrame>("getAnimFrame");
	sbg.add<&SavedBattleGame::getMapSizeX>("getSize.getX", "Get size in x direction");
	sbg.add<&SavedBattleGame::getMapSizeY>("getSize.getY", "Get size in y direction");
	sbg.add<&SavedBattleGame::getMapSizeZ>("getSize.getZ", "Get size in z direction");
	sbg.add<&getTileScript>("getTile", "Get tile on position x, y, z");
	sbg.add<&getTileEditableScript>("getTile", "Get tile on position x, y, z");
	sbg.addList<&filterUnitScript, &SavedBattleGame::_units>("getUnits", "Get list of all units");
	sbg.addList<&filterUnitFactionScript, &SavedBattleGame::_units>("getUnits.byFaction", "Get list of units from faction");
	sbg.addList<&filterItemScript, &SavedBattleGame::_items>("getItems", "Get list of all items");

	sbg.add<&SavedBattleGame::getAlienItemLevel>("getAlienItemLevel");
	sbg.add<&setAlienItemLevelScript>("setAlienItemLevel");
	sbg.add<&SavedBattleGame::getReinforcementsItemLevel>("getReinforcementsItemLevel");
	sbg.add<&setReinforcementsItemLevelScript>("setReinforcementsItemLevel");

	sbg.addPair<SavedGame, &getGeoscapeSaveScript, &getGeoscapeSaveScript>("getGeoscapeGame");

	sbg.add<void(*)(SavedBattleGame*, ScriptText), &flashMessageVariadicScriptImpl>("flashMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int), &flashMessageVariadicScriptImpl>("flashMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int, int), &flashMessageVariadicScriptImpl>("flashMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int, int, int), &flashMessageVariadicScriptImpl>("flashMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int, int, int, int), &flashMessageVariadicScriptImpl>("flashMessage");

	sbg.add<void(*)(SavedBattleGame*, ScriptText), &flashLongMessageVariadicScriptImpl>("flashLongMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int), &flashLongMessageVariadicScriptImpl>("flashLongMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int, int), &flashLongMessageVariadicScriptImpl>("flashLongMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int, int, int), &flashLongMessageVariadicScriptImpl>("flashLongMessage");
	sbg.add<void(*)(SavedBattleGame*, ScriptText, int, int, int, int), &flashLongMessageVariadicScriptImpl>("flashLongMessage");

	sbg.add<&randomChanceScript>("randomChance", "first argument is percent in range 0 - 100, then return in that argument random 1 or 0 based on percent");
	sbg.add<&randomRangeScript>("randomRange", "set in first argument random value from range given in two last arguments");
	sbg.add<&turnSideScript>("getTurnSide", "Return the faction whose turn it is.");
	sbg.addCustomConst("FACTION_PLAYER", FACTION_PLAYER);
	sbg.addCustomConst("FACTION_HOSTILE", FACTION_HOSTILE);
	sbg.addCustomConst("FACTION_NEUTRAL", FACTION_NEUTRAL);

	sbg.add<&tryConcealUnitScript>("tryConcealUnit");

	sbg.add<&difficultyLevelScript>("difficultyLevel");

	sbg.addScriptValue<&SavedBattleGame::_scriptValues>();

	sbg.addDebugDisplay<&debugDisplayScript>();

	sbg.addCustomConst("DIFF_BEGINNER", DIFF_BEGINNER);
	sbg.addCustomConst("DIFF_EXPERIENCED", DIFF_EXPERIENCED);
	sbg.addCustomConst("DIFF_VETERAN", DIFF_VETERAN);
	sbg.addCustomConst("DIFF_GENIUS", DIFF_GENIUS);
	sbg.addCustomConst("DIFF_SUPERHUMAN", DIFF_SUPERHUMAN);
}

/*
* Used for FOW updates
* Called in BattleGame popState and init, and when unit is moving in map.cpp
*/
void SavedBattleGame::updateVisibleTiles()
{
	_currentlyVisibleTiles.clear();
	for (BattleUnit* unit : _units)
	{
		if (unit->getFaction() == FACTION_PLAYER)
			_currentlyVisibleTiles.insert(unit->getVisibleTiles()->begin(), unit->getVisibleTiles()->end());
	}
}

/*
*
* Returns true if tile is visible to player
*/
bool SavedBattleGame::isTileVisible(Tile* sometile)
{
	return (_currentlyVisibleTiles.find(sometile) != _currentlyVisibleTiles.end());
}

/**
 * Register useful function used by graphic scripts.
 */
void SavedBattleGame::ScriptRegisterUnitAnimations(ScriptParserBase* parser)
{
	Bind<SavedBattleGame> sbg = { parser, BindBase::ExtensionBinding{} };

	sbg.add<&isAltPressedScript>("isAltPressed");
	sbg.add<&isCtrlPressedScript>("isCtrlPressed");
	sbg.add<&isShiftPressedScript>("isShiftPressed");
	sbg.addField<&SavedBattleGame::_toggleBrightnessTemp>("getDebugVisionMode");
	sbg.addField<&SavedBattleGame::_toggleNightVisionTemp>("isNightVisionEnabled");
	sbg.addField<&SavedBattleGame::_togglePersonalLightTemp>("isPersonalLightEnabled");
	sbg.addField<&SavedBattleGame::_toggleNightVisionColorTemp>("getNightVisionColor");
}

bool SavedBattleGame::hasObjectives()
{
	return _objectivesNeeded > 0;
}

bool SavedBattleGame::hasExitZone()
{
	bool hasExitZone = false;
	for (int i = 0; i < getMapSizeXYZ(); ++i)
	{
		Tile* tile = getTile(i);
		if (tile && tile->getFloorSpecialTileType() == END_POINT)
		{
			hasExitZone = true;
			break;
		}
	}
	return hasExitZone;
}

}
