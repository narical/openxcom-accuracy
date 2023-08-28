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
#include "SoldiersAIState.h"
#include <algorithm>
#include <climits>
#include "../Engine/Action.h"
#include "../Engine/Game.h"
#include "../Mod/Mod.h"
#include "../Engine/LocalizedText.h"
#include "../Engine/Options.h"
#include "../Interface/ComboBox.h"
#include "../Interface/TextButton.h"
#include "../Interface/Window.h"
#include "../Interface/Text.h"
#include "../Interface/TextList.h"
#include "../Menu/ErrorMessageState.h"
#include "../Savegame/Base.h"
#include "../Savegame/Craft.h"
#include "../Savegame/Soldier.h"
#include "../Mod/RuleInterface.h"
#include "../Engine/Unicode.h"

namespace OpenXcom
{

/**
 * Initializes all the elements in the Soldiers AI screen.
 * @param soldiers Soldiers to manipulate AI control
 */
SoldiersAIState::SoldiersAIState(std::vector<Soldier*>& soldiers)
		: _soldiers(soldiers)
{
	_commonConstruct();
}

/**
 * Initializes all the elements in the Soldiers AI screen.
 * @param craft Craft whose soldiers to manipulate AI control
 */
SoldiersAIState::SoldiersAIState(const Craft* craft)
{
	for (auto* soldier : *craft->getBase()->getSoldiers())
	{
		if (soldier->getCraft() == craft)
			_soldiers.push_back(soldier);
	}
	_commonConstruct();
}

/**
 * Initializes all the elements in the Soldiers AI screen.
 */
void SoldiersAIState::_commonConstruct()
{
	// Create objects
	_window = new Window(this, 320, 200, 0, 0);
	_btnOk = new TextButton(148, 16, 164, 176);
	_txtTitle = new Text(300, 17, 16, 7);
	_txtName = new Text(114, 9, 16, 32);
	_txtRank = new Text(102, 9, 122, 32);
	_txtControlled = new Text(84, 9, 220, 32);
	_lstSoldiers = new TextList(288, 128, 8, 40);

	// Set palette
	setInterface("craftSoldiers");

	add(_window, "window", "craftSoldiers");
	add(_btnOk, "button", "craftSoldiers");
	add(_txtTitle, "text", "craftSoldiers");
	add(_txtName, "text", "craftSoldiers");
	add(_txtRank, "text", "craftSoldiers");
	add(_txtControlled, "text", "craftSoldiers");
	add(_lstSoldiers, "list", "craftSoldiers");

	centerAllSurfaces();

	// Set up objects
	setWindowBackground(_window, "craftSoldiers");

	_btnOk->setText(tr("STR_OK"));
	_btnOk->onMouseClick((ActionHandler)&SoldiersAIState::btnOkClick);
	_btnOk->onKeyboardPress((ActionHandler)&SoldiersAIState::btnOkClick, Options::keyCancel);

	_txtTitle->setBig();
	_txtTitle->setText(tr("STR_SELECT_SOLDIERS_AI"));

	_txtName->setText(tr("STR_NAME_UC"));

	_txtRank->setText(tr("STR_RANK"));

	_txtControlled->setText(tr("STR_AI_CONTROLLED"));

	_lstSoldiers->setArrowColumn(188, ARROW_VERTICAL);
	_lstSoldiers->setColumns(3, 106, 98, 76);
	_lstSoldiers->setAlign(ALIGN_RIGHT, 3);
	_lstSoldiers->setSelectable(true);
	_lstSoldiers->setBackground(_window);
	_lstSoldiers->setMargin(8);
	_lstSoldiers->onMouseClick((ActionHandler)&SoldiersAIState::lstSoldiersClick, 0);
}

/**
 * cleans up dynamic state
 */
SoldiersAIState::~SoldiersAIState()
{
}


/**
 * Returns to the previous screen.
 * @param action Pointer to an action.
 */
void SoldiersAIState::btnOkClick(Action *)
{
	_game->popState();
}

/**
 * Shows the soldiers in a list at specified offset/scroll.
 */
void SoldiersAIState::initList(size_t scrl)
{
	int row = 0;
	_lstSoldiers->clearList();

	_lstSoldiers->setColumns(3, 106, 98, 76);

	for (const auto* soldier : _soldiers)
	{
		_lstSoldiers->addRow(3, soldier->getName(true, 30).c_str(), tr(soldier->getRankString()).c_str(), "");

		Uint8 color;
		if (soldier->getAllowAutoCombat())
		{
			color = _lstSoldiers->getSecondaryColor();
			_lstSoldiers->setCellText(row, 2, tr("True"));
		}
		else
		{
			color = _lstSoldiers->getColor();
			_lstSoldiers->setCellText(row, 2, tr("False"));
		}
		_lstSoldiers->setRowColor(row, color);
		row++;
	}
	if (scrl)
		_lstSoldiers->scrollTo(scrl);
	_lstSoldiers->draw();
}

/**
 * Shows the soldiers in a list.
 */
void SoldiersAIState::init()
{
	State::init();
	initList(0);
}

/**
 * Shows the selected soldier's info.
 * @param action Pointer to an action.
 */
void SoldiersAIState::lstSoldiersClick(Action *action)
{
	double mx = action->getAbsoluteXMouse();
	if (mx >= _lstSoldiers->getArrowsLeftEdge() && mx < _lstSoldiers->getArrowsRightEdge())
	{
		return;
	}
	int row = _lstSoldiers->getSelectedRow();
	if (action->getDetails()->button.button == SDL_BUTTON_LEFT)
	{
		const bool newAI = toggleAI();
		Uint8 color = _lstSoldiers->getColor();
		if (newAI)
		{
			color = _lstSoldiers->getSecondaryColor();
			_lstSoldiers->setCellText(row, 2, tr("True"));
		}
		else
		{
			_lstSoldiers->setCellText(row, 2, tr("False"));
		}
		_lstSoldiers->setRowColor(row, color);
	}
}


bool SoldiersAIState::toggleAI()
{
	Soldier *s = _soldiers.at(_lstSoldiers->getSelectedRow());
	return s->toggleAllowAutoCombat();
}

void SoldiersAIState::setAI(const bool AI)
{
	Soldier *s = _soldiers.at(_lstSoldiers->getSelectedRow());
	s->setAllowAutoCombat(AI);
}

}
