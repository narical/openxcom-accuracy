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
#include "Country.h"
#include "../Mod/RuleCountry.h"
#include "../Mod/Mod.h"
#include "../Mod/ModScript.h"
#include "../Engine/RNG.h"
#include "../Engine/ScriptBind.h"
#include "../Savegame/SavedGame.h"

namespace OpenXcom
{

/**
 * Initializes a country of the specified type.
 * @param rules Pointer to ruleset.
 * @param gen Generate new funding.
 */
Country::Country(RuleCountry *rules, bool gen) : _rules(rules), _pact(false), _newPact(false), _cancelPact(false), _funding(0), _satisfaction(Satisfaction::SATISFIED)
{
	if (gen)
	{
		_funding.push_back(_rules->generateFunding());
	}
	_activityAlien.push_back(0);
	_activityXcom.push_back(0);
}

/**
 *
 */
Country::~Country()
{
}

/**
 * Loads the country from a YAML file.
 * @param node YAML node.
 */
void Country::load(const YAML::Node &node, const ScriptGlobal& tags)
{
	_funding = node["funding"].as< std::vector<int> >(_funding);
	_activityXcom = node["activityXcom"].as< std::vector<int> >(_activityXcom);
	_activityAlien = node["activityAlien"].as< std::vector<int> >(_activityAlien);
	_pact = node["pact"].as<bool>(_pact);
	_newPact = node["newPact"].as<bool>(_newPact);
	_cancelPact = node["cancelPact"].as<bool>(_cancelPact);

	_scriptValues.load(node, &tags);
}

/**
 * Saves the country to a YAML file.
 * @return YAML node.
 */
YAML::Node Country::save(const ScriptGlobal& tags) const
{
	YAML::Node node;
	node["type"] = _rules->getType();
	node["funding"] = _funding;
	node["activityXcom"] = _activityXcom;
	node["activityAlien"] = _activityAlien;
	if (_pact)
	{
		node["pact"] = _pact;
		if (_cancelPact)
		{
			node["cancelPact"] = _cancelPact;
		}
	}
	// Note: can have a _newPact flag, even if already has a _pact from earlier (when xcom liberates and aliens retake a country during the same month)
	if (_newPact)
	{
		node["newPact"] = _newPact;
	}
	return node;

	_scriptValues.save(node, &tags);
}

/**
 * Returns the ruleset for the country's type.
 * @return Pointer to ruleset.
 */
RuleCountry *Country::getRules() const
{
	return _rules;
}

/**
 * Returns the country's current monthly funding.
 * @return Monthly funding.
 */
std::vector<int> &Country::getFunding()
{
	return _funding;
}

/**
 * Changes the country's current monthly funding.
 * @param funding Monthly funding.
 */
void Country::setFunding(int funding)
{
	_funding.back() = funding;
}

/*
 * Gets the countries satisfaction level.
 * @return satisfaction level, 0 = alien pact, 1 = unhappy, 2 = satisfied, 3 = happy.
 */
Country::Satisfaction Country::getSatisfaction() const
{
	return _satisfaction;
}

/**
 * Adds to the country's xcom activity level.
 * @param activity how many points to add.
 */
void Country::addActivityXcom(int activity)
{
	_activityXcom.back() += activity;
}

/**
 * Adds to the country's alien activity level.
 * @param activity how many points to add.
 */
void Country::addActivityAlien(int activity)
{
	_activityAlien.back() += activity;
}

/**
 * Gets the country's xcom activity level.
 * @return activity level.
 */
std::vector<int> &Country::getActivityXcom()
{
	return _activityXcom;
}

/**
 * Gets the country's alien activity level.
 * @return activity level.
 */
std::vector<int> &Country::getActivityAlien()
{
	return _activityAlien;
}

/**
 * @brief Handles updating country for a new month. Updates funding and satisfaction, signs pacts, and updates counters.
 * @param xcomScore Global xcom score beyond the country context.
 * @param alienScore Global alien score beyond the country context. 
 * @param pactScore Extra activity/score awarded for aliens forming a pact.
 * @param save reference to the current save game context
 * @return the change in funding this month compared to last month.
*/
int Country::newMonth(int xcomScore, int alienScore, int pactScore, SavedGame& save)
{
	auto [fundingChange, newSatisfaction] = CalculateChanges(save, xcomScore, alienScore);

	// call script which can adjust values.
	ModScript::NewMonthCountry::Output args{ fundingChange, static_cast<int>(newSatisfaction), _newPact, _cancelPact };
	ModScript::NewMonthCountry::Worker work{ this, &save, xcomScore, alienScore };
	work.execute(_rules->getScript<ModScript::NewMonthCountry>(), args);

	fundingChange = std::get<0>(args.data);
	_satisfaction = static_cast<Satisfaction>(std::get<1>(args.data));
	_newPact = static_cast<bool>(std::get<2>(args.data));
	_cancelPact = static_cast<bool>(std::get<3>(args.data));

	// form/cancel pacts.
	if (_newPact)
	{
		_pact = true;
		addActivityAlien(pactScore);
	}
	else if (_cancelPact)
	{
		_pact = false;
	}

	// reset pact change states.
	_newPact = false;
	_cancelPact = false;

	// new funding is equal to last months funding + change
	_funding.push_back(_funding.back() + fundingChange);

	// reset activity meters.
	_activityAlien.push_back(0);
	_activityXcom.push_back(0);
	if (_activityAlien.size() > 12)
		_activityAlien.erase(_activityAlien.begin());
	if (_activityXcom.size() > 12)
		_activityXcom.erase(_activityXcom.begin());
	if (_funding.size() > 12)
		_funding.erase(_funding.begin());

	return fundingChange;
}

/**
 * Pure helper method to calculate country changes on new month.
 * 
 * There are five possible outcomes:
 * - in a pact or formed a pact. No funding.
 * - left a pact when last month's funding was 0. Country satisfied. Recieve average funding.
 * - xcom score exceeds alien score by 30 points, and a random roll is succesful. Country happy. Increase funding.
 * - a random roll exceeds xcom score. Country unhappy. Decrease funding.
 * - None of the above. Country satisfied. No change to funding.
 * @return change to funding and new satisfaction levels.
*/
[[nodiscard]] std::pair<int, Country::Satisfaction> Country::CalculateChanges(const OpenXcom::SavedGame& save, int xcomScore, int alienScore) const
{
	// prepare values.
	// county specific scores
	int countryXcomScore = (xcomScore / 10) + _activityXcom.back();
	int countryAlienScore = (alienScore / 20) + _activityAlien.back();

	// funding can change by 5 to 20%, in 1k increments, but always by at least 1000.
	int lastMonthFunding = _funding.back();
	int fundingPercentChange = RNG::generate(5, 20);
	int fundingChange = (lastMonthFunding / 1000 * fundingPercentChange / 100) * 1000;
	fundingChange = std::max(fundingChange, 1000);

	// forming a pact, or in a pact and not canceling this month. No funding.
	if (_newPact || (_pact && !_cancelPact))
	{
		// since next month funding is equal to lastMonthFunding + fundingChange, this will result in 0 funding next month.
		// if funding was already 0, it will still be 0.
		return { -lastMonthFunding, Satisfaction::ALIEN_PACT };
	}

	// canceling a pact and resetting country funding.
	// note that if a pact is canceled but last month's funding was not 0, this branch is not taken.
	// This can probably only happen via scripts though, which should handle the issue themselves.
	if (_cancelPact && lastMonthFunding <= 0)
	{
		// using funding appropriate to the country's rules is probably better, but this is the original logic.
		int averageCountryFunding = save.getCountryFunding() / save.getCountries()->size() / 1000 * 1000;
		return { averageCountryFunding, Satisfaction::SATISFIED };
	}

	// Country is happy. Increase funding but don't don't let total funding go over the cap.
	if (countryXcomScore > countryAlienScore + 30 && RNG::generate(0, countryXcomScore) > countryAlienScore) {
		int fundingCap = getRules()->getFundingCap() * 1000;
		if (lastMonthFunding + fundingChange > fundingCap)
		{
			fundingChange = fundingCap - lastMonthFunding;
		}

		return { fundingChange, Satisfaction::HAPPY };
	}

	// Country is unhappy. Decrease funding, but don't let total funding go below 0.
	if (RNG::generate(0, countryAlienScore) > countryXcomScore)
	{
		fundingChange = -fundingChange;
		if (lastMonthFunding + fundingChange < 0)
		{
			fundingChange = -lastMonthFunding;
		}

		return { fundingChange, Satisfaction::UNHAPPY };
	}

	// no other condition applies. Country is satisfied. No change to funding.
	return { 0, Satisfaction::SATISFIED };
}

/**
 * @return if we will sign a new pact.
 */
bool Country::getNewPact() const
{
	return _newPact;
}

/**
 * sign a new pact at month's end.
 */
void Country::setNewPact()
{
	 _newPact = true;
	 _cancelPact = false;
}

/**
 * @return if we will cancel a pact at month's end.
 */
bool Country::getCancelPact() const
{
	return _cancelPact;
}

/**
 * cancel or prevent a pact.
 */
void Country::setCancelPact()
{
	if (_pact)
	{
		// cancel an existing signed pact
		_cancelPact = true;
		_newPact = false;
	}
	else
	{
		// prevent a not-yet-signed pact
		_cancelPact = false;
		_newPact = false;
	}
}

/**
 * no setter for this one, as it gets set automatically
 * at month's end if _newPact is set.
 * @return if we have signed a pact.
 */
bool Country::getPact() const
{
	return _pact;
}

/**
 * sign a new pact.
 */
void Country::setPact()
{
	 _pact = true;
}

/**
 * can be (re)infiltrated?
 */
bool Country::canBeInfiltrated()
{
	if (!_pact && !_newPact)
	{
		// completely new infiltration; or retaking a previously liberated country
		return true;
	}
	if (_pact && _cancelPact)
	{
		// xcom tried to liberate them this month, but the aliens were not amused... who shall they listen to at the end?
		return true;
	}
	return false;
}

namespace
{

int checkBoundsAndGet(int index, const std::vector<int>& element)
{
	if (index < 0 || index > element.size()) {
		Log(LOG_WARNING) << "Attempted to get invalid data point.";
		return -1;
	}
	return element[index];
}

}

int Country::getMonthlyActivityAlien(int month) const { return checkBoundsAndGet(month, _activityAlien); }
int Country::getMonthlyActivityXcom(int month) const { return checkBoundsAndGet(month, _activityXcom); }
int Country::getMonthlyFunding(int month) const { return checkBoundsAndGet(month, _funding); }

namespace // script helpers
{

std::string debugDisplayScript(const Country* country)
{
	if (country == nullptr) { return "null"; }

	return Country::ScriptName + std::string("(name: \"") + country->getRules()->getType() + "\")";
}

} // end script helpers.

void Country::ScriptRegister(ScriptParserBase* parser)
{
	Bind<Country> countryBinder = { parser };

	countryBinder.add<&Country::getRules>("getRuleCountry", "get the immutable rules for this country.");

	countryBinder.add<&Country::getCancelPact>("getCancelPact", "Get if the pact will be canceled at the end of the month.");
	countryBinder.add<&Country::getNewPact>("getNewPact", "Get if a new pact will be signed at the end of the month.");
	countryBinder.add<&Country::getPact>("getPact", "Get if the country has signed an alien pact or not.");

	countryBinder.add<&Country::getSatisfactionInt>("getSatisfaction", "Get the countries current satisfaction level.");

	countryBinder.addCustomConst("SATISFACTION_ALIENPACT", 0);
	countryBinder.addCustomConst("SATISFACTION_UNHAPPY", 1);
	countryBinder.addCustomConst("SATISFACTION_SATISIFIED", 2);
	countryBinder.addCustomConst("SATISFACTION_HAPPY", 3);

	countryBinder.add<&Country::getCurrentFunding>("getCurrentFunding", "Get the countries current funding.");
	countryBinder.add<&Country::getCurrentActivityAlien>("getCurrentActivityAlien", "Get the countries current alien activity.");
	countryBinder.add<&Country::getCurrentActivityXcom>("getCurrentActivityXcom", "Get the countries current xcom activity.");

	countryBinder.add<&Country::getMonthsTracked>("getMonthsTracked", "Gets the number of months currently tracked. [2, 12].");
	countryBinder.add<&Country::getMonthlyFunding>("getFunding", "Gets funding for a tracked month [0 to monthsTracked), -1 on error.");
	countryBinder.add<&Country::getMonthlyActivityAlien>("getAlienActivity", "Gets alien activity for a tracked month [0 to monthsTracked), -1 on error.");
	countryBinder.add<&Country::getMonthlyActivityXcom>("getXcomActivity", "Gets xcom activity for a tracked month [0 to monthsTracked), -1 on error.");

	countryBinder.addScriptValue<BindBase::SetAndGet, &Country::_scriptValues>();
	countryBinder.addDebugDisplay<&debugDisplayScript>();
}

/**
 * Constructor of new month country script parser.
 * Called every new month for every country.
 */
ModScript::NewMonthCountryParser::NewMonthCountryParser(ScriptGlobal* shared, const std::string& name, Mod* mod) : ScriptParserEvents{ shared, name,
	"fundingChange", "satisfaction", "formPact", "cancelPact",
	"country", "save", "totalXcomScore", "totalAlienScore" }
{
	Bind<Country> countryBinder = { this };
	countryBinder.addCustomPtr<const Mod>("rules", mod);
}

}
