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
		Log(LOG_INFO) << _listStyle;
		// get armor

		Armor* armor = _game->getMod()->getArmor(defs->id, true);

		// default page layout

		_btnInfo->setVisible(_game->getMod()->getShowPediaInfoButton());

		// draw different list styles

		switch (_listStyle)
		{
			// default OXCE
			case 0:
			{
				_txtInfo->setHeight(72);

				_lstInfo = new TextList(150, 64, 168, 110);
				add(_lstInfo);

				_lstInfo->setColor(_listColor1);
				_lstInfo->setColumns(2, 125, 25);
				_lstInfo->setDot(true);

				// Add armor values
				addStat("STR_FRONT_ARMOR", armor->getFrontArmor());
				addStat("STR_LEFT_ARMOR", armor->getLeftSideArmor());
				addStat("STR_RIGHT_ARMOR", armor->getRightSideArmor());
				addStat("STR_REAR_ARMOR", armor->getRearArmor());
				addStat("STR_UNDER_ARMOR", armor->getUnderArmor());

				_lstInfo->addRow(0);
				++_row;

				// Add damage modifiers
				for (int i = 0; i < DAMAGE_TYPES; ++i)
				{
					ItemDamageType dt = (ItemDamageType)i;
					int percentage = (int)Round(armor->getDamageModifier(dt) * 100.0f);
					std::string damage = getDamageTypeText(dt);
					if (percentage != 100 && damage != "STR_UNKNOWN")
					{
						addStat(damage, Unicode::formatPercentage(percentage));
					}
				}

				_lstInfo->addRow(0);
				++_row;

				// Add unit stats
				addStat("STR_TIME_UNITS", armor->getStats()->tu, true);
				addStat("STR_STAMINA", armor->getStats()->stamina, true);
				addStat("STR_HEALTH", armor->getStats()->health, true);
				addStat("STR_BRAVERY", armor->getStats()->bravery, true);
				addStat("STR_REACTIONS", armor->getStats()->reactions, true);
				addStat("STR_FIRING_ACCURACY", armor->getStats()->firing, true);
				addStat("STR_THROWING_ACCURACY", armor->getStats()->throwing, true);
				addStat("STR_MELEE_ACCURACY", armor->getStats()->melee, true);
				addStat("STR_STRENGTH", armor->getStats()->strength, true);
				addStat("STR_MANA_POOL", armor->getStats()->mana, true);
				addStat("STR_PSIONIC_STRENGTH", armor->getStats()->psiStrength, true);
				addStat("STR_PSIONIC_SKILL", armor->getStats()->psiSkill, true);
			}
			break;

			case 1:
			{
				// default info and list layout when info is present

				const int DEFAULT_TEXT_HEIGHT = 72;
				const int DEFAULT_LIST_HEIGHT = 64;
				const int DEFAULT_LIST_Y = 110;

				// set actual values depending on whether info is present

				int textHeight;
				int listHeight;
				int listY;

				if (defs->getTextForPage(_state->current_page).empty())
				{
					textHeight = 0;
					listHeight = DEFAULT_TEXT_HEIGHT + DEFAULT_LIST_HEIGHT;
					listY = DEFAULT_LIST_Y - DEFAULT_TEXT_HEIGHT;
				}
				else
				{
					textHeight = DEFAULT_TEXT_HEIGHT;
					listHeight = DEFAULT_LIST_HEIGHT;
					listY = DEFAULT_LIST_Y;
				}

				_txtInfo->setHeight(textHeight);

				// set list

				_lstInfo = new TextList(130, listHeight, 168, listY);
				add(_lstInfo);

				_lstInfo->setColor(_listColor1);
				_lstInfo->setColumns(3, 100, 20, 10);
				_lstInfo->setAlign(ALIGN_RIGHT, 1);
				_lstInfo->setAlign(ALIGN_RIGHT, 2);
				_lstInfo->setDot(true);
				_lstInfo->setDotFirstColumn(true);

				// armor values
				// all values are always displayed

				addStat1("STR_FRONT_ARMOR", armor->getFrontArmor(), "");
				addStat1("STR_LEFT_ARMOR", armor->getLeftSideArmor(), "");
				addStat1("STR_RIGHT_ARMOR", armor->getRightSideArmor(), "");
				addStat1("STR_REAR_ARMOR", armor->getRearArmor(), "");
				addStat1("STR_UNDER_ARMOR", armor->getUnderArmor(), "");

				_lstInfo->addRow(0);
				++_row;

				// damage modifiers

				for (int i = 0; i < DAMAGE_TYPES; ++i)
				{
					ItemDamageType dt = (ItemDamageType)i;
					int percentage = (int)Round(armor->getDamageModifier(dt) * 100.0f);
					std::string damage = getDamageTypeText(dt);
					if (percentage != 100 && damage != "STR_UNKNOWN")
					{
						addStat1(damage, percentage, "%");
					}
				}

				_lstInfo->addRow(0);
				++_row;

				// unit stats
				// zero values are not displayed

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

				for (std::pair<std::string, int> armorStat : armorStats)
				{
					if (armorStat.second != 0)
					{
						addStat1(armorStat.first, armorStat.second, "", true);
					}
				}

			}
			break;

		}

		centerAllSurfaces();
		
	}

	ArticleStateTFTDArmor::~ArticleStateTFTDArmor()
	{}

	void ArticleStateTFTDArmor::addStat(const std::string &label, int stat, bool plus)
	{
		if (stat != 0)
		{
			std::ostringstream ss;
			if (plus && stat > 0)
				ss << "+";
			ss << stat;
			_lstInfo->addRow(2, tr(label).c_str(), ss.str().c_str());
			_lstInfo->setCellColor(_row, 1, _listColor2);
			++_row;
		}
	}

	void ArticleStateTFTDArmor::addStat(const std::string &label, const std::string &stat)
	{
		_lstInfo->addRow(2, tr(label).c_str(), stat.c_str());
		_lstInfo->setCellColor(_row, 1, _listColor2);
		++_row;
	}

	void ArticleStateTFTDArmor::addStat1(const std::string &label, int stat, const std::string &unit, bool plus)
	{
		// generate stat string and add plus sign if requested

		std::ostringstream ss;
		if (plus && stat > 0)
			ss << "+";
		ss << stat;
		std::string statString = ss.str();

		// add row

		_lstInfo->addRow(3, tr(label).c_str(), ss.str().c_str(), unit.c_str());
		_lstInfo->setCellColor(_row, 1, _listColor2);
		++_row;

	}

}
