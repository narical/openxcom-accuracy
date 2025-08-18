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
#include <algorithm>
#include "LoadYaml.h"
#include "Mod.h"
#include "Armor.h"
#include "Unit.h"
#include "RuleItem.h"
#include "RuleItemCategory.h"
#include "RuleInventory.h"
#include "RuleDamageType.h"
#include "RuleSoldier.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Engine/Exception.h"
#include "../Engine/Collections.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/Surface.h"
#include "../Engine/ScriptBind.h"
#include "../Engine/RNG.h"
#include "../Battlescape/BattlescapeGame.h"

namespace OpenXcom
{

namespace
{

/**
 * Update `attacker` from `weapon_item`.
 */
void UpdateAttacker(BattleActionAttack& attack)
{
	if (attack.weapon_item && !attack.attacker)
	{
		const auto battleType = attack.weapon_item->getRules()->getBattleType();
		if (battleType == BT_PROXIMITYGRENADE || battleType == BT_GRENADE)
		{
			auto owner = attack.weapon_item->getPreviousOwner();
			if (owner)
			{
				attack.attacker = owner;
			}
		}
	}
}

/**
 * Update `damage_item` from `weapon_item`.
 */
void UpdateAmmo(BattleActionAttack& attack)
{
	if (attack.weapon_item && !attack.damage_item)
	{
		const auto battleType = attack.weapon_item->getRules()->getBattleType();
		if (battleType == BT_PROXIMITYGRENADE || battleType == BT_GRENADE || battleType == BT_PSIAMP)
		{
			attack.damage_item = attack.weapon_item;
		}
		else
		{
			attack.damage_item = attack.weapon_item->getAmmoForAction(attack.type);
		}
	}
}

/**
 * Update grenade `damage_item` from `weapon_item`.
 */
void UpdateGrenade(BattleActionAttack& attack)
{
	if (attack.weapon_item && !attack.damage_item)
	{
		const auto battleType = attack.weapon_item->getRules()->getBattleType();
		if (battleType == BT_PROXIMITYGRENADE || battleType == BT_GRENADE)
		{
			attack.damage_item = attack.weapon_item;
		}
	}
}

}

/**
 * Generate ActionAttack before shooting, this means we can get the ammo from the weapon.
 * @param action BattleCost of attack.
 * @return All attack action data.
 */
BattleActionAttack BattleActionAttack::GetBeforeShoot(const BattleActionCost &action)
{
	return GetBeforeShoot(action.type, action.actor, action.weapon, action.skillRules);
}

BattleActionAttack BattleActionAttack::GetBeforeShoot(BattleActionType type, BattleUnit *unit, BattleItem *wepon, const RuleSkill *skill)
{
	auto attack = BattleActionAttack{ type, unit, wepon };
	UpdateAttacker(attack);
	UpdateAmmo(attack);
	attack.skill_rules = skill;
	return attack;
}

/**
 * Generate ActionAttack after shooting, the ammo can be already spent and unloaded from the weapon.
 * @param action BattleCost of attack.
 * @param ammo Ammo used to shoot/attack.
 * @return All attack action data.
 */
BattleActionAttack BattleActionAttack::GetAferShoot(const BattleActionCost &action, BattleItem *ammo)
{
	return GetAferShoot(action.type, action.actor, action.weapon, ammo, action.skillRules);
}

BattleActionAttack BattleActionAttack::GetAferShoot(BattleActionType type, BattleUnit *unit, BattleItem *wepon, BattleItem *ammo, const RuleSkill *skill)
{
	auto attack = BattleActionAttack{ type, unit, wepon };
	UpdateAttacker(attack);
	attack.damage_item = ammo;
	attack.skill_rules = skill;
	UpdateGrenade(attack);
	return attack;
}



const float VexelsToTiles = 0.0625f;
const float TilesToVexels = 16.0f;

/**
 * Creates a blank ruleset for a certain type of item.
 * @param type String defining the type.
 */
RuleItem::RuleItem(const std::string &type, int listOrder) :
	_type(type), _name(type), _vehicleUnit(nullptr), _vehicleFixedAmmoSlot(0), _size(0.0),
	_monthlyBuyLimit(0), _costBuy(0), _costSell(0), _transferTime(24), _weight(3), _throwRange(200), _underwaterThrowRange(200),
	_throwDropoffRange(99), _underwaterThrowDropoffRange(99), _throwDropoff(5),
	_bigSprite(-1), _floorSprite(-1), _handSprite(120), _bulletSprite(-1), _specialIconSprite(-1),
	_hitAnimation(0), _hitAnimFrames(-1), _hitMissAnimation(-1), _hitMissAnimFrames(-1),
	_meleeAnimation(0), _meleeAnimFrames(-1), _meleeMissAnimation(-1), _meleeMissAnimFrames(-1),
	_psiAnimation(-1), _psiAnimFrames(-1), _psiMissAnimation(-1), _psiMissAnimFrames(-1),
	_power(0), _powerForAnimation(0), _hidePower(false), _ignoreAmmoPower(false), _powerRangeReduction(0), _powerRangeThreshold(0),
	_damageTypeSet(false), _meleeTypeSet(false),
	_accuracyUse(0), _accuracyMind(0), _accuracyPanic(20), _accuracyThrow(100), _accuracyCloseQuarters(-1),
	_noLOSAccuracyPenalty(-1),
	_explodeInventory(-1),
	_costUse(25), _costMind({}, {}), _costPanic({}, {}), _costThrow(25), _costPrime(50), _costUnprime(25),
	_clipSize(0), _specialChance(100), _tuLoad{ }, _tuUnload{ },
	_battleType(BT_NONE), _fuseType(BFT_NONE), _fuseTriggerEvents{ }, _hiddenOnMinimap(false),
	_medikitActionName("STR_USE_MEDI_KIT"), _psiAttackName(), _primeActionName("STR_PRIME_GRENADE"), _unprimeActionName(), _primeActionMessage("STR_GRENADE_IS_ACTIVATED"), _unprimeActionMessage("STR_GRENADE_IS_DEACTIVATED"),
	_twoHanded(false), _blockBothHands(false), _fixedWeapon(false), _fixedWeaponShow(false), _isConsumable(false), _isFireExtinguisher(false),
	_specialUseEmptyHand(false), _specialUseEmptyHandShow(false),
	_defaultInvSlotX(0), _defaultInvSlotY(0), _waypoints(0), _invWidth(1), _invHeight(1),
	_painKiller(0), _heal(0), _stimulant(0), _medikitType(BMT_NORMAL), _medikitTargetSelf(false), _medikitTargetImmune(false), _medikitTargetMatrix(63),
	_woundRecovery(0), _healthRecovery(0), _stunRecovery(0), _energyRecovery(0), _manaRecovery(0), _moraleRecovery(0), _painKillerRecovery(1.0f),
	_recoveryPoints(0), _armor(20), _turretType(-1),
	_aiUseDelay(-1), _aiMeleeHitCount(25),
	_recover(true), _recoverCorpse(true), _ignoreInBaseDefense(false), _ignoreInCraftEquip(true), _liveAlien(false),
	_liveAlienPrisonType(0), _attraction(0), _flatUse(0, 1), _flatThrow(0, 1), _flatPrime(0, 1), _flatUnprime(0, 1), _arcingShot(false),
	_experienceTrainingMode(ETM_DEFAULT), _manaExperience(0), _loadOrder(0), _listOrder(listOrder),
	_maxRange(200), _minRange(0), _dropoff(2), _bulletSpeed(0), _explosionSpeed(0), _shotgunPellets(0), _shotgunBehaviorType(0), _shotgunSpread(100), _shotgunChoke(100),
	_spawnUnitFaction(FACTION_NONE), _zombieUnitFaction(FACTION_HOSTILE),
	_targetMatrix(7), _convertToCivilian(false),
	_LOSRequired(false), _underwaterOnly(false), _landOnly(false), _psiReqiured(false), _manaRequired(false),
	_meleePower(0), _specialType(-1), _vaporColor(-1), _vaporDensity(0), _vaporProbability(15),
	_vaporColorSurface(-1), _vaporDensitySurface(0), _vaporProbabilitySurface(15),
	_kneelBonus(-1), _oneHandedPenalty(-1),
	_monthlySalary(0), _monthlyMaintenance(0),
	_sprayWaypoints(0)
{
	_accuracyMulti.setFiring();
	_meleeMulti.setMelee();
	_throwMulti.setThrowing();
	_closeQuartersMulti.setCloseQuarters();

	for (auto& load : _tuLoad)
	{
		load = 15;
	}
	for (auto& unload : _tuUnload)
	{
		unload = 8;
	}

	_confAimed.range = 200;
	_confSnap.range = 15;
	_confAuto.range = 7;

	_confAimed.cost = { 0 };
	_confSnap.cost = { 0, {} };
	_confAuto.cost = { 0, {} };
	_confMelee.cost = { 0 };

	_confAimed.flat = { {}, {} };
	_confSnap.flat = { {}, {} };
	_confAuto.flat = { {}, {} };
	_confMelee.flat = { {}, {} };

	_confAimed.name = "STR_AIMED_SHOT";
	_confSnap.name = "STR_SNAP_SHOT";
	_confAuto.name = "STR_AUTO_SHOT";

	_confAuto.shots = 3;

	_customItemPreviewIndex.push_back(Mod::NO_SURFACE);
}

/**
 *
 */
RuleItem::~RuleItem()
{
}

/**
 * Load ammo slot with checking correct range.
 * @param result
 * @param node
 * @param parentName
 */
void RuleItem::loadAmmoSlotChecked(int& result, const YAML::YamlNodeReader& reader, const std::string& parentName)
{
	if (reader)
	{
		int s = reader.readVal(result);
		if (s < AmmoSlotSelfUse || s >= AmmoSlotMax)
		{
			Log(LOG_ERROR) << "ammoSlot outside of allowed range in '" << parentName << "'";
		}
		else
		{
			result = s;
		}
	}
}

/**
 * Load RuleItemAction from yaml.
 * @param a Item use config.
 * @param node YAML node.
 * @param name Name of action type.
 */
void RuleItem::loadConfAction(RuleItemAction& a, const YAML::YamlNodeReader& reader, const std::string& name) const
{
	if (const auto& conf = reader[ryml::to_csubstr("conf" + name)])
	{

		conf.tryRead("shots", a.shots);
		conf.tryRead("spendPerShot", a.spendPerShot);
		conf.tryRead("followProjectiles", a.followProjectiles);
		conf.tryRead("name", a.name);
		conf.tryRead("shortName", a.shortName);
		loadAmmoSlotChecked(a.ammoSlot, conf["ammoSlot"], _name);
		loadIntNullable(a.ammoZombieUnitChanceOverride, conf["ammoZombieUnitChanceOverride"]);
		loadIntNullable(a.ammoSpawnUnitChanceOverride, conf["ammoSpawnUnitChanceOverride"]);
		loadIntNullable(a.ammoSpawnItemChanceOverride, conf["ammoSpawnItemChanceOverride"]);
		conf.tryRead("arcing", a.arcing);
	}
}

/**
 * Load RuleItemFuseTrigger from yaml.
 */
void RuleItem::loadConfFuse(RuleItemFuseTrigger& a, const YAML::YamlNodeReader& reader, const std::string& name) const
{
	if (const auto& conf = reader[ryml::to_csubstr(name)])
	{
		loadBool(a.defaultBehavior, conf["defaultBehavior"]);
		loadBool(a.throwTrigger, conf["throwTrigger"]);
		loadBool(a.throwExplode, conf["throwExplode"]);
		loadBool(a.proximityTrigger, conf["proximityTrigger"]);
		loadBool(a.proximityExplode, conf["proximityExplode"]);
	}
}

/**
* Updates item categories based on replacement rules.
* @param replacementRules The list replacement rules.
*/
void RuleItem::updateCategories(std::map<std::string, std::string> *replacementRules)
{
	for (const auto& pair : *replacementRules)
	{
		std::replace(_categories.begin(), _categories.end(), pair.first, pair.second);
	}
}

/**
 * Loads the item from a YAML file.
 * @param node YAML node.
 * @param mod Mod for the item.
 * @param listOrder The list weight for this item.
 */
void RuleItem::load(const YAML::YamlNodeReader& node, Mod *mod, const ModScript& parsers)
{
	const auto& reader = node.useIndex();
	if (const auto& parent = reader["refNode"])
	{
		load(parent, mod, parsers);
	}

	reader.tryRead("ufopediaType", _ufopediaType);
	reader.tryRead("name", _name);
	reader.tryRead("nameAsAmmo", _nameAsAmmo);

	//requires
	reader.tryRead("requiresBuyCountry", _requiresBuyCountry);
	mod->loadUnorderedNames(_type, _requiresName, reader["requires"]);
	mod->loadUnorderedNames(_type, _requiresBuyName, reader["requiresBuy"]);
	mod->loadBaseFunction(_type, _requiresBuyBaseFunc, reader["requiresBuyBaseFunc"]);


	mod->loadUnorderedNamesToInt(_type, _recoveryDividers, reader["recoveryDividers"]);
	reader.tryRead("recoveryTransformations", _recoveryTransformationsName);
	mod->loadUnorderedNames(_type, _categories, reader["categories"]);

	reader.tryRead("vehicleFixedAmmoSlot", _vehicleFixedAmmoSlot);
	reader.tryRead("size", _size);
	reader.tryRead("monthlyBuyLimit", _monthlyBuyLimit);
	reader.tryRead("costBuy", _costBuy);
	reader.tryRead("costSell", _costSell);
	reader.tryRead("transferTime", _transferTime);
	reader.tryRead("weight", _weight);
	reader.tryRead("throwRange", _throwRange);
	reader.tryRead("underwaterThrowRange", _underwaterThrowRange);
	reader.tryRead("throwDropoffRange", _throwDropoffRange);
	reader.tryRead("underwaterThrowDropoffRange", _underwaterThrowDropoffRange);
	reader.tryRead("throwDropoff", _throwDropoff);

	mod->loadSpriteOffset(_type, _bigSprite, reader["bigSprite"], "BIGOBS.PCK");
	mod->loadSpriteOffset(_type, _floorSprite, reader["floorSprite"], "FLOOROB.PCK");
	mod->loadSpriteOffset(_type, _handSprite, reader["handSprite"], "HANDOB.PCK");
	// Projectiles: 0-384 entries ((105*33) / (3*3)) (35 sprites per projectile(0-34), 11 projectiles (0-10))
	mod->loadSpriteOffset(_type, _bulletSprite, reader["bulletSprite"], "Projectiles", 35);
	mod->loadSpriteOffset(_type, _specialIconSprite, reader["specialIconSprite"], "SPICONS.DAT");

	mod->loadSoundOffset(_type, _reloadSound, reader["reloadSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _primeSound, reader["primeSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _unprimeSound, reader["unprimeSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _fireSound, reader["fireSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _hitSound, reader["hitSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _hitMissSound, reader["hitMissSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _meleeSound, reader["meleeSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _meleeHitSound, reader["meleeHitSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _meleeMissSound, reader["meleeMissSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _psiSound, reader["psiSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _psiMissSound, reader["psiMissSound"], "BATTLE.CAT");
	mod->loadSoundOffset(_type, _explosionHitSound, reader["explosionHitSound"], "BATTLE.CAT");

	mod->loadSpriteOffset(_type, _hitAnimation, reader["hitAnimation"], "SMOKE.PCK");
	mod->loadSpriteOffset(_type, _hitMissAnimation, reader["hitMissAnimation"], "SMOKE.PCK");
	mod->loadSpriteOffset(_type, _meleeAnimation, reader["meleeAnimation"], "HIT.PCK");
	mod->loadSpriteOffset(_type, _meleeMissAnimation, reader["meleeMissAnimation"], "HIT.PCK");
	mod->loadSpriteOffset(_type, _psiAnimation, reader["psiAnimation"], "HIT.PCK");
	mod->loadSpriteOffset(_type, _psiMissAnimation, reader["psiMissAnimation"], "HIT.PCK");

	reader.tryRead("hitAnimFrames", _hitAnimFrames);
	reader.tryRead("hitMissAnimFrames", _hitMissAnimFrames);
	reader.tryRead("meleeAnimFrames", _meleeAnimFrames);
	reader.tryRead("meleeMissAnimFrames", _meleeMissAnimFrames);
	reader.tryRead("psiAnimFrames", _psiAnimFrames);
	reader.tryRead("psiMissAnimFrames", _psiMissAnimFrames);

	if (reader["battleType"])
	{
		reader.tryRead("battleType", _battleType);
		_ignoreInCraftEquip = !isUsefulBattlescapeItem();

		if (_battleType == BT_PSIAMP)
		{
			_psiReqiured = true;
			_dropoff = 1;
			_confAimed.range = 0;
			_accuracyMulti.setPsiAttack();
			_targetMatrix = 6; // only hostile and neutral by default
		}
		else
		{
			_psiReqiured = false;
		}

		if (_battleType == BT_PROXIMITYGRENADE)
		{
			_fuseType = BFT_INSTANT;
		}
		else if (_battleType == BT_GRENADE)
		{
			_fuseType = BFT_SET;
		}
		else
		{
			_fuseType = BFT_NONE;
		}

		if (_battleType == BT_MELEE)
		{
			_confMelee.ammoSlot = 0;
		}
		else
		{
			_confMelee.ammoSlot = RuleItem::AmmoSlotSelfUse;
		}

		if (_battleType == BT_CORPSE)
		{
			//compatibility hack for corpse explosion, that didn't have defined damage type
			_damageType = *mod->getDamageType(DT_HE);
			_damageTypeSet = true;
		}
		_meleeType = *mod->getDamageType(DT_MELEE);
		_meleeTypeSet = true;
	}

	if (const auto& type = reader["damageType"])
	{
		//load predefined damage type
		_damageType = *mod->getDamageType((ItemDamageType)type.readVal<int>());
		_damageTypeSet = true;
	}
	reader.tryRead("blastRadius", _damageType.FixRadius);
	if (const auto& alter = reader["damageAlter"])
	{
		_damageType.load(alter);
	}

	if (const auto& type = reader["meleeType"])
	{
		//load predefined damage type
		_meleeType = *mod->getDamageType((ItemDamageType)type.readVal<int>());
		_meleeTypeSet = true;
	}
	if (const auto& alter = reader["meleeAlter"])
	{
		_meleeType.load(alter);
	}

	if (const auto& skill = reader["skillApplied"])
	{
		if (skill.readVal(false))
		{
			_meleeMulti.setMelee();
		}
		else
		{
			_meleeMulti.setFlatHundred();
			_meleeMulti.setModded(true); // vanilla default = true
		}
	}
	if (reader["strengthApplied"].readVal(false))
	{
		_damageBonus.setStrength();
		_damageBonus.setModded(true); // vanilla default = false
	}

	reader.tryRead("power", _power);
	reader.tryRead("powerForAnimation", _powerForAnimation);
	reader.tryRead("hidePower", _hidePower);
	reader.tryRead("ignoreAmmoPower", _ignoreAmmoPower);
	reader.tryRead("medikitActionName", _medikitActionName);
	reader.tryRead("psiAttackName", _psiAttackName);
	reader.tryRead("primeActionName", _primeActionName);
	reader.tryRead("primeActionMessage", _primeActionMessage);
	reader.tryRead("unprimeActionName", _unprimeActionName);
	reader.tryRead("unprimeActionMessage", _unprimeActionMessage);
	reader.tryRead("sellActionMessage", _sellActionMessage);
	reader.tryRead("fuseType", _fuseType);
	reader.tryRead("hiddenOnMinimap", _hiddenOnMinimap);
	reader.tryRead("clipSize", _clipSize);

	loadConfFuse(_fuseTriggerEvents, reader, "fuseTriggerEvents");

	reader.tryRead("accuracyAimed", _confAimed.accuracy);
	reader.tryRead("accuracyAuto", _confAuto.accuracy);
	reader.tryRead("accuracySnap", _confSnap.accuracy);
	reader.tryRead("accuracyMelee", _confMelee.accuracy);
	reader.tryRead("accuracyUse", _accuracyUse);
	reader.tryRead("accuracyMindControl", _accuracyMind);
	reader.tryRead("accuracyPanic", _accuracyPanic);
	reader.tryRead("accuracyThrow", _accuracyThrow);
	reader.tryRead("accuracyCloseQuarters", _accuracyCloseQuarters);
	reader.tryRead("noLOSAccuracyPenalty", _noLOSAccuracyPenalty);
	if (reader["isExplodingInHands"])
	{
		// FIXME: backwards-compatibility only, remove in 2026
		bool tmpBool = false;
		reader.tryRead("isExplodingInHands", tmpBool);
		_explodeInventory = tmpBool ? 2 : 0;
	}
	reader.tryRead("explodeInventory", _explodeInventory);

	_confAimed.cost.loadCost(reader, "Aimed");
	_confAuto.cost.loadCost(reader, "Auto");
	_confSnap.cost.loadCost(reader, "Snap");
	_confMelee.cost.loadCost(reader, "Melee");
	_costUse.loadCost(reader, "Use");
	_costMind.loadCost(reader, "MindControl");
	_costPanic.loadCost(reader, "Panic");
	_costThrow.loadCost(reader, "Throw");
	_costPrime.loadCost(reader, "Prime");
	_costUnprime.loadCost(reader, "Unprime");

	reader.tryRead("flatRate", _flatUse.Time);

	_confAimed.flat.loadFlat(reader, "Aimed");
	_confAuto.flat.loadFlat(reader, "Auto");
	_confSnap.flat.loadFlat(reader, "Snap");
	_confMelee.flat.loadFlat(reader, "Melee");
	_flatUse.loadFlat(reader, "Use");
	_flatThrow.loadFlat(reader, "Throw");
	_flatPrime.loadFlat(reader, "Prime");
	_flatUnprime.loadFlat(reader, "Unprime");

	loadConfAction(_confAimed, reader, "Aimed");
	loadConfAction(_confAuto, reader, "Auto");
	loadConfAction(_confSnap, reader, "Snap");
	loadConfAction(_confMelee, reader, "Melee");

	auto loadAmmoConf = [&](int offset, const YAML::YamlNodeReader& n)
	{
		if (n)
		{
			mod->loadUnorderedNames(_type, _compatibleAmmoNames[offset], n["compatibleAmmo"]);
			n.tryRead("tuLoad", _tuLoad[offset]);
			n.tryRead("tuUnload", _tuUnload[offset]);
		}
	};

	loadAmmoConf(0, reader);
	if (const auto& nodeAmmo = reader["ammo"])
	{
		for (int slot = 0; slot < AmmoSlotMax; ++slot)
		{
			loadAmmoConf(slot, nodeAmmo[ryml::to_csubstr(std::to_string(slot))]);
		}
	}

	reader.tryRead("specialChance", _specialChance);
	reader.tryRead("twoHanded", _twoHanded);
	reader.tryRead("blockBothHands", _blockBothHands);
	reader.tryRead("waypoints", _waypoints);
	reader.tryRead("fixedWeapon", _fixedWeapon);
	reader.tryRead("fixedWeaponShow", _fixedWeaponShow);
	reader["inventoryMoveCost"]["basePercent"].tryReadVal(_inventoryMoveCostPercent);
	mod->loadNameNull(_type, _defaultInventorySlotName, reader["defaultInventorySlot"]);
	reader.tryRead("defaultInvSlotX", _defaultInvSlotX);
	reader.tryRead("defaultInvSlotY", _defaultInvSlotY);
	mod->loadUnorderedNames(_type, _supportedInventorySectionsNames, reader["supportedInventorySections"]);
	reader.tryRead("isConsumable", _isConsumable);
	reader.tryRead("isFireExtinguisher", _isFireExtinguisher);
	reader.tryRead("specialUseEmptyHand", _specialUseEmptyHand);
	reader.tryRead("specialUseEmptyHandShow", _specialUseEmptyHandShow);
	reader.tryRead("invWidth", _invWidth);
	reader.tryRead("invHeight", _invHeight);

	reader.tryRead("painKiller", _painKiller);
	reader.tryRead("heal", _heal);
	reader.tryRead("stimulant", _stimulant);
	reader.tryRead("woundRecovery", _woundRecovery);
	reader.tryRead("healthRecovery", _healthRecovery);
	reader.tryRead("stunRecovery", _stunRecovery);
	reader.tryRead("energyRecovery", _energyRecovery);
	reader.tryRead("manaRecovery", _manaRecovery);
	reader.tryRead("moraleRecovery", _moraleRecovery);
	reader.tryRead("painKillerRecovery", _painKillerRecovery);
	reader.tryRead("medikitType", _medikitType);
	reader.tryRead("medikitTargetSelf", _medikitTargetSelf);
	reader.tryRead("medikitTargetImmune", _medikitTargetImmune);
	reader.tryRead("medikitTargetMatrix", _medikitTargetMatrix);
	reader.tryRead("medikitBackground", _medikitBackground);

	reader.tryRead("recoveryPoints", _recoveryPoints);
	reader.tryRead("armor", _armor);
	reader.tryRead("turretType", _turretType);
	if (const auto& nodeAI = reader["ai"])
	{
		nodeAI.tryRead("useDelay", _aiUseDelay);
		nodeAI.tryRead("meleeHitCount", _aiMeleeHitCount);
	}
	reader.tryRead("recover", _recover);
	reader.tryRead("recoverCorpse", _recoverCorpse);
	reader.tryRead("ignoreInBaseDefense", _ignoreInBaseDefense);
	reader.tryRead("ignoreInCraftEquip", _ignoreInCraftEquip);
	reader.tryRead("liveAlien", _liveAlien);
	reader.tryRead("prisonType", _liveAlienPrisonType);
	reader.tryRead("attraction", _attraction);
	reader.tryRead("arcingShot", _arcingShot);
	reader.tryRead("experienceTrainingMode", _experienceTrainingMode);
	reader.tryRead("manaExperience", _manaExperience);
	reader.tryRead("loadOrder", _loadOrder);
	reader.tryRead("listOrder", _listOrder);
	reader.tryRead("maxRange", _maxRange);
	reader.tryRead("aimRange", _confAimed.range);
	reader.tryRead("autoRange", _confAuto.range);
	reader.tryRead("snapRange", _confSnap.range);
	reader.tryRead("minRange", _minRange);
	reader.tryRead("dropoff", _dropoff);
	reader.tryRead("bulletSpeed", _bulletSpeed);
	reader.tryRead("explosionSpeed", _explosionSpeed);
	reader.tryRead("autoShots", _confAuto.shots);
	reader.tryRead("shotgunPellets", _shotgunPellets);
	reader.tryRead("shotgunBehavior", _shotgunBehaviorType);
	reader.tryRead("shotgunSpread", _shotgunSpread);
	reader.tryRead("shotgunChoke", _shotgunChoke);

	mod->loadUnorderedNamesToNames(_type, _zombieUnitByArmorMale, reader["zombieUnitByArmorMale"]);
	mod->loadUnorderedNamesToNames(_type, _zombieUnitByArmorFemale, reader["zombieUnitByArmorFemale"]);
	mod->loadUnorderedNamesToNames(_type, _zombieUnitByType, reader["zombieUnitByType"]);
	mod->loadNameNull(_type, _zombieUnit, reader["zombieUnit"]);
	mod->loadNameNull(_type, _spawnUnitName, reader["spawnUnit"]);
	mod->loadNameNull(_type, _spawnItemName, reader["spawnItem"]);
	reader.tryRead("spawnUnitFaction", _spawnUnitFaction);
	reader.tryRead("zombieUnitFaction", _zombieUnitFaction);
	loadIntNullable(_spawnUnitChance, reader["spawnUnitChance"]);
	loadIntNullable(_zombieUnitChance, reader["zombieUnitChance"]);
	loadIntNullable(_spawnItemChance, reader["spawnItemChance"]);

	// TODO: just backwards-compatibility, remove in 2022, update ruleset validator too
	reader.tryRead("psiTargetMatrix", _targetMatrix);

	reader.tryRead("targetMatrix", _targetMatrix);
	reader.tryRead("convertToCivilian", _convertToCivilian);
	reader.tryRead("LOSRequired", _LOSRequired);
	reader.tryRead("meleePower", _meleePower);
	reader.tryRead("underwaterOnly", _underwaterOnly);
	reader.tryRead("landOnly", _landOnly);
	reader.tryRead("specialType", _specialType);

	mod->loadTransparencyOffset(_type, _vaporColor, reader["vaporColor"]);
	reader.tryRead("vaporDensity", _vaporDensity);
	reader.tryRead("vaporProbability", _vaporProbability);

	mod->loadTransparencyOffset(_type, _vaporColorSurface, reader["vaporColorSurface"]);
	reader.tryRead("vaporDensitySurface", _vaporDensitySurface);
	reader.tryRead("vaporProbabilitySurface", _vaporProbabilitySurface);

	mod->loadSpriteOffset(_type, _customItemPreviewIndex, reader["customItemPreviewIndex"], "CustomItemPreviews");
	reader.tryRead("kneelBonus", _kneelBonus);
	reader.tryRead("oneHandedPenalty", _oneHandedPenalty);
	reader.tryRead("monthlySalary", _monthlySalary);
	reader.tryRead("monthlyMaintenance", _monthlyMaintenance);
	reader.tryRead("sprayWaypoints", _sprayWaypoints);

	_damageBonus.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::DamageBonusStatBonus>());
	_meleeBonus.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::MeleeBonusStatBonus>());
	_accuracyMulti.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::AccuracyMultiplierStatBonus>());
	_meleeMulti.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::MeleeMultiplierStatBonus>());
	_throwMulti.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::ThrowMultiplierStatBonus>());
	_closeQuartersMulti.load(_type, reader, parsers.bonusStatsScripts.get<ModScript::CloseQuarterMultiplierStatBonus>());

	reader.tryRead("powerRangeReduction", _powerRangeReduction);
	reader.tryRead("powerRangeThreshold", _powerRangeThreshold);

	reader.tryRead("psiRequired", _psiReqiured);
	reader.tryRead("manaRequired", _manaRequired);
	_scriptValues.load(reader, parsers.getShared());

	_battleItemScripts.load(_type, reader, parsers.battleItemScripts);
}

/**
 * Cross link with other Rules.
 */
void RuleItem::afterLoad(const Mod* mod)
{
	if ((_battleType == BT_MELEE || _battleType == BT_FIREARM) && _clipSize == 0)
	{
		for (RuleItemAction* conf : { &_confAimed, &_confAuto, &_confSnap, &_confMelee, })
		{
			if (conf->ammoSlot != RuleItem::AmmoSlotSelfUse && _compatibleAmmoNames[conf->ammoSlot].empty())
			{
				throw Exception("Weapon " + _type + " has clip size 0 and no ammo defined. Please use 'clipSize: -1' for unlimited ammo, or allocate a compatibleAmmo item.");
			}
		}
	}

	// these are good defaults for vanilla
	if (_loadOrder <= 0)
	{
		_loadOrder = _listOrder;
	}

	mod->verifySpriteOffset(_type, _bigSprite, "BIGOBS.PCK");
	mod->verifySpriteOffset(_type, _floorSprite, "FLOOROB.PCK");
	mod->verifySpriteOffset(_type, _handSprite, "HANDOB.PCK");
	// Projectiles: this will check only if first one is correct
	if (!isWaterOnly()) mod->verifySpriteOffset(_type, _bulletSprite, "Projectiles");
	if (isWaterOnly()) mod->verifySpriteOffset(_type, _bulletSprite, "UnderwaterProjectiles");
	mod->verifySpriteOffset(_type, _specialIconSprite, "SPICONS.DAT");

	mod->verifySoundOffset(_type, _reloadSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _fireSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _hitSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _hitMissSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _meleeSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _meleeHitSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _meleeMissSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _psiSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _psiMissSound, "BATTLE.CAT");
	mod->verifySoundOffset(_type, _explosionHitSound, "BATTLE.CAT");

	mod->verifySpriteOffset(_type, _hitAnimation, _damageType.FixRadius ? "X1.PCK" : "SMOKE.PCK");
	mod->verifySpriteOffset(_type, _hitMissAnimation, _damageType.FixRadius ? "X1.PCK" : "SMOKE.PCK");
	mod->verifySpriteOffset(_type, _meleeAnimation, "HIT.PCK");
	mod->verifySpriteOffset(_type, _meleeMissAnimation, "HIT.PCK");
	mod->verifySpriteOffset(_type, _psiAnimation, "HIT.PCK");
	mod->verifySpriteOffset(_type, _psiMissAnimation, "HIT.PCK");

	mod->verifySpriteOffset(_type, _customItemPreviewIndex, "CustomItemPreviews");


	_requires = mod->getResearch(_requiresName);
	_requiresBuy = mod->getResearch(_requiresBuyName);
	// fixedWeapons can mean vehicle
	if (_fixedWeapon)
	{
		_vehicleUnit = mod->getUnit(_type);
	}
	mod->linkRule(_spawnUnit, _spawnUnitName);
	mod->linkRule(_spawnItem, _spawnItemName);

	for (auto& pair : _recoveryTransformationsName)
	{
		auto item = mod->getItem(pair.first, true);
		if (!item->isAlien())
		{
			if (!pair.second.empty())
			{
				_recoveryTransformations[item] = pair.second;
			}
			else
			{
				throw Exception("Right-hand value of recovery transformations definition cannot be empty!");
			}
		}
		else
		{
			throw Exception("Sorry modders, cannot recover live aliens from random inorganic junk '" + pair.first + "'!");
		}
	}

	mod->linkRule(_defaultInventorySlot, _defaultInventorySlotName);
	if (_supportedInventorySectionsNames.size())
	{
		mod->linkRule(_supportedInventorySections, _supportedInventorySectionsNames);
		Collections::sortVector(_supportedInventorySections);
	}
	for (int i = 0; i < AmmoSlotMax; ++i)
	{
		mod->linkRule(_compatibleAmmo[i], _compatibleAmmoNames[i]);
		for (auto a : _compatibleAmmo[i])
		{
			if (_compatibleAmmoSlots.count(a) == 0)
			{
				_compatibleAmmoSlots.insert(std::make_pair(a, i));
			}
		}
	}
	if (_vehicleUnit)
	{
		if (_vehicleFixedAmmoSlot != 0 && _vehicleFixedAmmoSlot != -1)
		{
			throw Exception("Vehicle primary weapon fixed ammo slot can be only 0 or -1.");
		}
		if (_vehicleFixedAmmoSlot > -1)
		{
			if (_compatibleAmmo[_vehicleFixedAmmoSlot].size() > 1)
			{
				throw Exception("Vehicle weapons support only one ammo type");
			}
			if (_compatibleAmmo[_vehicleFixedAmmoSlot].size() == 1)
			{
				auto* ammo = _compatibleAmmo[_vehicleFixedAmmoSlot].front();
				if (ammo->getClipSize() > 0 && getClipSize() > 0)
				{
					if (getClipSize() % ammo->getClipSize())
					{
						throw Exception("Vehicle weapon clip size is not a multiple of '" + ammo->getType() + "' clip size");
					}
				}
			}
		}
	}

	//remove not needed data
	Collections::removeAll(_requiresName);
	Collections::removeAll(_requiresBuyName);
	Collections::removeAll(_recoveryTransformationsName);
	Collections::removeAll(_compatibleAmmoNames);
}

/**
 * Gets the custom name of the Ufopedia article related to this item.
 * @return The ufopedia article name.
 */
const std::string& RuleItem::getUfopediaType() const
{
	if (!_ufopediaType.empty())
		return _ufopediaType;

	return _type;
}

/**
 * Gets the item type. Each item has a unique type.
 * @return The item's type.
 */
const std::string &RuleItem::getType() const
{
	return _type;
}

/**
 * Gets the language string that names
 * this item. This is not necessarily unique.
 * @return  The item's name.
 */
const std::string &RuleItem::getName() const
{
	return _name;
}

/**
 * Gets name id to use when displaying in loaded weapon.
 * @return Translation StringId.
 */
const std::string &RuleItem::getNameAsAmmo() const
{
	return _nameAsAmmo;
}

/**
 * Gets the list of research required to
 * use this item.
 * @return The list of research IDs.
 */
const std::vector<const RuleResearch *> &RuleItem::getRequirements() const
{
	return _requires;
}

/**
 * Gets the list of research required to
 * buy this item from market.
 * @return The list of research IDs.
 */
const std::vector<const RuleResearch *> &RuleItem::getBuyRequirements() const
{
	return _requiresBuy;
}

/**
 * Gets the dividers used for recovery of special items (specialType > 1).
 * @return The list of recovery divider rules
 */
const std::map<std::string, int> &RuleItem::getRecoveryDividers() const
{
	return _recoveryDividers;
}

/**
 * Gets the item(s) to be recovered instead of this item.
 * @return The list of recovery transformation rules
 */
const std::map<const RuleItem*, std::vector<int> > &RuleItem::getRecoveryTransformations() const
{
	return _recoveryTransformations;
}

/**
* Gets the list of categories
* this item belongs to.
* @return The list of category IDs.
*/
const std::vector<std::string> &RuleItem::getCategories() const
{
	return _categories;
}

/**
* Checks if the item belongs to a category.
* @param category Category name.
* @return True if item belongs to the category, False otherwise.
*/
bool RuleItem::belongsToCategory(const std::string &category) const
{
	return std::find(_categories.begin(), _categories.end(), category) != _categories.end();
}

/**
 * Returns the first item category that has a non-empty invOrder, if it exists.
 */
const RuleItemCategory* RuleItem::getFirstCategoryWithInvOrder(const Mod* mod) const
{
	for (auto& catName : _categories)
	{
		auto* cat = mod->getItemCategory(catName, false);
		if (cat && !cat->getInvOrder().empty())
		{
			return cat;
		}
	}
	return nullptr;
}

/**
 * Gets unit rule if the item is vehicle weapon.
 */
Unit* RuleItem::getVehicleUnit() const
{
	return _vehicleUnit;
}

/**
 * Gets the amount of space this item
 * takes up in a storage facility.
 * @return The storage size.
 */
double RuleItem::getSize() const
{
	return _size;
}

/**
 * Gets the amount of money this item
 * costs to purchase (0 if not purchasable).
 * @return The buy cost.
 */
int RuleItem::getBuyCost() const
{
	return _costBuy;
}

/**
 * Gets the item's purchase cost.
 * @param base Current base from where item is bought
 * @param save Game
 * @return Current cost
 */
int RuleItem::getBuyCostAdjusted(const Base* base, const SavedGame* save) const
{
	(void)base; //TODO: not exposed to scripts yet

	int buyPriceCoefficient = save->getBuyPriceCoefficient();
	int cost = getBuyCost();
	int adjusted = ((int64_t)cost) * buyPriceCoefficient / 100;

	adjusted = ModScript::scriptFunc2<ModScript::BuyCostItem>(this, adjusted, cost, this, save, buyPriceCoefficient);

	return adjusted;
}

/**
 * Gets the amount of money this item
 * is worth to sell.
 * @return The sell cost.
 */
int RuleItem::getSellCost() const
{
	return _costSell;
}

/**
 * Gets the item's sale cost.
 * @param base Current base from where item is sold
 * @param save Game
 * @return Current cost
 */
int RuleItem::getSellCostAdjusted(const Base* base, const SavedGame* save) const
{
	(void)base; //TODO: not exposed to scripts yet

	int sellPriceCoefficient = save->getSellPriceCoefficient();
	int cost = getSellCost();
	int adjusted = ((int64_t)cost) * sellPriceCoefficient / 100;

	adjusted = ModScript::scriptFunc2<ModScript::SellCostItem>(this, adjusted, cost, this, save, sellPriceCoefficient);

	return adjusted;
}

/**
 * Gets the amount of time this item
 * takes to arrive at a base.
 * @return The time in hours.
 */
int RuleItem::getTransferTime() const
{
	return _transferTime;
}

/**
 * Gets the weight of the item.
 * @return The weight in strength units.
 */
int RuleItem::getWeight() const
{
	return _weight;
}

/**
 * Gets the reference in BIGOBS.PCK for use in inventory.
 * @return The sprite reference.
 */
int RuleItem::getBigSprite() const
{
	return _bigSprite;
}

/**
 * Gets the reference in FLOOROB.PCK for use in battlescape.
 * @return The sprite reference.
 */
int RuleItem::getFloorSprite() const
{
	return _floorSprite;
}

/**
 * Gets the reference in HANDOB.PCK for use in inventory.
 * @return The sprite reference.
 */
int RuleItem::getHandSprite() const
{
	return _handSprite;
}

/**
 * Gets the reference in SPICONS.DAT for use in battlescape.
 * @return The sprite reference.
 */
int RuleItem::getSpecialIconSprite() const
{
	return _specialIconSprite;
}

/**
 * Returns whether this item is held with two hands.
 * @return True if it is two-handed.
 */
bool RuleItem::isTwoHanded() const
{
	return _twoHanded;
}

/**
 * Returns whether this item must be used with both hands.
 * @return True if requires both hands.
 */
bool RuleItem::isBlockingBothHands() const
{
	return _blockBothHands;
}

/**
 * Returns whether this item uses waypoints.
 * @return True if it uses waypoints.
 */
int RuleItem::getWaypoints() const
{
	return _waypoints;
}

/**
 * Returns whether this item is a fixed weapon.
 * You can't move/throw/drop fixed weapons - e.g. HWP turrets.
 * @return True if it is a fixed weapon.
 */
bool RuleItem::isFixed() const
{
	return _fixedWeapon;
}

/**
 * Do show fixed item on unit.
 * @return true if show even is fixed.
 */
bool RuleItem::getFixedShow() const
{
	return _fixedWeaponShow;
}

/**
 * Checks if the item can be placed into a given inventory section.
 * @param inventorySection Inventory section rule.
 * @return True if the item can be placed into a given inventory section.
 */
bool RuleItem::canBePlacedIntoInventorySection(const RuleInventory* inventorySection) const
{
	// backwards-compatibility
	if (_supportedInventorySections.empty())
		return true;

	// always possible to put an item on the ground
	if (inventorySection->getType() == INV_GROUND)
		return true;

	// otherwise check allowed inventory sections
	return Collections::sortVectorHave(_supportedInventorySections, inventorySection);
}

/**
 * Gets the item's bullet sprite reference.
 * @return The sprite reference.
 */
int RuleItem::getBulletSprite() const
{
	return _bulletSprite;
}

/**
 * Gets a random sound id from a given sound vector.
 * @param vector The source vector.
 * @param defaultValue Default value (in case nothing is specified = vector is empty).
 * @return The sound id.
 */
int RuleItem::getRandomSound(const std::vector<int> &vector, int defaultValue) const
{
	if (!vector.empty())
	{
		return vector[RNG::generate(0, vector.size() - 1)];
	}
	return defaultValue;
}

/**
 * Gets the item's reload sound.
 * @return The reload sound id.
 */
int RuleItem::getReloadSound() const
{
	return getRandomSound(_reloadSound);
}

/**
 * Gets the item's prime sound.
 * @return The prime sound id.
 */
int RuleItem::getPrimeSound() const
{
	return getRandomSound(_primeSound);
}

/**
 * Gets the item's unprime sound.
 * @return The unprime sound id.
 */
int RuleItem::getUnprimeSound() const
{
	return getRandomSound(_unprimeSound);
}

/**
 * Gets the item's fire sound.
 * @return The fire sound id.
 */
int RuleItem::getFireSound() const
{
	return getRandomSound(_fireSound);
}

/**
 * Gets the item's hit sound.
 * @return The hit sound id.
 */
int RuleItem::getHitSound() const
{
	return getRandomSound(_hitSound);
}

/**
 * Gets the item's hit animation.
 * @return The hit animation id.
 */
int RuleItem::getHitAnimation() const
{
	return _hitAnimation;
}

/**
 * Gets the item's miss sound.
 * @return The miss sound id.
 */
int RuleItem::getHitMissSound() const
{
	return getRandomSound(_hitMissSound);
}

/**
 * Gets the item's miss animation.
 * @return The miss animation id.
 */
int RuleItem::getHitMissAnimation() const
{
	return _hitMissAnimation;
}


/**
 * What sound does this weapon make when you swing this at someone?
 * @return The weapon's melee attack sound.
 */
int RuleItem::getMeleeSound() const
{
	return getRandomSound(_meleeSound, 39);
}

/**
 * What is the starting frame offset in hit.pck to use for the animation?
 * @return the starting frame offset in hit.pck to use for the animation.
 */
int RuleItem::getMeleeAnimation() const
{
	return _meleeAnimation;
}

/**
 * What sound does this weapon make when you miss a swing?
 * @return The weapon's melee attack miss sound.
 */
int RuleItem::getMeleeMissSound() const
{
	return getRandomSound(_meleeMissSound);
}

/**
 * What is the starting frame offset in hit.pck to use for the animation?
 * @return the starting frame offset in hit.pck to use for the animation.
 */
int RuleItem::getMeleeMissAnimation() const
{
	return _meleeMissAnimation;
}

/**
 * What sound does this weapon make when you punch someone in the face with it?
 * @return The weapon's melee hit sound.
 */
int RuleItem::getMeleeHitSound() const
{
	return getRandomSound(_meleeHitSound);
}

/**
 * What sound does explosion sound?
 * @return The weapon's explosion sound.
 */
int RuleItem::getExplosionHitSound() const
{
	return getRandomSound(_explosionHitSound);
}

/**
 * Gets the item's psi hit sound.
 * @return The hit sound id.
 */
int RuleItem::getPsiSound() const
{
	return getRandomSound(_psiSound);
}

/**
 * What is the starting frame offset in hit.pck to use for the animation?
 * @return the starting frame offset in hit.pck to use for the animation.
 */
int RuleItem::getPsiAnimation() const
{
	return _psiAnimation;
}

/**
 * Gets the item's psi miss sound.
 * @return The miss sound id.
 */
int RuleItem::getPsiMissSound() const
{
	return getRandomSound(_psiMissSound);
}

/**
 * What is the starting frame offset in hit.pck to use for the animation?
 * @return the starting frame offset in hit.pck to use for the animation.
 */
int RuleItem::getPsiMissAnimation() const
{
	return _psiMissAnimation;
}

/**
 * Gets the item's power.
 * @return The power.
 */
int RuleItem::getPower() const
{
	return _power;
}

/**
 * Gets amount of power dropped for range in voxels.
 * @return Power reduction.
 */
float RuleItem::getPowerRangeReduction(float range) const
{
	range -= _powerRangeThreshold * TilesToVexels;
	return (_powerRangeReduction * VexelsToTiles) * (range > 0 ? range : 0);
}

/**
 * Get amount of psi accuracy dropped for range in voxels.
 * @param range
 * @return Psi accuracy reduction.
 */
float RuleItem::getPsiAccuracyRangeReduction(float range) const
{
	range -= _confAimed.range * TilesToVexels;
	return (_dropoff * VexelsToTiles) * (range > 0 ? range : 0);
}

/**
 * Get configuration of aimed shot action.
 */
const RuleItemAction *RuleItem::getConfigAimed() const
{
	return &_confAimed;
}

/**
 * Get configuration of autoshot action.
 */
const RuleItemAction *RuleItem::getConfigAuto() const
{
	return &_confAuto;
}

/**
 * Get configuration of snapshot action.
 */
const RuleItemAction *RuleItem::getConfigSnap() const
{
	return &_confSnap;
}

/**
 * Get configuration of melee action.
 */
const RuleItemAction *RuleItem::getConfigMelee() const
{
	return &_confMelee;
}


/**
 * Gets the item's accuracy for snapshots.
 * @return The snapshot accuracy.
 */
int RuleItem::getAccuracySnap() const
{
	return _confSnap.accuracy;
}

/**
 * Gets the item's accuracy for autoshots.
 * @return The autoshot accuracy.
 */
int RuleItem::getAccuracyAuto() const
{
	return _confAuto.accuracy;
}

/**
 * Gets the item's accuracy for aimed shots.
 * @return The aimed accuracy.
 */
int RuleItem::getAccuracyAimed() const
{
	return _confAimed.accuracy;
}

/**
 * Gets the item's accuracy for melee attacks.
 * @return The melee accuracy.
 */
int RuleItem::getAccuracyMelee() const
{
	return _confMelee.accuracy;
}

/**
 * Gets the item's accuracy for use psi-amp.
 * @return The psi-amp accuracy.
 */
int RuleItem::getAccuracyUse() const
{
	return _accuracyUse;
}

/**
 * Gets the item's accuracy for mind control use.
 * @return The mind control accuracy.
 */
int RuleItem::getAccuracyMind() const
{
	return _accuracyMind;
}

/**
 * Gets the item's accuracy for panic use.
 * @return The panic accuracy.
 */
int RuleItem::getAccuracyPanic() const
{
	return _accuracyPanic;
}

/**
 * Gets the item's accuracy for throw.
 * @return The throw accuracy.
 */
int RuleItem::getAccuracyThrow() const
{
	return _accuracyThrow;
}

/**
 * Gets the item's accuracy for close quarters combat.
 * @return The close quarters accuracy.
 */
int RuleItem::getAccuracyCloseQuarters(const Mod *mod) const
{
	return _accuracyCloseQuarters != -1 ? _accuracyCloseQuarters : mod->getCloseQuartersAccuracyGlobal();
}

/**
 * Gets the item's accuracy penalty for out-of-LOS targets
 * @return The no-LOS accuracy penalty.
 */
int RuleItem::getNoLOSAccuracyPenalty(const Mod *mod) const
{
	return _noLOSAccuracyPenalty != -1 ? _noLOSAccuracyPenalty : mod->getNoLOSAccuracyPenaltyGlobal();
}

/**
 * Gets the setting for primed explosives exploding in the inventory.
 * @return The setting (0 = no, 1 = yes, except when in hands, 2 = always).
 */
int RuleItem::getExplodeInventory(const Mod* mod) const
{
	return _explodeInventory != -1 ? _explodeInventory : (_battleType == BT_GRENADE ? mod->getExplodeInventoryGlobal() : 0);
}

/**
 * Gets the item's time unit percentage for aimed shots.
 * @return The aimed shot TU percentage.
 */
RuleItemUseCost RuleItem::getCostAimed() const
{
	return getDefault(_confAimed.cost);
}

/**
 * Gets the item's time unit percentage for autoshots.
 * @return The autoshot TU percentage.
 */
RuleItemUseCost RuleItem::getCostAuto() const
{
	return getDefault(_confAuto.cost, _confAimed.cost);
}

/**
 * Gets the item's time unit percentage for snapshots.
 * @return The snapshot TU percentage.
 */
RuleItemUseCost RuleItem::getCostSnap() const
{
	return getDefault(_confSnap.cost, _confAimed.cost);
}

/**
 * Gets the item's time unit percentage for melee attacks.
 * @return The melee TU percentage.
 */
RuleItemUseCost RuleItem::getCostMelee() const
{
	return getDefault(_confMelee.cost);
}

/**
 * Gets the number of Time Units needed to use this item.
 * @return The number of Time Units needed to use this item.
 */
RuleItemUseCost RuleItem::getCostUse() const
{
	if (_battleType != BT_PSIAMP || !_psiAttackName.empty())
	{
		return getDefault(_costUse);
	}
	else
	{
		return {};
	}
}

/**
 * Gets the number of Time Units needed to use mind control action.
 * @return The number of Time Units needed to mind control.
 */
RuleItemUseCost RuleItem::getCostMind() const
{
	return getDefault(_costMind, _costUse);
}

/**
 * Gets the number of Time Units needed to use panic action.
 * @return The number of Time Units needed to panic.
 */
RuleItemUseCost RuleItem::getCostPanic() const
{
	return getDefault(_costPanic, _costUse);
}

/**
 * Gets the item's time unit percentage for throwing.
 * @return The throw TU percentage.
 */
RuleItemUseCost RuleItem::getCostThrow() const
{
	return getDefault(_costThrow);
}

/**
 * Gets the item's time unit percentage for prime grenade.
 * @return The prime TU percentage.
 */
RuleItemUseCost RuleItem::getCostPrime() const
{
	if (!_primeActionName.empty())
	{
		return getDefault(_costPrime);
	}
	else
	{
		return { };
	}
}

/**
 * Gets the item's time unit percentage for unprime grenade.
 * @return The prime TU percentage.
 */
RuleItemUseCost RuleItem::getCostUnprime() const
{
	return getDefault(_costUnprime);
}

/**
 * Gets the item's time unit for loading weapon ammo.
 * @param slot Slot position.
 * @return The throw TU.
 */
int RuleItem::getTULoad(int slot) const
{
	return _tuLoad[slot];
}

/**
 * Gets the item's time unit for unloading weapon ammo.
 * @param slot Slot position.
 * @return The throw TU.
 */
int RuleItem::getTUUnload(int slot) const
{
	return _tuUnload[slot];
}

/**
 * Gets the ammo type for a vehicle.
 */
const RuleItem* RuleItem::getVehicleClipAmmo() const
{
	if (_vehicleFixedAmmoSlot > -1)
	{
		return _compatibleAmmo[_vehicleFixedAmmoSlot].empty() ? nullptr : _compatibleAmmo[_vehicleFixedAmmoSlot].front();
	}
	return nullptr;
}

/**
 * Gets the maximum number of rounds for a vehicle. E.g. a vehicle that can load 6 clips with 10 rounds each, returns 60.
 */
int RuleItem::getVehicleClipSize() const
{
	auto ammo = getVehicleClipAmmo();
	if (ammo)
	{
		if (ammo->getClipSize() > 0 && getClipSize() > 0)
		{
			return getClipSize();
		}
		else
		{
			return ammo->getClipSize();
		}
	}
	else
	{
		return getClipSize();
	}
}

/**
 * Gets the number of clips needed to fully load a vehicle. E.g. a vehicle that holds max 60 rounds and clip size is 10, returns 6.
 */
int RuleItem::getVehicleClipsLoaded() const
{
	auto ammo = getVehicleClipAmmo();
	if (ammo)
	{
		if (ammo->getClipSize() > 0 && getClipSize() > 0)
		{
			return getClipSize() / ammo->getClipSize();
		}
		else
		{
			return ammo->getClipSize();
		}
	}
	else
	{
		return 0;
	}
}
/**
 * Gets a list of compatible ammo.
 * @return Pointer to a list of compatible ammo.
 */
const std::vector<const RuleItem*> *RuleItem::getPrimaryCompatibleAmmo() const
{
	return &_compatibleAmmo[0];
}

/**
 * Gets slot position for ammo type.
 * @param type Type of ammo item.
 * @return Slot position.
 */
int RuleItem::getSlotForAmmo(const RuleItem* type) const
{
	auto f = _compatibleAmmoSlots.find(type);
	if (f != _compatibleAmmoSlots.end())
	{
		return f->second;
	}
	return -1;
}

/**
 *  Get slot position for ammo type.
 */
const std::vector<const RuleItem*> *RuleItem::getCompatibleAmmoForSlot(int slot) const
{
	return &_compatibleAmmo[slot];
}

/**
 * Gets the item's damage type.
 * @return The damage type.
 */
const RuleDamageType *RuleItem::getDamageType() const
{
	return &_damageType;
}

/**
 * Gets the item's melee damage type for range weapons.
 * @return The damage type.
 */
const RuleDamageType *RuleItem::getMeleeType() const
{
	return &_meleeType;
}

/**
 * Gets the item's battle type.
 * @return The battle type.
 */
BattleType RuleItem::getBattleType() const
{
	return _battleType;
}

/**
 * Is the item's type BT_GRENADE or BT_PROXIMITYGRENADE?
 */
bool RuleItem::isGrenadeOrProxy() const
{
	return _battleType == BT_GRENADE || _battleType == BT_PROXIMITYGRENADE;
}

/**
 * Gets the item's fuse timer type.
 * @return Fuse Timer Type.
 */
BattleFuseType RuleItem::getFuseTimerType() const
{
	if (_fuseType == BFT_SET && Options::instantPrime)
		return BFT_FIX_MIN;
	return _fuseType;
}

/**
 * Gets the item's default fuse timer.
 * @return Time in turns.
 */
int RuleItem::getFuseTimerDefault() const
{
	if (_fuseType >= BFT_FIX_MIN && _fuseType < BFT_FIX_MAX)
	{
		return (int)_fuseType;
	}
	else if (_fuseType == BFT_SET || _fuseType == BFT_INSTANT)
	{
		return 0;
	}
	else
	{
		return -1; //can't prime
	}
}

/**
 * Is this item (e.g. a mine) hidden on the minimap?
 * @return True if the item should be hidden.
 */
bool RuleItem::isHiddenOnMinimap() const
{
	return _hiddenOnMinimap;
}

/**
 * Get fuse trigger event.
 */
const RuleItemFuseTrigger *RuleItem::getFuseTriggerEvent() const
{
	return &_fuseTriggerEvents;
}

/**
 * Gets the item's width in a soldier's inventory.
 * @return The width.
 */
int RuleItem::getInventoryWidth() const
{
	return _invWidth;
}

/**
 * Gets the item's height in a soldier's inventory.
 * @return The height.
 */
int RuleItem::getInventoryHeight() const
{
	return _invHeight;
}

/**
 * Gets the item's ammo clip size.
 * @return The ammo clip size.
 */
int RuleItem::getClipSize() const
{
	return _clipSize;
}

/**
 * Gets the chance of special effect like zombify or corpse explosion or mine triggering.
 * @return Percent value.
 */
int RuleItem::getSpecialChance() const
{
	return _specialChance;
}
/**
 * Draws and centers the hand sprite on a surface
 * according to its dimensions.
 * @param texture Pointer to the surface set to get the sprite from.
 * @param surface Pointer to the surface to draw to.
 */
void RuleItem::drawHandSprite(const SurfaceSet *texture, Surface *surface, const BattleItem *item, const SavedBattleGame *save, int animFrame) const
{
	//TODO: split this function to one using only `this` and another using only `item`
	const Surface *frame = nullptr;
	if (item)
	{
		frame = item->getBigSprite(texture, save, animFrame);
		if (frame)
		{
			ScriptWorkerBlit scr;
			BattleItem::ScriptFill(&scr, item, save, BODYPART_ITEM_INVENTORY, animFrame, 0);
			scr.executeBlit(frame, surface, this->getHandSpriteOffX(), this->getHandSpriteOffY(), 0);
		}
	}
	else
	{
		frame = texture->getFrame(this->getBigSprite());
		frame->blitNShade(surface, this->getHandSpriteOffX(), this->getHandSpriteOffY());
	}
}

/**
 * item's hand spite x offset
 * @return x offset
 */
int RuleItem::getHandSpriteOffX() const
{
	return (RuleInventory::HAND_W - getInventoryWidth()) * RuleInventory::SLOT_W/2;
}

/**
 * item's hand spite y offset
 * @return y offset
 */
int RuleItem::getHandSpriteOffY() const
{
	return (RuleInventory::HAND_H - getInventoryHeight()) * RuleInventory::SLOT_H/2;
}

/**
 * Gets the heal quantity of the item.
 * @return The new heal quantity.
 */
int RuleItem::getHealQuantity() const
{
	return _heal;
}

/**
 * Gets the pain killer quantity of the item.
 * @return The new pain killer quantity.
 */
int RuleItem::getPainKillerQuantity() const
{
	return _painKiller;
}

/**
 * Gets the stimulant quantity of the item.
 * @return The new stimulant quantity.
 */
int RuleItem::getStimulantQuantity() const
{
	return _stimulant;
}

/**
 * Gets the amount of fatal wound healed per usage.
 * @return The amount of fatal wound healed.
 */
int RuleItem::getWoundRecovery() const
{
	return _woundRecovery;
}

/**
 * Gets the amount of health added to a wounded soldier's health.
 * @return The amount of health to add.
 */
int RuleItem::getHealthRecovery() const
{
	return _healthRecovery;
}

/**
 * Gets the amount of energy added to a soldier's energy.
 * @return The amount of energy to add.
 */
int RuleItem::getEnergyRecovery() const
{
	return _energyRecovery;
}

/**
 * Gets the amount of stun removed from a soldier's stun level.
 * @return The amount of stun removed.
 */
int RuleItem::getStunRecovery() const
{
	return _stunRecovery;
}

/**
 * Gets the amount of morale added to a solders's morale.
 * @return The amount of morale to add.
 */
int RuleItem::getMoraleRecovery() const
{
	return _moraleRecovery;
}

/**
 * Gets the medikit morale recovered based on missing health.
 * @return The ratio of how much restore.
 */
float RuleItem::getPainKillerRecovery() const
{
	return _painKillerRecovery;
}

/**
 * Is this (medikit-type & items with prime) item consumable?
 * @return True if the item is consumable.
 */
bool RuleItem::isConsumable() const
{
	return _isConsumable;
}

/**
 * Does this item extinguish fire?
 * @return True if the item extinguishes fire.
 */
bool RuleItem::isFireExtinguisher() const
{
	return _isFireExtinguisher;
}

/**
 * If this item is used as a specialWeapon, can it be accessed by an empty hand?
 * @return True if accessed by empty hand.
 */
bool RuleItem::isSpecialUsingEmptyHand() const
{
	return _specialUseEmptyHand;
}

/**
 * Gets the medikit type of how it operate.
 * @return Type of medikit.
 */
BattleMediKitType RuleItem::getMediKitType() const
{
	return _medikitType;
}

/**
 * Gets the medikit custom background.
 * @return Sprite ID.
 */
const std::string &RuleItem::getMediKitCustomBackground() const
{
	return _medikitBackground;
}

/**
 * Returns the item's max explosion radius. Small explosions don't have a restriction.
 * Larger explosions are restricted using a formula, with a maximum of radius 10 no matter how large the explosion is.
 * @param stats unit stats
 * @return The radius.
 */
int RuleItem::getExplosionRadius(BattleActionAttack::ReadOnly attack) const
{
	int radius = 0;

	if (_damageType.FixRadius == -1)
	{
		radius = getPowerBonus(attack) * _damageType.RadiusEffectiveness;
		if (_damageType.FireBlastCalc)
		{
			radius += 1;
		}
		// cap the formula to 11
		if (radius > 11)
		{
			radius = 11;
		}
		if (radius <= 0)
		{
			radius = 1;
		}
	}
	else
	{
		// unless a blast radius is actually defined.
		radius = _damageType.FixRadius;
	}

	return radius;
}

/**
 * Returns the item's recovery points.
 * This is used during the battlescape debriefing score calculation.
 * @return The recovery points.
 */
int RuleItem::getRecoveryPoints() const
{
	return _recoveryPoints;
}

/**
 * Returns the item's armor.
 * The item is destroyed when an explosion power bigger than its armor hits it.
 * @return The armor.
 */
int RuleItem::getArmor() const
{
	return _armor;
}

/**
 * Check if item is normal inventory item.
 */
bool RuleItem::isInventoryItem() const
{
	return getBigSprite() > -1 && isFixed() == false;
}

/**
 * Checks if item have some use in battlescape.
 */
bool RuleItem::isUsefulBattlescapeItem() const
{
	return (_battleType != BT_CORPSE && _battleType != BT_NONE);
}

/**
 * Returns if the item should be recoverable
 * from the battlescape.
 * @return True if it is recoverable.
 */
bool RuleItem::isRecoverable() const
{
	return _recover;
}


/**
 * Returns if the corpse item should be recoverable from the battlescape.
 * @return True if it is recoverable.
 */
bool RuleItem::isCorpseRecoverable() const
{
	// Explanation:
	// Since the "recover" flag applies to both live body (prisoner capture) and dead body (corpse recovery) in OXC,
	// OXCE adds this new flag to allow recovery of a live body, but disable recovery of the corpse
	// (used in mods mostly to ignore dead bodies of killed humans)
	return _recoverCorpse;
}


/**
* Checks if the item can be equipped in base defense mission.
* @return True if it can be equipped.
*/
bool RuleItem::canBeEquippedBeforeBaseDefense() const
{
	return !_ignoreInBaseDefense;
}

/**
 * Check if the item can be equipped to craft inventory.
 * @return True if it can be equipped.
 */
bool RuleItem::canBeEquippedToCraftInventory() const
{
	return !_ignoreInCraftEquip;
}

/**
 * Returns the item's Turret Type.
 * @return The turret index (-1 for no turret).
 */
int RuleItem::getTurretType() const
{
	return _turretType;
}

/**
 * Returns first turn when AI can use item.
 * @param Pointer to the mod. 0 by default.
 * @return First turn when AI can use item.
 *	if mod == 0 returns only local defined aiUseDelay
 *	else takes into account global define of aiUseDelay for this item
 */
int RuleItem::getAIUseDelay(const Mod *mod) const
{
	if (Options::ignoreDelay)
		return 0;

	if (mod == 0 || _aiUseDelay >= 0)
		return _aiUseDelay;

	switch (getBattleType())
	{
	case BT_FIREARM:
		if (getWaypoints())
		{
			return mod->getAIUseDelayBlaster();
		}
		else
		{
			return mod->getAIUseDelayFirearm();
		}

	case BT_MELEE:
		return mod->getAIUseDelayMelee();

	case BT_GRENADE:
		return mod->getAIUseDelayGrenade();

	case BT_PROXIMITYGRENADE:
		return mod->getAIUseDelayProxy();

	case BT_PSIAMP:
		return mod->getAIUseDelayPsionic();

	case BT_MEDIKIT:
		return mod->getAIUseDelayMedikit();

	default:
		return _aiUseDelay;
	}
}

/**
 * Returns number of melee hits AI should do when attacking enemy.
 * @return Number of hits.
 */
int RuleItem::getAIMeleeHitCount() const
{
	return _aiMeleeHitCount;
}

/**
 * Returns if this is a live alien.
 * @return True if this is a live alien.
 */
bool RuleItem::isAlien() const
{
	return _liveAlien;
}

/**
* Returns to which type of prison does the live alien belong.
* @return Prison type.
*/
int RuleItem::getPrisonType() const
{
	return _liveAlienPrisonType;
}

/**
 * Returns whether this item charges a flat rate for costAimed.
 * @return True if this item charges a flat rate for costAimed.
 */
RuleItemUseFlat RuleItem::getFlatAimed() const
{
	return getDefault(_confAimed.flat, _flatUse);
}

/**
 * Returns whether this item charges a flat rate for costAuto.
 * @return True if this item charges a flat rate for costAuto.
 */
RuleItemUseFlat RuleItem::getFlatAuto() const
{
	return getDefault(_confAuto.flat, _confAimed.flat, _flatUse);
}

/**
 * Returns whether this item charges a flat rate for costSnap.
 * @return True if this item charges a flat rate for costSnap.
 */
RuleItemUseFlat RuleItem::getFlatSnap() const
{
	return getDefault(_confSnap.flat, _confAimed.flat, _flatUse);
}

/**
 * Returns whether this item charges a flat rate for costMelee.
 * @return True if this item charges a flat rate for costMelee.
 */
RuleItemUseFlat RuleItem::getFlatMelee() const
{
	return getDefault(_confMelee.flat, _flatUse);
}

/**
 * Returns whether this item charges a flat rate of use and attack cost.
 * @return True if this item charges a flat rate of use and attack cost.
 */
RuleItemUseFlat RuleItem::getFlatUse() const
{
	return getDefault(_flatUse);
}

/**
 * Returns whether this item charges a flat rate for costThrow.
 * @return True if this item charges a flat rate for costThrow.
 */
RuleItemUseFlat RuleItem::getFlatThrow() const
{
	return getDefault(_flatThrow);
}

/**
 * Returns whether this item charges a flat rate for costPrime.
 * @return True if this item charges a flat rate for costPrime.
 */
RuleItemUseFlat RuleItem::getFlatPrime() const
{
	return getDefault(_flatPrime);
}

/**
 * Returns whether this item charges a flat rate for costUnprime.
 * @return True if this item charges a flat rate for costUnprime.
 */
RuleItemUseFlat RuleItem::getFlatUnprime() const
{
	return getDefault(_flatUnprime);
}

/**
 * Returns if this weapon should arc its shots.
 * @return True if this weapon should arc its shots.
 */
bool RuleItem::getArcingShot() const
{
	return _arcingShot;
}

/**
 * Returns the experience training mode configured for this weapon.
 * @return The mode ID.
 */
ExperienceTrainingMode RuleItem::getExperienceTrainingMode() const
{
	return _experienceTrainingMode;
}

/**
 * Gets the attraction value for this item (for AI).
 * @return The attraction value.
 */
int RuleItem::getAttraction() const
{
	return _attraction;
}

/**
 * Gets the list weight for this research item
 * @return The list weight.
 */
int RuleItem::getListOrder() const
{
	return _listOrder;
}

/**
 * Gets the maximum range of this weapon
 * @return The maximum range.
 */
int RuleItem::getMaxRange() const
{
	return _maxRange;
}

/**
 * Checks whether a given distance is out of range for this item.
 * @param distanceSq Given distance squared.
 * @return True, if out of range.
 */
bool RuleItem::isOutOfRange(int distanceSq) const
{
	bool outOfRange = distanceSq > (_maxRange * _maxRange);
	// special handling for short ranges and diagonals
	if (outOfRange)
	{
		// special handling for maxRange 1: allow it to target diagonally adjacent tiles (one diagonal move)
		if (_maxRange == 1 && distanceSq <= 3)
		{
			outOfRange = false;
		}
		// special handling for maxRange 2: allow it to target diagonally adjacent tiles (one diagonal move + one straight move)
		else if (_maxRange == 2 && distanceSq <= 6)
		{
			outOfRange = false;
		}
	}
	return outOfRange;
}

/**
 * Checks whether a given distance is out of throw range for this item.
 * @return True, if out of throw range.
 */
bool RuleItem::isOutOfThrowRange(int distanceSq, int depth) const
{
	bool outOfRange = false;

	if (depth > 0)
	{
		if (distanceSq > _underwaterThrowRange * _underwaterThrowRange)
		{
			outOfRange = true;
		}
	}
	else
	{
		if (distanceSq > _throwRange * _throwRange)
		{
			outOfRange = true;
		}
	}

	// no special handling for short ranges and diagonals
	return outOfRange;
}

/**
 * Gets the maximum effective range of this weapon when using Aimed Shot.
 * @return The maximum range.
 */
int RuleItem::getAimRange() const
{
	return _confAimed.range;
}

/**
 * Gets the maximum effective range of this weapon for Snap Shot.
 * @return The maximum range.
 */
int RuleItem::getSnapRange() const
{
	return _confSnap.range;
}

/**
 * Gets the maximum effective range of this weapon for Auto Shot.
 * @return The maximum range.
 */
int RuleItem::getAutoRange() const
{
	return _confAuto.range;
}

/**
 * Gets the minimum effective range of this weapon.
 * @return The minimum effective range.
 */
int RuleItem::getMinRange() const
{
	return _minRange;
}

/**
 * Gets the accuracy dropoff value of this weapon.
 * @return The per-tile dropoff.
 */
int RuleItem::getDropoff() const
{
	return _dropoff;
}

/**
 * Helper function to calculate limits and dropoff.
 * @return The per-tile dropoff.
 */
int RuleItem::calculateLimits(int& upperLimit, int& lowerLimit, int depth, BattleActionType type) const
{
	upperLimit = type == BA_THROW ? 200 : getAimRange();
	lowerLimit = type == BA_THROW ?   0 : getMinRange();

	if (Options::battleUFOExtenderAccuracy)
	{
		switch (type)
		{
		case BA_SNAPSHOT:
			upperLimit = getSnapRange();
			break;
		case BA_AUTOSHOT:
			upperLimit = getAutoRange();
			break;
		case BA_THROW:
			upperLimit = depth > 0 ? getUnderwaterThrowDropoffRange() : getThrowDropoffRange();
			break;
		default:
			break;
		}
	}

	return type == BA_THROW ? getThrowDropoff() : getDropoff();
}

/**
 * Gets the speed at which this bullet travels.
 * @return The speed.
 */
int RuleItem::getBulletSpeed() const
{
	return _bulletSpeed;
}

/**
 * Gets the speed at which this bullet explodes.
 * @return The speed.
 */
int RuleItem::getExplosionSpeed() const
{
	return _explosionSpeed;
}

/**
 * is this item a rifle?
 * @return whether or not it is a rifle.
 */
bool RuleItem::isRifle() const
{
	return (_battleType == BT_FIREARM || _battleType == BT_MELEE) && _twoHanded;
}

/**
 * is this item a pistol?
 * @return whether or not it is a pistol.
 */
bool RuleItem::isPistol() const
{
	return (_battleType == BT_FIREARM || _battleType == BT_MELEE) && !_twoHanded;
}

/**
 * Gets the number of projectiles this ammo shoots at once.
 * @return The number of projectiles.
 */
int RuleItem::getShotgunPellets() const
{
	return _shotgunPellets;
}

/**
* Gets the shotgun behavior type. This is an attribute of shotgun ammo.
* @return 0 = cone-like spread (vanilla), 1 = grouping.
*/
int RuleItem::getShotgunBehaviorType() const
{
	return _shotgunBehaviorType;
}

/**
* Gets the spread of shotgun projectiles. This is an attribute of shotgun ammo.
* Can be used in both shotgun behavior types.
* @return The shotgun spread.
*/
int RuleItem::getShotgunSpread() const
{
	return _shotgunSpread;
}

/**
* Gets the shotgun choke value for modifying pellet spread. This is an attribute of the weapon (not ammo).
* @return The shotgun choke value.
*/
int RuleItem::getShotgunChoke() const
{
	return _shotgunChoke;
}

/**
 * Gets the unit that the victim is morphed into when attacked.
 * @return The weapon's zombie unit.
 */
const std::string &RuleItem::getZombieUnit(const BattleUnit* victim) const
{
	if (victim)
	{
		// by armor and gender
		if (victim->getGender() == GENDER_MALE)
		{
			auto i = _zombieUnitByArmorMale.find(victim->getArmor()->getType());
			if (i != _zombieUnitByArmorMale.end())
			{
				return i->second;
			}
		}
		else
		{
			auto j = _zombieUnitByArmorFemale.find(victim->getArmor()->getType());
			if (j != _zombieUnitByArmorFemale.end())
			{
				return j->second;
			}
		}
		// by type
		const std::string victimType = victim->getUnitRules() ? victim->getUnitRules()->getType() : victim->getGeoscapeSoldier()->getRules()->getType();
		auto k = _zombieUnitByType.find(victimType);
		if (k != _zombieUnitByType.end())
		{
			return k->second;
		}
	}
	// fall back
	return _zombieUnit;
}

/**
 * How much damage does this weapon do when you punch someone in the face with it?
 * @return The weapon's melee power.
 */
int RuleItem::getMeleePower() const
{
	return _meleePower;
}

/**
 * Checks if this item can be used to target a given faction.
 * Usage #1: checks the psiamp's allowed targets.
 * - Not used in AI.
 * - Mind control of the same faction is hardcoded disabled.
 * Usage #2: checks if a death trap item applies to a given faction (it use `attacker == FACTION_PLAYER`).
 * @return True if allowed, false otherwise.
 */
bool RuleItem::isTargetAllowed(UnitFaction targetFaction, UnitFaction attacker) const
{
	if (targetFaction == attacker) // same faction "attack"
	{
		return _targetMatrix & 1;
	}
	else if ((targetFaction == FACTION_HOSTILE) || (attacker == FACTION_HOSTILE)) // can't be both true as first `if` cover this case
	{
		return _targetMatrix & 2;
	}
	else if ((targetFaction == FACTION_NEUTRAL) || (attacker == FACTION_NEUTRAL)) // only `Player -> Neutral` or `Neutral -> Player` left
	{
		return _targetMatrix & 4;
	}
	return false;
}

/**
 * Is line of sight required for this psionic weapon to function?
 * @return If line of sight is required.
 */
bool RuleItem::isLOSRequired() const
{
	return _LOSRequired;
}

/**
 * Can this item only be used underwater?
 * @return if this is an underwater weapon or not.
 */
bool RuleItem::isWaterOnly() const
{
	return _underwaterOnly;
}

/**
* Can this item only be used on land?
* @return if this is a land weapon or not.
*/
bool RuleItem::isLandOnly() const
{
	return _landOnly;
}

/**
 * Is psi skill is required to use this weapon.
 * @return If psi skill is required.
 */
bool RuleItem::isPsiRequired() const
{
	return _psiReqiured;
}

/**
 * Is mana required to use this weapon?
 * @return If mana is required.
 */
bool RuleItem::isManaRequired() const
{
	return _manaRequired;
}

/**
 * Compute power bonus based on unit stats.
 * @param stats unit stats
 * @return bonus power.
 */
int RuleItem::getPowerBonus(BattleActionAttack::ReadOnly attack) const
{
	return _damageBonus.getBonus(attack, _power);
}

/**
 * Compute power bonus based on unit stats.
 * @param stats unit stats
 * @return bonus power.
 */
int RuleItem::getMeleeBonus(BattleActionAttack::ReadOnly attack) const
{
	return _meleeBonus.getBonus(attack, _meleePower);
}

/**
 * Compute multiplier of melee hit chance based on unit stats.
 * @param stats unit stats
 * @return multiplier.
 */
int RuleItem::getMeleeMultiplier(BattleActionAttack::ReadOnly attack) const
{
	return _meleeMulti.getBonus(attack);
}

/**
 * Compute multiplier of accuracy based on unit stats.
 * @param stats unit stats
 * @return multiplier.
 */
int RuleItem::getAccuracyMultiplier(BattleActionAttack::ReadOnly attack) const
{
	return _accuracyMulti.getBonus(attack);
}

/**
 * Compute multiplier of throw accuracy based on unit stats.
 * @param stats unit stats
 * @return multiplier.
 */
int RuleItem::getThrowMultiplier(BattleActionAttack::ReadOnly attack) const
{
	return _throwMulti.getBonus(attack);
}

/**
 * Compute multiplier of close quarters accuracy based on unit stats.
 * @param stats unit stats
 * @return multiplier.
 */
int RuleItem::getCloseQuartersMultiplier(BattleActionAttack::ReadOnly attack) const
{
	return _closeQuartersMulti.getBonus(attack);
}

/**
 * Gets the associated special type of this item.
 * note that type 14 is the alien brain, and types
 * 0 and 1 are "regular tile" and "starting point"
 * so try not to use those ones.
 * @return special type.
 */
int RuleItem::getSpecialType() const
{
	return _specialType;
}

/**
 * Gets the color offset to use for the vapor trail.
 * @param depth battlescape depth (0=surface, 1-3=underwater)
 * @return the color offset.
 */
int RuleItem::getVaporColor(int depth) const
{
	if (depth == 0)
		return _vaporColorSurface;

	return _vaporColor;
}

/**
 * Gets the vapor cloud density for the vapor trail.
 * @param depth battlescape depth (0=surface, 1-3=underwater)
 * @return the vapor density.
 */
int RuleItem::getVaporDensity(int depth) const
{
	if (depth == 0)
		return _vaporDensitySurface;

	return _vaporDensity;
}

/**
 * Gets the vapor cloud probability for the vapor trail.
 * @param depth battlescape depth (0=surface, 1-3=underwater)
 * @return the vapor probability.
 */
int RuleItem::getVaporProbability(int depth) const
{
	if (depth == 0)
		return _vaporProbabilitySurface;

	return _vaporProbability;
}

/**
 * Gets the index of the sprite in the CustomItemPreview sprite set.
 * @return Sprite index.
 */
const std::vector<int> &RuleItem::getCustomItemPreviewIndex() const
{
	return _customItemPreviewIndex;
}

/**
* Gets the kneel bonus (15% bonus is encoded as 100+15 = 115).
* @return Kneel bonus.
*/
int RuleItem::getKneelBonus(const Mod *mod) const
{
	return _kneelBonus != -1 ? _kneelBonus : mod->getKneelBonusGlobal();
}

/**
* Gets the one-handed penalty (20% penalty is encoded as 100-20 = 80).
* @return One-handed penalty.
*/
int RuleItem::getOneHandedPenalty(const Mod *mod) const
{
	return _oneHandedPenalty != -1 ? _oneHandedPenalty : mod->getOneHandedPenaltyGlobal();
}

/**
* Gets the monthly salary.
* @return Monthly salary.
*/
int RuleItem::getMonthlySalary() const
{
	return _monthlySalary;
}

/**
* Gets the monthly maintenance.
* @return Monthly maintenance.
*/
int RuleItem::getMonthlyMaintenance() const
{
	return _monthlyMaintenance;
}

/**
 * Gets how many waypoints are used for a "spray" attack
 * @return Number of waypoints.
 */
int RuleItem::getSprayWaypoints() const
{
	return _sprayWaypoints;
}


////////////////////////////////////////////////////////////
//					Script binding
////////////////////////////////////////////////////////////

namespace
{

void getTypeScript(const RuleItem* r, ScriptText& txt)
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

void getBattleTypeScript(const RuleItem *ri, int &ret)
{
	if (ri)
	{
		ret = (int)ri->getBattleType();
		return;
	}
	ret = (int)BT_NONE;
}

void isSingleTargetScript(const RuleItem* r, int &ret)
{
	if (r)
	{
		ret = (r->getDamageType()->FixRadius == 0);
		return;
	}
	else
	{
		ret = 0;
	}
}

void hasCategoryScript(const RuleItem* ri, int& val, const std::string& cat)
{
	if (ri)
	{
		auto it = std::find(ri->getCategories().begin(), ri->getCategories().end(), cat);
		if (it != ri->getCategories().end())
		{
			val = 1;
			return;
		}
	}
	val = 0;
}

void getResistTypeScript(const RuleDamageType* rdt, int &ret)
{
	ret = rdt ? rdt->ResistType : 0;
}

void getAoeScript(const RuleDamageType* rdt, int &ret)
{
	ret = rdt ? !rdt->isDirect() : 0;
}

void getRandomTypeScript(const RuleDamageType* rdt, int &ret)
{
	ret = rdt ? rdt->RandomType : 0;
}

void getArmorEffectivenessScript(const RuleDamageType* rdt, int& ret)
{
	ret = rdt ? round(rdt->ArmorEffectiveness * 100) : 0;
}

void getArmorIgnoreScript(const RuleDamageType* rdt, int& ret)
{
	ret = rdt ? rdt->ArmorIgnore : 0;
}

template<float RuleDamageType::* Ptr>
void getDamageToScript(const RuleDamageType* rdt, int &ret, int value)
{
	ret = rdt ? (rdt->* Ptr) * value : 0;
}

void getRandomDamageScript(const RuleDamageType* rdt, int &ret, int value, RNG::RandomState* rng)
{
	ret = 0;
	if (rdt && rng)
	{
		auto func = [&](int min, int max)
		{
			return rng->generate(min, max);
		};
		ret = rdt->getRandomDamage(value, &func);
	}
}

std::string debugDisplayScript(const RuleDamageType* rdt)
{
	if (rdt)
	{
		std::string s;
		s += "RuleDamageType";
		s += "(resist: ";
		s += std::to_string((int)rdt->ResistType);
		s += " random: ";
		s += std::to_string((int)rdt->RandomType);
		s += ")";
		return s;
	}
	else
	{
		return "null";
	}
}

std::string debugDisplayScript(const RuleItem* ri)
{
	if (ri)
	{
		std::string s;
		s += RuleItem::ScriptName;
		s += "(type: \"";
		s += ri->getType();
		s += "\")";
		return s;
	}
	else
	{
		return "null";
	}
}

}


/**
 * Register RuleItem in script parser.
 * @param parser Script parser.
 */
void RuleItem::ScriptRegister(ScriptParserBase* parser)
{
	{
		const auto name = std::string{ "RuleDamageType" };
		parser->registerRawPointerType<RuleDamageType>(name);
		Bind<RuleDamageType> rs = { parser, name };

		rs.add<&RuleDamageType::isDirect>("isDirect", "if this damage type affects only one target");
		rs.add<&getAoeScript>("isAreaOfEffect", "if this damage type can affect multiple targets");

		rs.add<&getResistTypeScript>("getResistType", "which damage resistance type is used for damage reduction");
		rs.add<&getRandomTypeScript>("getRandomType", "how to calculate randomized weapon damage from the weapon's power");

		rs.add<&getArmorEffectivenessScript>("getArmorEffectiveness", "how effective is a unit's armor against this damage, % (value multiplied by 100 compared to ruleset value)");
		rs.add<&getArmorIgnoreScript>("getArmorIgnore", "how many points of armor are ignored");

		rs.add<&getDamageToScript<&RuleDamageType::ToArmorPre>>("getDamageToArmorPre", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToArmor>>("getDamageToArmor", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToEnergy>>("getDamageToEnergy", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToHealth>>("getDamageToHealth", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToItem>>("getDamageToItem", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToMana>>("getDamageToMana", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToMorale>>("getDamageToMorale", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToStun>>("getDamageToStun", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToTile>>("getDamageToTile", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToTime>>("getDamageToTime", "calculated damage value multiplied by the corresponding modifier");
		rs.add<&getDamageToScript<&RuleDamageType::ToWound>>("getDamageToWound", "calculated damage value multiplied by the corresponding modifier");

		rs.add<&getRandomDamageScript>("getRandomDamage", "calculated damage value (based on weapon's power)");

		rs.addDebugDisplay<&debugDisplayScript>();
	}

	parser->registerPointerType<Mod>();

	Bind<RuleItem> ri = { parser };

	ri.addCustomConst("BT_NONE", BT_NONE);
	ri.addCustomConst("BT_FIREARM", BT_FIREARM);
	ri.addCustomConst("BT_AMMO", BT_AMMO);
	ri.addCustomConst("BT_MELEE", BT_MELEE);
	ri.addCustomConst("BT_GRENADE", BT_GRENADE);
	ri.addCustomConst("BT_PROXIMITYGRENADE", BT_PROXIMITYGRENADE);
	ri.addCustomConst("BT_MEDIKIT", BT_MEDIKIT);
	ri.addCustomConst("BT_SCANNER", BT_SCANNER);
	ri.addCustomConst("BT_MINDPROBE", BT_MINDPROBE);
	ri.addCustomConst("BT_PSIAMP", BT_PSIAMP);
	ri.addCustomConst("BT_FLARE", BT_FLARE);
	ri.addCustomConst("BT_CORPSE", BT_CORPSE);

	ri.add<&getTypeScript>("getType");

	ri.add<&RuleItem::getAccuracyAimed>("getAccuracyAimed");
	ri.add<&RuleItem::getAccuracyAuto>("getAccuracyAuto");
	ri.add<&RuleItem::getAccuracyMelee>("getAccuracyMelee");
	ri.add<&RuleItem::getAccuracyMind>("getAccuracyMind");
	ri.add<&RuleItem::getAccuracyPanic>("getAccuracyPanic");
	ri.add<&RuleItem::getAccuracySnap>("getAccuracySnap");
	ri.add<&RuleItem::getAccuracyThrow>("getAccuracyThrow");
	ri.add<&RuleItem::getAccuracyUse>("getAccuracyUse");

	ri.add<&RuleItem::getPower>("getPower", "primary power, before applying unit bonuses, random rolls or other modifiers");
	ri.add<&RuleItem::getDamageType>("getDamageType", "primary damage type");
	ri.add<&RuleItem::getMeleePower>("getMeleePower", "secondary power (gunbutt), before applying unit bonuses, random rolls or other modifiers");
	ri.add<&RuleItem::getMeleeType>("getMeleeDamageType", "secondary damage type (gunbutt)");

	ri.add<&RuleItem::getArmor>("getArmorValue");
	ri.add<&RuleItem::getWeight>("getWeight");
	ri.add<&getBattleTypeScript>("getBattleType");
	ri.add<&RuleItem::getWaypoints>("getWaypoints");
	ri.add<&RuleItem::isWaterOnly>("isWaterOnly");
	ri.add<&RuleItem::isTwoHanded>("isTwoHanded");
	ri.add<&RuleItem::isBlockingBothHands>("isBlockingBothHands");
	ri.add<&isSingleTargetScript>("isSingleTarget");
	ri.add<&hasCategoryScript>("hasCategory");

	ri.addScriptValue<BindBase::OnlyGet, &RuleItem::_scriptValues>();
	ri.addDebugDisplay<&debugDisplayScript>();
}

}
