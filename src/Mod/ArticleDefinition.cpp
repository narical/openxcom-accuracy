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

#include "ArticleDefinition.h"
#include "../Engine/Exception.h"
#include "../Mod/RuleItem.h"

namespace YAML
{
	template<>
	struct convert<OpenXcom::ArticleDefinitionRect>
	{
		static Node encode(const OpenXcom::ArticleDefinitionRect& rhs)
		{
			Node node;
			node["x"] = rhs.x;
			node["y"] = rhs.y;
			node["width"] = rhs.width;
			node["height"] = rhs.height;
			return node;
		}

		static bool decode(const Node& node, OpenXcom::ArticleDefinitionRect& rhs)
		{
			if (!node.IsMap())
				return false;

			rhs.x = node["x"].as<int>(rhs.x);
			rhs.y = node["y"].as<int>(rhs.y);
			rhs.width = node["width"].as<int>(rhs.width);
			rhs.height = node["height"].as<int>(rhs.height);
			return true;
		}
	};
}

namespace OpenXcom
{

	/**
	 * Constructor.
	 * @param type_id Article type of this instance.
	 */
	ArticleDefinition::ArticleDefinition(UfopaediaTypeId type_id) : customPalette(false), hiddenCommendation(false), _type_id(type_id), _listOrder(0)
	{
		_pages.resize(1);
	}

	/**
	 * Destructor.
	 */
	ArticleDefinition::~ArticleDefinition()
	{}

	/**
	 * Gets the article definition type. (Text, TextImage, Craft, ...)
	 * @return The type of article definition of this instance.
	 */
	UfopaediaTypeId ArticleDefinition::getType() const
	{
		return _type_id;
	}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinition::load(const YAML::Node &node, int listOrder)
	{
		id = _pages[0].title = node["id"].as<std::string>(id);
		section = node["section"].as<std::string>(section);
		_requires = node["requires"].as< std::vector<std::string> >(_requires);
		disabledBy = node["disabledBy"].as< std::vector<std::string> >(disabledBy);
		hiddenCommendation = node["hiddenCommendation"].as<bool>(hiddenCommendation);
		//_type_id = (UfopaediaTypeId)node["type_id"].as<int>(_type_id);
		_listOrder = node["listOrder"].as<int>(_listOrder);
		if (!_listOrder)
		{
			_listOrder = listOrder;
		}

		auto loadPage = [&](size_t offset, const YAML::Node &n)
		{
			if (n)
			{
				_pages[offset].title = n["title"].as<std::string>(_pages[offset].title);
				_pages[offset].text = n["text"].as<std::string>(_pages[offset].text);
				RuleItem::loadAmmoSlotChecked(_pages[offset].ammoSlot, n["ammoSlot"], id);
			}
		};

		loadPage(0, node);
		if (const YAML::Node& pagesNode = node["pages"])
		{
			if (pagesNode.IsSequence())
			{
				auto size = pagesNode.size();
				auto firstCopy = _pages[0];
				_pages.resize(std::max(size_t{ 1 }, size), firstCopy); //all new pages are copy of old first page
				for (size_t i = 0; i < size; ++i)
				{
					loadPage(i, pagesNode[i]);
				}
			}
			else
			{
				throw Exception("Unsupported type of node 'pages' for Article '" + id + "'");
			}
		}
	}

	/**
	 * Gets the list weight of the article.
	 * @return The list weight of the article.
	 */
	int ArticleDefinition::getListOrder() const
	{
		return _listOrder;
	}

	/**
	 * Constructor.
	 */
	ArticleDefinitionRect::ArticleDefinitionRect() : x(0), y(0), width(0), height(0) {}

	/**
	 * Sets the rectangle parameters in a function.
	 * @param set_x X.
	 * @param set_y Y.
	 * @param set_width Width.
	 * @param set_height Height.
	 */
	void ArticleDefinitionRect::set(int set_x, int set_y, int set_width, int set_height)
	{
		x = set_x;
		y = set_y;
		width = set_width;
		height = set_height;
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionCraft::ArticleDefinitionCraft() : ArticleDefinition(UFOPAEDIA_TYPE_CRAFT)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionCraft::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
		image_id = node["image_id"].as<std::string>(image_id);
		if (image_id.find("_CPAL") != std::string::npos)
			customPalette = true;
		rect_stats = node["rect_stats"].as<ArticleDefinitionRect>(rect_stats);
		rect_text = node["rect_text"].as<ArticleDefinitionRect>(rect_text);
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionCraftWeapon::ArticleDefinitionCraftWeapon() : ArticleDefinition(UFOPAEDIA_TYPE_CRAFT_WEAPON)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionCraftWeapon::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
		image_id = node["image_id"].as<std::string>(image_id);
		if (image_id.find("_CPAL") != std::string::npos)
			customPalette = true;
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionText::ArticleDefinitionText() : ArticleDefinition(UFOPAEDIA_TYPE_TEXT)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionText::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionTextImage::ArticleDefinitionTextImage() : ArticleDefinition(UFOPAEDIA_TYPE_TEXTIMAGE), text_width(0), align_bottom(false)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionTextImage::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
		image_id = node["image_id"].as<std::string>(image_id);
		if (image_id.find("_CPAL") != std::string::npos)
			customPalette = true;
		text_width = node["text_width"].as<int>(text_width);
		align_bottom = node["align_bottom"].as<bool>(align_bottom);
		rect_text = node["rect_text"].as<ArticleDefinitionRect>(rect_text);
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionTFTD::ArticleDefinitionTFTD() : ArticleDefinition(UFOPAEDIA_TYPE_TFTD), text_width(0)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionTFTD::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
		_type_id = (UfopaediaTypeId)(node["type_id"].as<int>(_type_id));
		image_id = node["image_id"].as<std::string>(image_id);
		if (image_id.find("_CPAL") != std::string::npos)
			customPalette = true;
		text_width = node["text_width"].as<int>(157); // 95% of these won't need to be defined, so let's give it a default
		weapon = node["weapon"].as<std::string>(weapon);
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionBaseFacility::ArticleDefinitionBaseFacility() : ArticleDefinition(UFOPAEDIA_TYPE_BASE_FACILITY)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionBaseFacility::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionItem::ArticleDefinitionItem() : ArticleDefinition(UFOPAEDIA_TYPE_ITEM)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionItem::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
		weapon = node["weapon"].as<std::string>(weapon);
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionUfo::ArticleDefinitionUfo() : ArticleDefinition(UFOPAEDIA_TYPE_UFO)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionUfo::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
	}

	/**
	 * Constructor (only setting type of base class).
	 */
	ArticleDefinitionArmor::ArticleDefinitionArmor() : ArticleDefinition(UFOPAEDIA_TYPE_ARMOR)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionArmor::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
		image_id = node["image_id"].as<std::string>(image_id);
		if (image_id.find("_CPAL") != std::string::npos)
			customPalette = true;
	}

	/**
	 * Constructor (only setting type of base class)
	 */
	ArticleDefinitionVehicle::ArticleDefinitionVehicle() : ArticleDefinition(UFOPAEDIA_TYPE_VEHICLE)
	{}

	/**
	 * Loads the article definition from a YAML file.
	 * @param node YAML node.
	 * @param listOrder The list weight for this article.
	 */
	void ArticleDefinitionVehicle::load(const YAML::Node &node, int listOrder)
	{
		ArticleDefinition::load(node, listOrder);
		image_id = node["image_id"].as<std::string>(image_id);
		if (image_id.find("_CPAL") != std::string::npos)
			customPalette = true;
		weapon = node["weapon"].as<std::string>(weapon);
	}
}
