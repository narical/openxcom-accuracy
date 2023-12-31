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
#include "UfoTrajectory.h"
#include "../Savegame/Ufo.h"

namespace YAML
{
	template<>
	struct convert<OpenXcom::TrajectoryWaypoint>
	{
		static Node encode(const OpenXcom::TrajectoryWaypoint& rhs)
		{
			Node node;
			node.push_back(rhs.zone);
			node.push_back(rhs.altitude);
			node.push_back(rhs.speed);
			return node;
		}

		static bool decode(const Node& node, OpenXcom::TrajectoryWaypoint& rhs)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;

			rhs.zone = node[0].as<int>();
			rhs.altitude = node[1].as<int>();
			rhs.speed = node[2].as<int>();
			return true;
		}
	};
}

namespace OpenXcom
{

const std::string UfoTrajectory::RETALIATION_ASSAULT_RUN = "__RETALIATION_ASSAULT_RUN";

UfoTrajectory::UfoTrajectory(const std::string &id) : _id(id), _groundTimer(5)
{
}

/**
 * Overwrites trajectory data with the data stored in @a node.
 * Only the fields contained in the node will be overwritten.
 * @param node The node containing the new values.
 */
void UfoTrajectory::load(const YAML::Node &node)
{
	if (const YAML::Node &parent = node["refNode"])
	{
		load(parent);
	}

	_groundTimer = node["groundTimer"].as<size_t>(_groundTimer);
	_waypoints = node["waypoints"].as< std::vector<TrajectoryWaypoint> >(_waypoints);
}

/**
 * Gets the altitude at a waypoint.
 * @param wp The waypoint.
 * @return The altitude.
 */
std::string UfoTrajectory::getAltitude(size_t wp) const
{
	return Ufo::ALTITUDE_STRING[_waypoints[wp].altitude];
}

}
