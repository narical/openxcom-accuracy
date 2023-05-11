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
void Country::load(const YAML::Node &node, const ScriptGlobal* shared)
{
	_funding = node["funding"].as< std::vector<int> >(_funding);
	_activityXcom = node["activityXcom"].as< std::vector<int> >(_activityXcom);
	_activityAlien = node["activityAlien"].as< std::vector<int> >(_activityAlien);
	_pact = node["pact"].as<bool>(_pact);
	_newPact = node["newPact"].as<bool>(_newPact);
	_cancelPact = node["cancelPact"].as<bool>(_cancelPact);
	_scriptValues.load(node, shared);
}

/**
 * Saves the country to a YAML file.
 * @return YAML node.
 */
YAML::Node Country::save(const ScriptGlobal* shared) const
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

	_scriptValues.save(node, shared);

	return node;

	_scriptValues.save(node, &tags);
}

/**
 * Returns the ruleset for the country's type.
 * @return Pointer to ruleset.
 */
const RuleCountry *Country::getRules() const
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
	if (_pact)
		return Satisfaction::ALIEN_PACT;
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
 * reset all the counters,
 * calculate this month's funding,
 * set the change value for the month.
 * @param xcomTotal the council's xcom score
 * @param alienTotal the council's alien score
 * @param pactScore the penalty for signing a pact
 * @param averageFunding current average funding across all countries (including withdrawn countries)
 */

void Country::newMonth(int xcomTotal, int alienTotal, int pactScore, int averageFunding, const SavedGame* save)
{
	// Note: this is a TEMPORARY variable! it's not saved in the save file, i.e. we don't know the value from the previous month!
	_satisfaction = Satisfaction::SATISFIED;
	const int funding = getFunding().back();
	const int good = (xcomTotal / 10) + _activityXcom.back();
	const int bad = (alienTotal / 20) + _activityAlien.back();
	const int oldFunding = _funding.back() / 1000;
	int newFunding = (oldFunding * RNG::generate(5, 20) / 100) * 1000;
	if (newFunding == 0)
	{
		newFunding = 1000; // increase at least by 1000
	}

	if (bad <= good + 30)
	{
		if (good > bad + 30)
		{
			if (RNG::generate(0, good) > bad)
			{
				// don't go over the cap
				int cap = getRules()->getFundingCap()*1000;
				if (funding + newFunding > cap)
					newFunding = cap - funding;
				if (newFunding)
					_satisfaction = Satisfaction::HAPPY;
			}
		}
	}
	else
	{
		if (RNG::generate(0, bad) > good)
		{
			if (newFunding)
			{
				newFunding = -newFunding;
				// don't go below zero
				if (funding + newFunding < 0)
					newFunding = 0 - funding;
				if (newFunding)
					_satisfaction = Satisfaction::UNHAPPY;
			}
		}
	}

	if (_satisfaction == Satisfaction::SATISFIED)
	{
		newFunding = 0;
	}
	if (_cancelPact)
	{
		if (oldFunding <= 0)
		{
			_satisfaction = Satisfaction::SATISFIED; // satisfied, not happy or unhappy
			newFunding = averageFunding;
		}
	}

	// call script which can adjust values.
	ModScript::NewMonthCountry::Output args{ newFunding, static_cast<int>(_satisfaction), _newPact, _cancelPact };
	ModScript::NewMonthCountry::Worker work{ this, save, xcomTotal, alienTotal };
	work.execute(_rules->getScript<ModScript::NewMonthCountry>(), args);

	newFunding = std::get<0>(args.data);
	_satisfaction = static_cast<Satisfaction>(std::get<1>(args.data));
	_newPact = static_cast<bool>(std::get<2>(args.data));
	_cancelPact = static_cast<bool>(std::get<3>(args.data));

	// form/cancel pacts
	if (_newPact)
	{
		_pact = true;
		addActivityAlien(pactScore);
	}
	else if (_cancelPact)
	{
		_pact = false;
	}

	// reset pact change states
	_newPact = false;
	_cancelPact = false;

	// set the new funding and reset the activity meters
	if (_pact)
		_funding.push_back(0); // yes, hardcoded!
	else
		_funding.push_back(funding + newFunding);

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

////////////////////////////////////////////////////////////
//					Script binding
////////////////////////////////////////////////////////////

namespace
{

std::string debugDisplayScript(const Country* c)
{
	if (c)
	{
		std::string s;
		s += Country::ScriptName;
		s += "(name: \"";
		s += c->getRules()->getType();
		s += "\")";
		return s;
	}
	else
	{
		return "null";
	}
}

} // namespace

void Country::ScriptRegister(ScriptParserBase* parser)
{
	parser->registerPointerType<RuleCountry>();

	Bind<Country> c = { parser };

	c.addRules<RuleCountry, &Country::getRules>("getRuleCountry");

	c.add<&Country::getPact>("getPact", "Get if the country has signed an alien pact or not.");

	c.add<&Country::getCurrentFunding>("getCurrentFunding", "Get the country's current funding.");
	c.add<&Country::getCurrentActivityAlien>("getCurrentActivityAlien", "Get the country's current alien activity.");
	c.add<&Country::getCurrentActivityXcom>("getCurrentActivityXcom", "Get the country's current xcom activity.");

	c.addScriptValue<&Country::_scriptValues>();
	c.addDebugDisplay<&debugDisplayScript>();

	c.addCustomConst("SATISFACTION_ALIENPACT", 0);
	c.addCustomConst("SATISFACTION_UNHAPPY", 1);
	c.addCustomConst("SATISFACTION_SATISIFIED", 2);
	c.addCustomConst("SATISFACTION_HAPPY", 3);
}

/**
 * Constructor of new month country script parser.
 * Called every new month for every country.
 */
ModScript::NewMonthCountryParser::NewMonthCountryParser(ScriptGlobal* shared, const std::string& name, Mod* mod) : ScriptParserEvents{ shared, name,
	"fundingChange", "satisfaction", "formPact", "cancelPact",
	"country", "geoscape_game", "totalXcomScore", "totalAlienScore" }
{
	BindBase b { this };

	b.addCustomPtr<const Mod>("rules", mod);
}

}
