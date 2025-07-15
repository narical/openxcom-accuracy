#pragma once
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
#include "../Engine/State.h"
#include <vector>

namespace OpenXcom
{

class TextButton;
class Window;
class Text;
class TextList;
class ComboBox;
class Base;
class BattleUnit;
class Craft;
class Soldier;
struct SortFunctor;

/**
 * Screen that lets the player
 * control the AI parameters for soldiers/battleunits.
 */
class SoldiersAIState : public State
{
private:
	static constexpr int noCol = 4;	//Name Rank AI-Control Aggresiveness

	TextButton *_btnOk;
	Window *_window;
	Text *_txtTitle, *_txtName, *_txtRank;
	Text *_txtControlled, *_txtAgressiveness;
	TextList *_lstUnits;

	std::vector<Soldier *> _soldiers;
	std::vector<BattleUnit *> _units;
	/// initializes the display list based on the units given in the constructor
	void initList(size_t scrl);
	/// Proxy for constructor
	void _commonConstruct();
public:
	/// Creates the Unit AI state.
	SoldiersAIState(std::vector<Soldier*>& soldiers);
	SoldiersAIState(const Craft* craft);
	/// Creates the Unit AI state in Battle.
	SoldiersAIState(std::vector<BattleUnit*>& units);
	/// Cleans up the Unit AI state.
	~SoldiersAIState();
	/// Handler for clicking the OK button.
	void btnOkClick(Action *action);
	/// Handler for clicking the Preview button.
	void btnPreviewClick(Action *action);
	/// Updates the unit list.
	void init() override;
	/// Moves a unit up.
	void moveSoldierUp(Action *action, unsigned int row, bool max = false);
	/// Moves a unit down.
	void moveSoldierDown(Action *action, unsigned int row, bool max = false);
	/// Handler for clicking the Unit list.
	void lstSoldiersClick(Action *action);

	/// Toggle AI control @ autocombat of currently selected soldier
	bool toggleAISoldier(bool leeroy = false);
	/// Toggle AI control @ autocombat of currently selected battleunit
	bool toggleAIBattleUnit(bool leeroy = false);

	/// Toggle AI aggressiveness @ autocombat of currently selected soldier/battleunit
	template <typename T>
	int toggleAgg(T* unit)
	{
		const auto newVal = (unit->getAggression() + 1) % 2;
		unit->setAggression(newVal);
		return newVal;
	}
};

}
