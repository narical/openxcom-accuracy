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
#include "../Engine/Game.h"
#include "../Engine/LocalizedText.h"
#include "../Engine/Options.h"
#include "../Engine/Unicode.h"
#include "../Interface/Text.h"
#include "../Interface/TextButton.h"
#include "../Interface/TextList.h"
#include "../Interface/Window.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleUfo.h"
#include "../Mod/RuleRegion.h"
#include "../Mod/RuleCountry.h"
#include "../Savegame/AlienMission.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Ufo.h"
#include "../Savegame/Region.h"
#include "../Savegame/Country.h"
#include "GeoscapeState.h"
#include "Globe.h"
#include "InterceptState.h"
#include "HiddenAlienActivityState.h"
#include <sstream>

namespace OpenXcom
{

/**
 * Initializes all the elements in the hidden alien activity window.
 * @param game Pointer to the core game.
 * @param ufo Pointer to the UFO to get info from.
 * @param state Pointer to the Geoscape.
 * @param detected Was the UFO detected?
 * @param hyperwave Was it a hyperwave radar?
 */
HiddenAlienActivityState::HiddenAlienActivityState
(
	GeoscapeState* state,
	std::set<OpenXcom::Region*> displayHiddenAlienActivityRegions,
	std::set<OpenXcom::Country*> displayHiddenAlienActivityCountries
)
	:
	_state(state),
	_displayHiddenAlienActivityRegions(displayHiddenAlienActivityRegions),
	_displayHiddenAlienActivityCountries(displayHiddenAlienActivityCountries)
{
	_screen = false;

	// create objects

	_window = new Window(this, 224, 180, 16, 10, POPUP_BOTH);
	_txtInfo = new Text(200, 16, 28, 20);
	_txtHeaderRegions = new Text(200, 8, 28, 40);
	_lstHiddenAlienActivityRegions = new TextList(183, 40, 28, 50);
	_txtHeaderCountries = new Text(200, 8, 28, 98);
	_lstHiddenAlienActivityCountries = new TextList(183, 40, 28, 108);
	_btnOk = new TextButton(200, 12, 28, 152);
	_btnCancel = new TextButton(200, 12, 28, 168);

	// set palette

	setInterface("UFOInfo", true);

	// add elements

	add(_window, "window", "UFOInfo");
	add(_txtInfo, "text", "UFOInfo");
	add(_txtHeaderRegions, "text", "UFOInfo");
	add(_lstHiddenAlienActivityRegions, "text", "UFOInfo");
	add(_txtHeaderCountries, "text", "UFOInfo");
	add(_lstHiddenAlienActivityCountries, "text", "UFOInfo");
	add(_btnOk, "button", "UFOInfo");
	add(_btnCancel, "button", "UFOInfo");

	// set up objects

	setWindowBackground(_window, "UFOInfo");

	centerAllSurfaces();

	_txtInfo->setBig();
	_txtInfo->setText(tr("STR_HIDDEN_ALIEN_ACTIVITY"));
	_txtInfo->setAlign(ALIGN_CENTER);

	_txtHeaderRegions->setText(tr("STR_UFO_ACTIVITY_IN_AREAS"));

	_lstHiddenAlienActivityRegions->setColumns(1, 180);
	_lstHiddenAlienActivityRegions->setHighContrast(true);
	_lstHiddenAlienActivityRegions->setColor(90);

	_txtHeaderCountries->setText(tr("STR_UFO_ACTIVITY_IN_COUNTRIES"));

	_lstHiddenAlienActivityCountries->setColumns(1, 180);
	_lstHiddenAlienActivityCountries->setHighContrast(true);
	_lstHiddenAlienActivityCountries->setColor(90);

	_btnOk->setText(tr("STR_OK_5_SECONDS"));
	_btnOk->onMouseClick((ActionHandler)&HiddenAlienActivityState::btnOkClick);

	_btnCancel->setText(tr("STR_CANCEL"));
	_btnCancel->onMouseClick((ActionHandler)&HiddenAlienActivityState::btnCancelClick);
	_btnCancel->onKeyboardPress((ActionHandler)&HiddenAlienActivityState::btnCancelClick, Options::keyCancel);

	// populate lists

	for (OpenXcom::Region* const region : _displayHiddenAlienActivityRegions)
	{
		_lstHiddenAlienActivityRegions->addRow(1, tr(region->getRules()->getType()).c_str());
	}

	for (OpenXcom::Country* const country : _displayHiddenAlienActivityCountries)
	{
		_lstHiddenAlienActivityCountries->addRow(1, tr(country->getRules()->getType()).c_str());
	}

}

/**
 *
 */
HiddenAlienActivityState::~HiddenAlienActivityState()
{
}

/**
 * Returns to the previous screen.
 * @param action Pointer to an action.
 */
void HiddenAlienActivityState::btnOkClick(Action*)
{
	_state->timerReset();
	_game->popState();
}

/**
 * Returns to the previous screen.
 * @param action Pointer to an action.
 */
void HiddenAlienActivityState::btnCancelClick(Action*)
{
	_game->popState();
}

}

