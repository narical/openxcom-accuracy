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
class Craft;
class Soldier;
struct SortFunctor;

/**
 * Select Squad screen that lets the player
 * pick the soldiers to be controlled by AI.
 */
class SoldiersAIState : public State
{
private:
	TextButton *_btnOk;
	Window *_window;
	Text *_txtTitle, *_txtName, *_txtRank;
	Text *_txtControlled;
	TextList *_lstSoldiers;

	std::vector<Soldier *> _soldiers;
	/// initializes the display list based on the soldiers given in the constructor
	void initList(size_t scrl);
	/// Proxy for constructor
	void _commonConstruct();
public:
	/// Creates the Soldiers AI state.
	SoldiersAIState(std::vector<Soldier*>& soldiers);
	SoldiersAIState(const Craft* craft);
	/// Cleans up the Soldiers AI state.
	~SoldiersAIState();
	/// Handler for clicking the OK button.
	void btnOkClick(Action *action);
	/// Handler for clicking the Preview button.
	void btnPreviewClick(Action *action);
	/// Updates the soldiers list.
	void init() override;
	/// Moves a soldier up.
	void moveSoldierUp(Action *action, unsigned int row, bool max = false);
	/// Moves a soldier down.
	void moveSoldierDown(Action *action, unsigned int row, bool max = false);
	/// Handler for clicking the Soldiers list.
	void lstSoldiersClick(Action *action);

	/// Toggle AI control @ autocombat of currently selected soldier
	bool toggleAI();
	/// Set AI control @ autocombat of currently selected soldier
	void setAI(const bool AI);
};

}
