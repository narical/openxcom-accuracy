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

#include <sstream>
#include "../fmath.h"
#include "ArticleStateTFTD.h"
#include "ArticleStateTFTDArmor.h"
#include "../Mod/ArticleDefinition.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Engine/Game.h"
#include "../Engine/LocalizedText.h"
#include "../Interface/TextButton.h"
#include "../Engine/Unicode.h"
#include "../Interface/TextList.h"

namespace OpenXcom
{

	ArticleStateTFTDArmor::ArticleStateTFTDArmor(ArticleDefinitionTFTD *defs, std::shared_ptr<ArticleCommonState> state) : ArticleStateTFTD(defs, std::move(state)), _row(0)
	{
		const std::string HEADER_ARMOR_THICKNESS  = "- armor thickness --";
		const std::string HEADER_DAMAGE_MODIFIERS = "- damage modifiers -";
		const std::string HEADER_UNIT_STATS       = "- stats modifiers --";

		// default text and list layout when text is present

		const int DEFAULT_TEXT_HEIGHT = 72;
		const int DEFAULT_LIST_HEIGHT = 64;
		const int DEFAULT_LIST_Y = 110;

		// actual text and list heights

		int textHeight;
		int listHeight;
		int listY;
		bool showHeaders;

		// set text and list layout depending on whether text is present

		if (defs->getTextForPage(_state->current_page).empty())
		{
			textHeight = 0;
			listHeight = DEFAULT_TEXT_HEIGHT + DEFAULT_LIST_HEIGHT;
			listY = DEFAULT_LIST_Y - DEFAULT_TEXT_HEIGHT;
			showHeaders = true;
		}
		else
		{
			textHeight = DEFAULT_TEXT_HEIGHT;
			listHeight = DEFAULT_LIST_HEIGHT;
			listY = DEFAULT_LIST_Y;
			showHeaders = false;
		}

		_txtInfo->setHeight(textHeight);

		_btnInfo->setVisible(_game->getMod()->getShowPediaInfoButton());

		// get armor

		Armor *armor = _game->getMod()->getArmor(defs->id, true);

		// set list

		_lstInfo = new TextList(130, listHeight, 168, listY);
		add(_lstInfo);

		_lstInfo->setColor(_listColor1);
		_lstInfo->setColumns(3, 100, 20, 10);
		_lstInfo->setAlign(ALIGN_RIGHT, 1);
		_lstInfo->setAlign(ALIGN_RIGHT, 2);
		_lstInfo->setDot(false);
		_lstInfo->setColumnDot(0, true);

		// armor values
		// all values are always displayed

		if (showHeaders)
		{
			_lstInfo->setColumnDot(0, false);
			_lstInfo->addRow(1, HEADER_ARMOR_THICKNESS.c_str());
			++_row;
			_lstInfo->addRow(0);
			++_row;
			_lstInfo->setColumnDot(0, true);
		}

		addStat("STR_FRONT_ARMOR", armor->getFrontArmor());
		addStat("STR_LEFT_ARMOR", armor->getLeftSideArmor());
		addStat("STR_RIGHT_ARMOR", armor->getRightSideArmor());
		addStat("STR_REAR_ARMOR", armor->getRearArmor());
		addStat("STR_UNDER_ARMOR", armor->getUnderArmor());

		_lstInfo->addRow(0);
		++_row;

		// damage modifiers

		if (showHeaders)
		{
			_lstInfo->setColumnDot(0, false);
			_lstInfo->addRow(1, HEADER_DAMAGE_MODIFIERS.c_str());
			++_row;
			_lstInfo->addRow(0);
			++_row;
			_lstInfo->setColumnDot(0, true);
		}

		for (int i = 1; i < 10; ++i)
		{
			ItemDamageType dt = (ItemDamageType)i;
			int percentage = (int)Round(armor->getDamageModifier(dt) * 100.0f);
			std::string damage = getDamageTypeText(dt);

			if (percentage != 100)
			{
				addStat(damage, percentage, "%");
			}

		}

		_lstInfo->addRow(0);
		++_row;

		// unit stats
		// zero values are never displayed

		std::vector<std::pair<std::string, int> > armorStats;

		armorStats.push_back({"STR_TIME_UNITS", armor->getStats()->tu});
		armorStats.push_back({"STR_STAMINA", armor->getStats()->stamina});
		armorStats.push_back({"STR_HEALTH", armor->getStats()->health});
		armorStats.push_back({"STR_BRAVERY", armor->getStats()->bravery});
		armorStats.push_back({"STR_REACTIONS", armor->getStats()->reactions});
		armorStats.push_back({"STR_FIRING_ACCURACY", armor->getStats()->firing});
		armorStats.push_back({"STR_THROWING_ACCURACY", armor->getStats()->throwing});
		armorStats.push_back({"STR_MELEE_ACCURACY", armor->getStats()->melee});
		armorStats.push_back({"STR_STRENGTH", armor->getStats()->strength});
		armorStats.push_back({"STR_MANA_POOL", armor->getStats()->mana});
		armorStats.push_back({"STR_PSIONIC_STRENGTH", armor->getStats()->psiStrength});
		armorStats.push_back({"STR_PSIONIC_SKILL", armor->getStats()->psiSkill});

		int displayableArmorStatCount = 0;
		for (std::pair<std::string, int> armorStat : armorStats)
		{
			if (armorStat.second != 0)
			{
				displayableArmorStatCount++;
			}
		}

		if (displayableArmorStatCount > 0)
		{
			if (showHeaders)
			{
				_lstInfo->setColumnDot(0, false);
				_lstInfo->addRow(1, HEADER_UNIT_STATS.c_str());
				++_row;
				_lstInfo->addRow(0);
				++_row;
				_lstInfo->setColumnDot(0, true);
			}

			for (std::pair<std::string, int> armorStat : armorStats)
			{
				if (armorStat.second != 0)
				{
					addStat(armorStat.first, armorStat.second, "", true);
				}
			}

		}

		centerAllSurfaces();

	}

	ArticleStateTFTDArmor::~ArticleStateTFTDArmor()
	{}

	void ArticleStateTFTDArmor::addStat(const std::string& label, int stat, const std::string &unit, bool plus, bool dim)
	{
		// add + in front of positive if requested

		std::ostringstream ss;
		if (plus && stat > 0)
			ss << "+";
		ss << stat;

		// add row

		_lstInfo->addRow(3, tr(label).c_str(), ss.str().c_str(), unit.c_str());

		// set value color

		_lstInfo->setCellColor(_row, 1, (dim ? _listColor1 : _listColor2));

		// increment row

		++_row;

	}

}

