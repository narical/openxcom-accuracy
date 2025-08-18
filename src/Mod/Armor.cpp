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
#include "Armor.h"
#include "Unit.h"
#include "../Engine/ScriptBind.h"
#include "LoadYaml.h"
#include "Mod.h"
#include "RuleSoldier.h"
#include "../Savegame/SoldierDiary.h"

namespace OpenXcom
{

const std::string Armor::NONE = "STR_NONE";

/**
 * Creates a blank ruleset for a certain
 * type of armor.
 * @param type String defining the type.
 */
Armor::Armor(const std::string &type, int listOrder) :
	_type(type), _infiniteSupply(false), _frontArmor(0), _sideArmor(0), _leftArmorDiff(0), _rearArmor(0), _underArmor(0),
	_drawingRoutine(0), _drawBubbles(false), _movementType(MT_WALK), _specab(SPECAB_NONE), _turnBeforeFirstStep(false), _turnCost(1), _moveSound(-1), _size(1), _spaceOccupied(-1), _weight(0),
	_visibilityAtDark(0), _visibilityAtDay(0),
	_camouflageAtDay(0), _camouflageAtDark(0), _antiCamouflageAtDay(0), _antiCamouflageAtDark(0),
	_visibilityThroughSmoke(0), _visibilityThroughFire(100),
	_psiVision(0), _psiCamouflage(0),
	_deathFrames(3), _constantAnimation(false), _hasInventory(true), _forcedTorso(TORSO_USE_GENDER),
	_faceColorGroup(0), _hairColorGroup(0), _utileColorGroup(0), _rankColorGroup(0),
	_fearImmune(defBoolNullable), _bleedImmune(defBoolNullable), _painImmune(defBoolNullable), _zombiImmune(defBoolNullable),
	_ignoresMeleeThreat(defBoolNullable), _createsMeleeThreat(defBoolNullable),
	_overKill(0.5f), _meleeDodgeBackPenalty(0),
	_allowsRunning(defBoolNullable), _allowsStrafing(defBoolNullable), _allowsSneaking(defBoolNullable), _allowsKneeling(defBoolNullable), _allowsMoving(1),
	_isPilotArmor(false), _allowTwoMainWeapons(false), _instantWoundRecovery(false),
	_standHeight(-1), _kneelHeight(-1), _floatHeight(-1), _meleeOriginVoxelVerticalOffset(0), _group(0), _listOrder(listOrder)
{
	for (int i=0; i < DAMAGE_TYPES; i++)
		_damageModifier[i] = 1.0f;

	_psiDefence.setPsiDefense();
	_timeRecovery.setTimeRecovery();
	_energyRecovery.setEnergyRecovery();
	_stunRecovery.setStunRecovery();

	_customArmorPreviewIndex.push_back(Mod::NO_SURFACE);
}

/**
 *
 */
Armor::~Armor()
{

}

/**
 * Loads the armor from a YAML file.
 * @param node YAML node.
 */
void Armor::load(const YAML::YamlNodeReader& node, Mod *mod, const ModScript &parsers)
{
	const auto& reader = node.useIndex();
	if (const YAML::YamlNodeReader& parent = reader["refNode"])
	{
		load(parent, mod, parsers);
	}

	reader.tryRead("ufopediaType", _ufopediaType);
	reader.tryRead("spriteSheet", _spriteSheet);
	reader.tryRead("spriteInv", _spriteInv);
	reader.tryRead("allowInv", _hasInventory);
	if (reader["corpseItem"])
	{
		_corpseBattleNames.clear();
		_corpseBattleNames.push_back(reader["corpseItem"].readVal<std::string>());
		_corpseGeoName = _corpseBattleNames[0];
	}
	else if (reader["corpseBattle"])
	{
		mod->loadNames(_type, _corpseBattleNames, reader["corpseBattle"]);
		_corpseGeoName = _corpseBattleNames.at(0);
	}
	mod->loadNames(_type, _builtInWeaponsNames, reader["builtInWeapons"]);
	mod->loadName(_type, _corpseGeoName, reader["corpseGeo"]);
	mod->loadNameNull(_type, _storeItemName, reader["storeItem"]);
	mod->loadNameNull(_type, _selfDestructItemName, reader["selfDestructItem"]);
	mod->loadNameNull(_type, _specWeaponName, reader["specialWeapon"]);
	mod->loadNameNull(_type, _requiresName, reader["requires"]);
	mod->loadNameNull(_type, _requiresAwardName, reader["requiresAward"]);
	mod->loadNameNull(_type, _requiresBonusName, reader["requiresBonus"]);

	reader.tryRead("layersDefaultPrefix", _layersDefaultPrefix);
	reader.tryRead("layersSpecificPrefix", _layersSpecificPrefix);
	reader.tryRead("layersDefinition", _layersDefinition);

	reader.tryRead("frontArmor", _frontArmor);
	reader.tryRead("sideArmor", _sideArmor);
	reader.tryRead("leftArmorDiff", _leftArmorDiff);
	reader.tryRead("rearArmor", _rearArmor);
	reader.tryRead("underArmor", _underArmor);
	reader.tryRead("drawingRoutine", _drawingRoutine);
	reader.tryRead("drawBubbles", _drawBubbles);
	reader.tryRead("movementType", _movementType);
	reader.tryRead("specab", _specab);

	if (const YAML::YamlNodeReader& ai = reader["ai"])
	{
		ai.tryRead("targetWeightAsHostile", _aiTargetWeightAsHostile);
		ai.tryRead("targetWeightAsHostileCivilians", _aiTargetWeightAsHostileCivilians);
		ai.tryRead("targetWeightAsFriendly", _aiTargetWeightAsFriendly);
		ai.tryRead("targetWeightAsNeutral", _aiTargetWeightAsNeutral);
	}

	reader.tryRead("turnBeforeFirstStep", _turnBeforeFirstStep);
	reader.tryRead("turnCost", _turnCost);
	if (const YAML::YamlNodeReader& move = reader["moveCost"])
	{
		_moveCostBase.load(move["basePercent"]);
		_moveCostBaseFly.load(move["baseFlyPercent"]);
		_moveCostBaseClimb.load(move["baseClimbPercent"]);
		_moveCostBaseNormal.load(move["baseNormalPercent"]);

		_moveCostWalk.load(move["walkPercent"]);
		_moveCostRun.load(move["runPercent"]);
		_moveCostStrafe.load(move["strafePercent"]);
		_moveCostSneak.load(move["sneakPercent"]);

		_moveCostFlyWalk.load(move["flyWalkPercent"]);
		_moveCostFlyRun.load(move["flyRunPercent"]);
		_moveCostFlyStrafe.load(move["flyStrafePercent"]);

		_moveCostFlyUp.load(move["flyUpPercent"]);
		_moveCostFlyDown.load(move["flyDownPercent"]);

		_moveCostClimbUp.load(move["climbUpPercent"]);
		_moveCostClimbDown.load(move["climbDownPercent"]);

		_moveCostGravLift.load(move["gravLiftPercent"]);
	}

	mod->loadSoundOffset(_type, _moveSound, reader["moveSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _deathSoundMale, reader["deathMale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _deathSoundFemale, reader["deathFemale"], "BATTLE.CAT");

	mod->loadSoundOffset(_type, _selectUnitSoundMale, reader["selectUnitMale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _selectUnitSoundFemale, reader["selectUnitFemale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _startMovingSoundMale, reader["startMovingMale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _startMovingSoundFemale, reader["startMovingFemale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _selectWeaponSoundMale, reader["selectWeaponMale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _selectWeaponSoundFemale, reader["selectWeaponFemale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _annoyedSoundMale, reader["annoyedMale"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _annoyedSoundFemale, reader["annoyedFemale"], "BATTLE.CAT");

	reader.tryRead("weight", _weight);
	reader.tryRead("visibilityAtDark", _visibilityAtDark);
	reader.tryRead("visibilityAtDay", _visibilityAtDay);
	reader.tryRead("personalLight", _personalLightFriend);
	reader.tryRead("personalLightHostile", _personalLightHostile);
	reader.tryRead("personalLightNeutral", _personalLightNeutral);
	reader.tryRead("camouflageAtDay", _camouflageAtDay);
	reader.tryRead("camouflageAtDark", _camouflageAtDark);
	reader.tryRead("antiCamouflageAtDay", _antiCamouflageAtDay);
	reader.tryRead("antiCamouflageAtDark", _antiCamouflageAtDark);
	reader.tryRead("heatVision", _visibilityThroughSmoke);
	reader.tryRead("visibilityThroughFire", _visibilityThroughFire);
	reader.tryRead("psiVision", _psiVision);
	reader.tryRead("psiCamouflage", _psiCamouflage);
	reader.tryRead("alwaysVisible", _isAlwaysVisible);

	_stats.merge(reader["stats"].readVal(_stats));
	if (const YAML::YamlNodeReader& dmg = reader["damageModifier"])
	{
		size_t end = std::min(dmg.childrenCount(), (size_t)DAMAGE_TYPES);
		for (size_t i = 0; i < end; ++i)
		{
			_damageModifier[i] = dmg[i].readVal<float>();
		}
	}
	mod->loadInts(_type, _loftempsSet, reader["loftempsSet"]);
	if (reader["loftemps"])
		_loftempsSet = { reader["loftemps"].readVal<int>() };
	reader.tryRead("deathFrames", _deathFrames);
	reader.tryRead("constantAnimation", _constantAnimation);
	reader.tryRead("forcedTorso", _forcedTorso);
	if (reader.tryRead("size", _size))
	{
		if (_size != 1) //TODO: Add handling for opposite case too
		{
			_fearImmune = 1;
			_bleedImmune = 1;
			_painImmune = 1;
			_zombiImmune = 1;
			_ignoresMeleeThreat = 1;
			_createsMeleeThreat = 0;
		}
	}
	reader.tryRead("spaceOccupied", _spaceOccupied);
	loadBoolNullable(_fearImmune, reader["fearImmune"]);
	loadBoolNullable(_bleedImmune, reader["bleedImmune"]);
	loadBoolNullable(_painImmune, reader["painImmune"]);
	if (_size == 1) //Big units are always immune, because game we don't have 2x2 unit zombie
	{
		loadBoolNullable(_zombiImmune, reader["zombiImmune"]);
	}
	loadBoolNullable(_ignoresMeleeThreat, reader["ignoresMeleeThreat"]);
	loadBoolNullable(_createsMeleeThreat, reader["createsMeleeThreat"]);

	reader.tryRead("overKill", _overKill);
	reader.tryRead("meleeDodgeBackPenalty", _meleeDodgeBackPenalty);

	_psiDefence.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::PsiDefenceStatBonus>());
	_meleeDodge.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::MeleeDodgeStatBonus>());

	_timeRecovery.load(_type, reader["recovery"], parsers.bonusStatsScripts.get<ModScript::TimeRecoveryStatBonus>());
	_energyRecovery.load(_type, reader["recovery"], parsers.bonusStatsScripts.get<ModScript::EnergyRecoveryStatBonus>());
	_moraleRecovery.load(_type, reader["recovery"], parsers.bonusStatsScripts.get<ModScript::MoraleRecoveryStatBonus>());
	_healthRecovery.load(_type, reader["recovery"], parsers.bonusStatsScripts.get<ModScript::HealthRecoveryStatBonus>());
	_manaRecovery.load(_type, reader["recovery"], parsers.bonusStatsScripts.get<ModScript::ManaRecoveryStatBonus>());
	_stunRecovery.load(_type, reader["recovery"], parsers.bonusStatsScripts.get<ModScript::StunRecoveryStatBonus>());

	reader.tryRead("spriteFaceGroup", _faceColorGroup);
	reader.tryRead("spriteHairGroup", _hairColorGroup);
	reader.tryRead("spriteRankGroup", _rankColorGroup);
	reader.tryRead("spriteUtileGroup", _utileColorGroup);
	mod->loadInts(_type, _faceColor, reader["spriteFaceColor"]);
	mod->loadInts(_type, _hairColor, reader["spriteHairColor"]);
	mod->loadInts(_type, _rankColor, reader["spriteRankColor"]);
	mod->loadInts(_type, _utileColor, reader["spriteUtileColor"]);

	_battleUnitScripts.load(_type, reader, parsers.battleUnitScripts);

	mod->loadUnorderedNames(_type, _unitsNames, reader["units"]);
	mod->loadUnorderedInts(_type, _ranks, reader["ranks"]);
	_scriptValues.load(reader, parsers.getShared());
	mod->loadSpriteOffset(_type, _customArmorPreviewIndex, reader["customArmorPreviewIndex"], "CustomArmorPreviews");
	loadBoolNullable(_allowsRunning, reader["allowsRunning"]);
	loadBoolNullable(_allowsStrafing, reader["allowsStrafing"]);
	loadBoolNullable(_allowsSneaking, reader["allowsSneaking"]);
	loadBoolNullable(_allowsKneeling, reader["allowsKneeling"]);
	loadBoolNullable(_allowsMoving, reader["allowsMoving"]);
	reader.tryRead("isPilotArmor", _isPilotArmor);
	reader.tryRead("allowTwoMainWeapons", _allowTwoMainWeapons);
	reader.tryRead("instantWoundRecovery", _instantWoundRecovery);
	reader.tryRead("standHeight", _standHeight);
	reader.tryRead("kneelHeight", _kneelHeight);
	reader.tryRead("floatHeight", _floatHeight);
	reader.tryRead("meleeOriginVoxelVerticalOffset", _meleeOriginVoxelVerticalOffset);
	reader.tryRead("group", _group);
	reader.tryRead("listOrder", _listOrder);
}

/**
 * Cross link with other rules.
 */
void Armor::afterLoad(const Mod* mod)
{
	mod->verifySoundOffset(_type, _moveSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _deathSoundMale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _deathSoundFemale, "BATTLE.CAT");

	mod->verifySoundOffset(_type, _selectUnitSoundMale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _selectUnitSoundFemale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _startMovingSoundMale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _startMovingSoundFemale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _selectWeaponSoundMale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _selectWeaponSoundFemale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _annoyedSoundMale, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _annoyedSoundFemale, "BATTLE.CAT");

	mod->verifySpriteOffset(_type, _customArmorPreviewIndex, "CustomArmorPreviews");


	mod->linkRule(_corpseBattle, _corpseBattleNames);
	mod->linkRule(_corpseGeo, _corpseGeoName);
	mod->linkRule(_builtInWeapons, _builtInWeaponsNames);
	mod->linkRule(_units, _unitsNames);
	mod->linkRule(_requires, _requiresName);
	mod->linkRule(_requiresAward, _requiresAwardName);
	mod->linkRule(_requiresBonus, _requiresBonusName);
	if (_storeItemName == Armor::NONE)
	{
		_infiniteSupply = true;
	}
	mod->linkRule(_storeItem, _storeItemName); //special logic there: "STR_NONE" -> nullptr
	mod->linkRule(_selfDestructItem, _selfDestructItemName);
	mod->linkRule(_specWeapon, _specWeaponName);


	{
		auto totalSize = (size_t)getTotalSize();

		mod->checkForSoftError(_corpseBattle.size() != totalSize, _type, "Number of battle corpse items for 'corpseBattle' does not match the armor size.", LOG_ERROR);
		mod->checkForSoftError(_loftempsSet.size() != totalSize, _type, "Number of defined templates for 'loftempsSet' or 'loftemps' does not match the armor size.", LOG_ERROR);

		auto s = mod->getVoxelData()->size() / 16;
		for (auto& lof : _loftempsSet)
		{
			mod->checkForSoftError((size_t)lof >= s, _type, "Value " + std::to_string(lof) + " in 'loftempsSet' or 'loftemps' is larger than number of avaiable templates.", LOG_ERROR);
		}
	}

	int numCorpse = 0;
	for (auto* c : _corpseBattle)
	{
		if (!c)
		{
			throw Exception("Battle corpse item(s) cannot be empty.");
		}

		if (!numCorpse++)
		{
			// only the first item needs to be a corpse item
			mod->checkForSoftError(c->getBattleType() != BT_CORPSE, _type, "The first battle corpse item must be of item type 'corpse' (battleType: 11)");
		}
		else
		{
			mod->checkForSoftError(c->isRecoverable(), _type, "Multiple recoverable battle corpse item(s)");
		}
	}
	if (!_corpseGeo)
	{
		throw Exception("Geo corpse item cannot be empty.");
	}

	// calcualte final surfaces used by layers
	if (!_layersDefaultPrefix.empty())
	{
		for (auto& version : _layersDefinition)
		{
			int layerIndex = 0;
			for (auto& layerItem : version.second)
			{
				if (!layerItem.empty())
				{
					static std::string buf; //static buffer that grows on demand; that's what she said
					const auto& pre = _layersSpecificPrefix.find(layerIndex);
					const auto& prefix = pre != _layersSpecificPrefix.end() ? pre->second : _layersDefaultPrefix;
					size_t formattedLen = c4::format(c4::to_substr(buf), "{}__{}__{}", prefix, layerIndex, layerItem);
					if (formattedLen > buf.size())
					{
						buf.resize(formattedLen);
						c4::format(c4::to_substr(buf), "{}__{}__{}", prefix, layerIndex, layerItem);
					}
					layerItem.assign(buf.data(), formattedLen);

					//check if surface is valid
					if (Options::lazyLoadResources == false)
					{
						//TODO: remove `const_cast`
						mod->checkForSoftError(const_cast<Mod*>(mod)->getSurface(layerItem, false) == nullptr, _type, "Missing surface definition for '" + layerItem + "'", LOG_ERROR);
					}
				}
				layerIndex++;
			}
			//clean unused layers
			Collections::removeIf(version.second, [](const std::string& s) { return s.empty(); });
			version.second.shrink_to_fit();
		}
	}

	Collections::sortVector(_units);
	Collections::sortVector(_ranks);
}



/**
 * Gets the custom name of the Ufopedia article related to this armor.
 * @return The ufopedia article name.
 */
const std::string& Armor::getUfopediaType() const
{
	if (!_ufopediaType.empty())
		return _ufopediaType;

	return _type;
}

/**
 * Returns the language string that names
 * this armor. Each armor has a unique name. Coveralls, Power Suit,...
 * @return The armor name.
 */
const std::string& Armor::getType() const
{
	return _type;
}

/**
 * Gets the unit's sprite sheet.
 * @return The sprite sheet name.
 */
std::string Armor::getSpriteSheet() const
{
	return _spriteSheet;
}

/**
 * Gets the unit's inventory sprite.
 * @return The inventory sprite name.
 */
std::string Armor::getSpriteInventory() const
{
	return _spriteInv;
}

/**
 * Gets the front armor level.
 * @return The front armor level.
 */
int Armor::getFrontArmor() const
{
	return _frontArmor;
}

/**
 * Gets the left side armor level.
 * @return The left side armor level.
 */
int Armor::getLeftSideArmor() const
{
	return _sideArmor + _leftArmorDiff;
}

/**
* Gets the right side armor level.
* @return The right side armor level.
*/
int Armor::getRightSideArmor() const
{
	return _sideArmor;
}

/**
 * Gets the rear armor level.
 * @return The rear armor level.
 */
int Armor::getRearArmor() const
{
	return _rearArmor;
}

/**
 * Gets the under armor level.
 * @return The under armor level.
 */
int Armor::getUnderArmor() const
{
	return _underArmor;
}

/**
 * Gets the armor level of part.
 * @param side Part of armor.
 * @return The armor level of part.
 */
int Armor::getArmor(UnitSide side) const
{
	switch (side)
	{
	case SIDE_FRONT:	return _frontArmor;
	case SIDE_LEFT:		return _sideArmor + _leftArmorDiff;
	case SIDE_RIGHT:	return _sideArmor;
	case SIDE_REAR:		return _rearArmor;
	case SIDE_UNDER:	return _underArmor;
	default: return 0;
	}
}


/**
 * Gets the corpse item used in the Geoscape.
 * @return The name of the corpse item.
 */
const RuleItem* Armor::getCorpseGeoscape() const
{
	return _corpseGeo;
}

/**
 * Gets the list of corpse items dropped by the unit
 * in the Battlescape (one per unit tile).
 * @return The list of corpse items.
 */
const std::vector<const RuleItem*> &Armor::getCorpseBattlescape() const
{
	return _corpseBattle;
}

/**
 * Gets the storage item needed to equip this.
 * Every soldier armor needs an item.
 * @return The name of the store item (STR_NONE for infinite armor).
 */
const RuleItem* Armor::getStoreItem() const
{
	return _storeItem;
}

/**
 * Gets the type of special weapon.
 * @return The name of the special weapon.
 */
const RuleItem* Armor::getSpecialWeapon() const
{
	return _specWeapon;
}

/**
 * Gets the research required to be able to equip this armor.
 * @return The name of the research topic.
 */
const RuleResearch* Armor::getRequiredResearch() const
{
	return _requires;
}

/**
 * Gets the drawing routine ID.
 * @return The drawing routine ID.
 */
int Armor::getDrawingRoutine() const
{
	return _drawingRoutine;
}

/**
 * Gets whether or not to draw bubbles (breathing animation).
 * @return True if breathing animation is enabled, false otherwise.
 */
bool Armor::drawBubbles() const
{
	return _drawBubbles;
}

/**
 * Gets the movement type of this armor.
 * Useful for determining whether the armor can fly.
 * @important: do not use this function outside the BattleUnit constructor,
 * unless you are SURE you know what you are doing.
 * for more information, see the BattleUnit constructor.
 * @return The movement type.
 */
MovementType Armor::getMovementType() const
{
	return _movementType;
}

/**
 * Get MovementType based on depth of battle.
 */
MovementType Armor::getMovementTypeByDepth(int depth) const
{
	if (_movementType == MT_FLOAT)
	{
		if (depth > 0)
		{
			return MT_FLY;
		}
		else
		{
			return MT_WALK;
		}
	}
	else if (_movementType == MT_SINK)
	{
		if (depth == 0)
		{
			return MT_FLY;
		}
		else
		{
			return MT_WALK;
		}
	}
	else
	{
		return _movementType;
	}
}

/**
 * Gets the armor's special ability.
 * @return The armor's specab.
 */
int Armor::getSpecialAbility() const
{
	return (int)_specab;
}

/**
* Gets the armor's move sound.
* @return The id of the armor's move sound.
*/
int Armor::getMoveSound() const
{
	return _moveSound;
}

/**
 * Gets the size of the unit. Normally this is 1 (small) or 2 (big).
 * @return The unit's size.
 */
int Armor::getSize() const
{
	return _size;
}

/**
 * Gets the total size of the unit. Normally this is 1 for small or 4 for big.
 * @return The unit's size.
 */
int Armor::getTotalSize() const
{
	return _size * _size;
}
/**
 * Gets how much space the armor occupies in a craft. If not specified, uses armor size.
 * @return Space occupied by the unit.
 */
int Armor::getSpaceOccupied() const
{
	return (_spaceOccupied > -1 ? _spaceOccupied : getTotalSize());
}
/**
 * Gets the damage modifier for a certain damage type.
 * @param dt The damageType.
 * @return The damage modifier 0->1.
 */
float Armor::getDamageModifier(ItemDamageType dt) const
{
	return _damageModifier[(int)dt];
}

const std::vector<float> Armor::getDamageModifiersRaw() const
{
	std::vector<float> result;
	for (int i = 0; i < DAMAGE_TYPES; i++)
	{
		result.push_back(_damageModifier[i]);
	}
	return result;
}

/** Gets the loftempSet.
 * @return The loftempsSet.
 */
const std::vector<int>& Armor::getLoftempsSet() const
{
	return _loftempsSet;
}

/**
  * Gets pointer to the armor's stats.
  * @return stats Pointer to the armor's stats.
  */
const UnitStats *Armor::getStats() const
{
	return &_stats;
}

/**
 * Gets unit psi defense.
 */
int Armor::getPsiDefence(const BattleUnit* unit) const
{
	return _psiDefence.getBonus(unit);
}

/**
 * Gets unit melee dodge chance.
 */
int Armor::getMeleeDodge(const BattleUnit* unit) const
{
	return _meleeDodge.getBonus(unit);
}

/**
 * Gets unit dodge penalty if hit from behind.
 */
float Armor::getMeleeDodgeBackPenalty() const
{
	return _meleeDodgeBackPenalty;
}

/**
 *  Gets unit TU recovery.
 */
int Armor::getTimeRecovery(const BattleUnit* unit, int externalBonuses) const
{
	return _timeRecovery.getBonus(unit, externalBonuses);
}

/**
 *  Gets unit Energy recovery.
 */
int Armor::getEnergyRecovery(const BattleUnit* unit, int externalBonuses) const
{
	return _energyRecovery.getBonus(unit, externalBonuses);
}

/**
 *  Gets unit Morale recovery.
 */
int Armor::getMoraleRecovery(const BattleUnit* unit, int externalBonuses) const
{
	return _moraleRecovery.getBonus(unit, externalBonuses);
}

/**
 *  Gets unit Health recovery.
 */
int Armor::getHealthRecovery(const BattleUnit* unit, int externalBonuses) const
{
	return _healthRecovery.getBonus(unit, externalBonuses);
}

/**
 *  Gets unit Mana recovery.
 */
int Armor::getManaRecovery(const BattleUnit* unit, int externalBonuses) const
{
	return _manaRecovery.getBonus(unit, externalBonuses);
}

/**
 *  Gets unit Stun recovery.
 */
int Armor::getStunRegeneration(const BattleUnit* unit, int externalBonuses) const
{
	return _stunRecovery.getBonus(unit, externalBonuses);
}

/**
 * Gets the armor's weight.
 * @return the weight of the armor.
 */
int Armor::getWeight() const
{
	return _weight;
}

/**
 * Gets number of death frames.
 * @return number of death frames.
 */
int Armor::getDeathFrames() const
{
	return _deathFrames;
}

/**
 * Gets if armor uses constant animation.
 * @return if it uses constant animation
 */
bool Armor::getConstantAnimation() const
{
	return _constantAnimation;
}

/**
 * Checks if this armor ignores gender (power suit/flying suit).
 * @return which torso to force on the sprite.
 */
ForcedTorso Armor::getForcedTorso() const
{
	return _forcedTorso;
}

/**
 * What weapons does this armor have built in?
 * this is a vector of strings representing any
 * weapons that may be inherent to this armor.
 * note: unlike "livingWeapon" this is used in ADDITION to
 * any loadout or living weapon item that may be defined.
 * @return list of weapons that are integral to this armor.
 */
const std::vector<const RuleItem*> &Armor::getBuiltInWeapons() const
{
	return _builtInWeapons;
}

/**
 * Gets max view distance at dark in BattleScape.
 * @return The distance to see at dark.
 */
int Armor::getVisibilityAtDark() const
{
	return _visibilityAtDark;
}

/**
* Gets max view distance at day in BattleScape.
* @return The distance to see at day.
*/
int Armor::getVisibilityAtDay() const
{
	return _visibilityAtDay;
}

/**
* Gets info about camouflage at day.
* @return The vision distance modifier.
*/
int Armor::getCamouflageAtDay() const
{
	return _camouflageAtDay;
}

/**
* Gets info about camouflage at dark.
* @return The vision distance modifier.
*/
int Armor::getCamouflageAtDark() const
{
	return _camouflageAtDark;
}

/**
* Gets info about anti camouflage at day.
* @return The vision distance modifier.
*/
int Armor::getAntiCamouflageAtDay() const
{
	return _antiCamouflageAtDay;
}

/**
* Gets info about anti camouflage at dark.
* @return The vision distance modifier.
*/
int Armor::getAntiCamouflageAtDark() const
{
	return _antiCamouflageAtDark;
}

/**
* Gets info about psi vision.
* @return How many tiles can units be sensed even through solid obstacles (e.g. walls).
*/
int Armor::getPsiVision() const
{
	return _psiVision;
}

/**
 * Gets info about psi camouflage.
 * @return psi camo data.
 */
int Armor::getPsiCamouflage() const
{
	return _psiCamouflage;
}

/**
* Gets personal light radius created by solders.
* @return Return light radius.
*/
int Armor::getPersonalLightFriend() const
{
	return _personalLightFriend;
}

/**
* Gets personal light radius created by alien.
* @return Return light radius.
*/
int Armor::getPersonalLightHostile() const
{
	return _personalLightHostile;
}

/**
* Gets personal light radius created by civilian.
* @return Return light radius.
*/
int Armor::getPersonalLightNeutral() const
{
	return _personalLightNeutral;
}

/**
 * Gets how armor react to fear.
 * @param def Default value.
 * @return Can ignored fear?
 */
bool Armor::getFearImmune(bool def) const
{
	return useBoolNullable(_fearImmune, def);
}

/**
 * Gets how armor react to bleeding.
 * @param def Default value.
 * @return Can ignore bleed?
 */
bool Armor::getBleedImmune(bool def) const
{
	return useBoolNullable(_bleedImmune, def);
}

/**
 * Gets how armor react to inflicted pain.
 * @param def
 * @return Can ignore pain?
 */
bool Armor::getPainImmune(bool def) const
{
	return useBoolNullable(_painImmune, def);
}

/**
 * Gets how armor react to zombification.
 * @param def Default value.
 * @return Can't be turn to zombie?
 */
bool Armor::getZombiImmune(bool def) const
{
	return useBoolNullable(_zombiImmune, def);
}

/**
 * Gets whether or not this unit ignores close quarters threats.
 * @param def Default value.
 * @return Ignores CQB check?
 */
bool Armor::getIgnoresMeleeThreat(bool def) const
{
	return useBoolNullable(_ignoresMeleeThreat, def);
}

/**
 * Gets whether or not this unit is a close quarters threat.
 * @param def Default value.
 * @return Creates CQB check for others?
 */
bool Armor::getCreatesMeleeThreat(bool def) const
{
	return useBoolNullable(_createsMeleeThreat, def);
}

/**
 * Gets how much damage (over the maximum HP) is needed to vaporize/disintegrate a unit.
 * @return Percent of require hp.
 */
float Armor::getOverKill() const
{
	return _overKill;
}

/**
 * Gets hair base color group for replacement, if 0 then don't replace colors.
 * @return Color group or 0.
 */
int Armor::getFaceColorGroup() const
{
	return _faceColorGroup;
}

/**
 * Gets hair base color group for replacement, if 0 then don't replace colors.
 * @return Color group or 0.
 */
int Armor::getHairColorGroup() const
{
	return _hairColorGroup;
}

/**
 * Gets utile base color group for replacement, if 0 then don't replace colors.
 * @return Color group or 0.
 */
int Armor::getUtileColorGroup() const
{
	return _utileColorGroup;
}

/**
 * Gets rank base color group for replacement, if 0 then don't replace colors.
 * @return Color group or 0.
 */
int Armor::getRankColorGroup() const
{
	return _rankColorGroup;
}

namespace
{

/**
 * Helper function finding value in vector with fallback if vector is shorter.
 * @param vec Vector with values we try get.
 * @param pos Position in vector that can be greater than size of vector.
 * @return Value in vector.
 */
int findWithFallback(const std::vector<int> &vec, size_t pos)
{
	//if pos == 31 then we test for 31, 15, 7
	//if pos == 36 then we test for 36, 4
	//we stop on p < 8 for compatibility reasons.
	for (int i = 0; i <= RuleSoldier::LookVariantBits; ++i)
	{
		size_t p = (pos & (RuleSoldier::LookTotalMask >> i));
		if (p < vec.size())
		{
			return vec[p];
		}
	}
	return 0;
}

} //namespace

/**
 * Gets new face colors for replacement, if 0 then don't replace colors.
 * @return Color index or 0.
 */
int Armor::getFaceColor(int i) const
{
	return findWithFallback(_faceColor, i);
}

/**
 * Gets new hair colors for replacement, if 0 then don't replace colors.
 * @return Color index or 0.
 */
int Armor::getHairColor(int i) const
{
	return findWithFallback(_hairColor, i);
}

/**
 * Gets new utile colors for replacement, if 0 then don't replace colors.
 * @return Color index or 0.
 */
int Armor::getUtileColor(int i) const
{
	return findWithFallback(_utileColor, i);
}

/**
 * Gets new rank colors for replacement, if 0 then don't replace colors.
 * @return Color index or 0.
 */
int Armor::getRankColor(int i) const
{
	return findWithFallback(_rankColor, i);
}

/**
 * Can this unit's inventory be accessed for any reason?
 * @return if we can access the inventory.
 */
bool Armor::hasInventory() const
{
	return _hasInventory;
}

/**
* Gets the list of units this armor applies to.
* @return The list of unit IDs (empty = applies to all).
*/
const std::vector<const RuleSoldier*> &Armor::getUnitsRaw() const
{
	return _units;
}

/**
 * Check if a soldier can use this armor.
 */
bool Armor::getCanBeUsedBy(const Soldier* soldier) const
{
	if (!_units.empty())
	{
		if (!Collections::sortVectorHave(_units, soldier->getRules()))
		{
			return false;
		}
	}
	if (!_ranks.empty())
	{
		int rankInt = soldier->getRank();
		if (!Collections::sortVectorHave(_ranks, rankInt))
		{
			return false;
		}
	}
	if (_requiresAward)
	{
		if (!soldier->getDiary()->containsCommendation(_requiresAward))
		{
			return false;
		}
	}
	if (_requiresBonus)
	{
		if (!soldier->hasBonus(_requiresBonus))
		{
			return false;
		}
	}
	return true;
}

/**
 * Gets the index of the sprite in the CustomArmorPreview sprite set.
 * @return Sprite index.
 */
const std::vector<int> &Armor::getCustomArmorPreviewIndex() const
{
	return _customArmorPreviewIndex;
}

/**
 * Can you run while wearing this armor?
 * @return True if you are allowed to run.
 */
bool Armor::allowsRunning(bool def) const
{
	return useBoolNullable(_allowsRunning, def);
}

/**
 * Can you strafe while wearing this armor?
 * @return True if you are allowed to strafe.
 */
bool Armor::allowsStrafing(bool def) const
{
	return useBoolNullable(_allowsStrafing, def);
}

/**
 * Can you sneak while wearing this armor?
 * @return True if you are allowed to sneak.
 */
bool Armor::allowsSneaking(bool def) const
{
	return useBoolNullable(_allowsSneaking, def);
}

/**
 * Can you kneel while wearing this armor?
 * @return True if you are allowed to kneel.
 */
bool Armor::allowsKneeling(bool def) const
{
	return useBoolNullable(_allowsKneeling, def);
}

/**
 * Can you move while wearing this armor?
 * @return True if you are allowed to move.
 */
bool Armor::allowsMoving() const
{
	return _allowsMoving;
}

/**
 * Does this armor instantly recover any wounds after the battle?
 * @return True if soldier should not get any recovery time.
 */
bool Armor::getInstantWoundRecovery() const
{
	return _instantWoundRecovery;
}

/**
 * Returns a unit's height at standing in this armor.
 * @return The unit's height.
 */
int Armor::getStandHeight() const
{
	return _standHeight;
}

/**
 * Returns a unit's height at kneeling in this armor.
 * @return The unit's kneeling height.
 */
int Armor::getKneelHeight() const
{
	return _kneelHeight;
}

/**
 * Returns a unit's floating elevation in this armor.
 * @return The unit's floating height.
 */
int Armor::getFloatHeight() const
{
	return _floatHeight;
}


////////////////////////////////////////////////////////////
//					Script binding
////////////////////////////////////////////////////////////

namespace
{

void getTypeScript(const Armor* r, ScriptText& txt)
{
	if (r)
	{
		txt = { r->getType().c_str() };
		return;
	}
	else
	{
		txt = ScriptText::empty;
	}
}

void getArmorValueScript(const Armor *ar, int &ret, int side)
{
	if (ar && 0 <= side && side < SIDE_MAX)
	{
		ret = ar->getArmor((UnitSide)side);
		return;
	}
	ret = 0;
}

std::string debugDisplayScript(const Armor* ar)
{
	if (ar)
	{
		std::string s;
		s += Armor::ScriptName;
		s += "(name: \"";
		s += ar->getType();
		s += "\")";
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
void Armor::ScriptRegister(ScriptParserBase* parser)
{
	Bind<Armor> ar = { parser };

	ar.addCustomConst("SIDE_FRONT", SIDE_FRONT);
	ar.addCustomConst("SIDE_LEFT", SIDE_LEFT);
	ar.addCustomConst("SIDE_RIGHT", SIDE_RIGHT);
	ar.addCustomConst("SIDE_REAR", SIDE_REAR);
	ar.addCustomConst("SIDE_UNDER", SIDE_UNDER);

	ar.add<&getTypeScript>("getType");

	ar.add<&Armor::getDrawingRoutine>("getDrawingRoutine");
	ar.add<&Armor::drawBubbles>("getDrawBubbles");
	ar.add<&Armor::getDeathFrames>("getDeathFrames");
	ar.add<&Armor::getConstantAnimation>("getConstantAnimation");

	ar.add<&Armor::getVisibilityAtDark>("getVisibilityAtDark");
	ar.add<&Armor::getVisibilityAtDay>("getVisibilityAtDay");
	ar.add<&Armor::getCamouflageAtDark>("getCamouflageAtDark");
	ar.add<&Armor::getCamouflageAtDay>("getCamouflageAtDay");
	ar.add<&Armor::getAntiCamouflageAtDark>("getAntiCamouflageAtDark");
	ar.add<&Armor::getAntiCamouflageAtDay>("getAntiCamouflageAtDay");
	ar.add<&Armor::getVisibilityThroughSmoke>("getHeatVision", "getVisibilityThroughSmoke");
	ar.add<&Armor::getVisibilityThroughFire>("getVisibilityThroughFire", "getVisibilityThroughFire");
	ar.add<&Armor::getPersonalLightFriend>("getPersonalLight");
	ar.add<&Armor::getPersonalLightHostile>("getPersonalLightHostile");
	ar.add<&Armor::getPersonalLightNeutral>("getPersonalLightNeutral");
	ar.add<&Armor::getSize>("getSize");

	UnitStats::addGetStatsScript<&Armor::_stats>(ar, "Stats.");

	ar.add<&getArmorValueScript>("getArmor");


	ar.addField<&Armor::_moveCostBase, &ArmorMoveCost::TimePercent>("MoveCost.getBaseTimePercent");
	ar.addField<&Armor::_moveCostBase, &ArmorMoveCost::EnergyPercent>("MoveCost.getBaseEnergyPercent");
	ar.addField<&Armor::_moveCostBaseNormal, &ArmorMoveCost::TimePercent>("MoveCost.getBaseNormalTimePercent");
	ar.addField<&Armor::_moveCostBaseNormal, &ArmorMoveCost::EnergyPercent>("MoveCost.getBaseNormalEnergyPercent");
	ar.addField<&Armor::_moveCostBaseFly, &ArmorMoveCost::TimePercent>("MoveCost.getBaseFlyTimePercent");
	ar.addField<&Armor::_moveCostBaseFly, &ArmorMoveCost::EnergyPercent>("MoveCost.getBaseFlyEnergyPercent");


	ar.addScriptValue<BindBase::OnlyGet, &Armor::_scriptValues>();
	ar.addDebugDisplay<&debugDisplayScript>();
}

}
