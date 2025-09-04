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
#include <assert.h>
#include <set>
#include "TileEngine.h"
#include "AIModule.h"
#include "Map.h"
#include "Camera.h"
#include "Projectile.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleUnitStatistics.h"
#include "../Savegame/HitLog.h"
#include "../Engine/RNG.h"
#include "../Engine/GraphSubset.h"
#include "BattlescapeState.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Unit.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Mod/RuleSkill.h"
#include "../Engine/Options.h"
#include "ProjectileFlyBState.h"
#include "MeleeAttackBState.h"
#include "../fmath.h"

namespace OpenXcom
{
namespace
{

/**
 * Calculates a line trajectory, using bresenham algorithm in 3D.
 * @param origin Origin.
 * @param target Target.
 * @param posFunc Function call for each step in primary direction of line.
 * @param driftFunc Function call for each side step of line.
 */
template<typename FuncNewPosition, typename FuncDrift>
bool calculateLineHelper(const Position& origin, const Position& target, FuncNewPosition posFunc, FuncDrift driftFunc)
{
	int x, x0, x1, delta_x, step_x;
	int y, y0, y1, delta_y, step_y;
	int z, z0, z1, delta_z, step_z;
	int swap_xy, swap_xz;
	int drift_xy, drift_xz;
	int cx, cy, cz;

	//start and end points
	x0 = origin.x;	 x1 = target.x;
	y0 = origin.y;	 y1 = target.y;
	z0 = origin.z;	 z1 = target.z;

	//'steep' xy Line, make longest delta x plane
	swap_xy = abs(y1 - y0) > abs(x1 - x0);
	if (swap_xy)
	{
		std::swap(x0, y0);
		std::swap(x1, y1);
	}

	//do same for xz
	swap_xz = abs(z1 - z0) > abs(x1 - x0);
	if (swap_xz)
	{
		std::swap(x0, z0);
		std::swap(x1, z1);
	}

	//delta is Length in each plane
	delta_x = abs(x1 - x0);
	delta_y = abs(y1 - y0);
	delta_z = abs(z1 - z0);

	//drift controls when to step in 'shallow' planes
	//starting value keeps Line centred
	drift_xy  = (delta_x / 2);
	drift_xz  = (delta_x / 2);

	//direction of line
	step_x = 1;  if (x0 > x1) {  step_x = -1; }
	step_y = 1;  if (y0 > y1) {  step_y = -1; }
	step_z = 1;  if (z0 > z1) {  step_z = -1; }

	//starting point
	y = y0;
	z = z0;

	//step through longest delta (which we have swapped to x)
	for (x = x0; ; x += step_x)
	{
		//copy position
		cx = x;	cy = y;	cz = z;

		//unswap (in reverse)
		if (swap_xz) std::swap(cx, cz);
		if (swap_xy) std::swap(cx, cy);
		if (posFunc(Position(cx, cy, cz)))
		{
			return true;
		}

		if (x == x1) break;

		//update progress in other planes
		drift_xy = drift_xy - delta_y;
		drift_xz = drift_xz - delta_z;

		//step in y plane
		if (drift_xy < 0)
		{
			y = y + step_y;
			drift_xy = drift_xy + delta_x;

			cx = x;	cz = z; cy = y;
			if (swap_xz) std::swap(cx, cz);
			if (swap_xy) std::swap(cx, cy);
			if (driftFunc(Position(cx, cy, cz)))
			{
				return true;
			}
		}

		//same in z
		if (drift_xz < 0)
		{
			z = z + step_z;
			drift_xz = drift_xz + delta_x;

			cx = x;	cz = z; cy = y;
			if (swap_xz) std::swap(cx, cz);
			if (swap_xy) std::swap(cx, cy);
			if (driftFunc(Position(cx, cy, cz)))
			{
				return true;
			}
		}
	}
	return false;
}

template<typename FuncNewPosition>
bool calculateParabolaHelper(const Position& origin, const Position& target, double curvature, const Position& delta, FuncNewPosition posFunc)
{
	double ro = Position::distance(target, origin);

	if (AreSame(ro, 0.0)) return false;

	double fi = acos((double)(target.z - origin.z) / ro);
	double te = atan2((double)(target.y - origin.y), (double)(target.x - origin.x));

	te += (delta.x / ro) / 2 * M_PI; //horizontal magic value
	fi += ((delta.z + delta.y) / ro) / 14 * M_PI * curvature; //another magic value (vertical), to make it in line with fire spread

	double zA = sqrt(ro)*curvature;
	double zK = 4.0 * zA / ro / ro;

	int x = origin.x;
	int y = origin.y;
	int z = origin.z;
	int i = 8;

	while (z > 0)
	{
		x = (int)((double)origin.x + (double)i * cos(te) * sin(fi));
		y = (int)((double)origin.y + (double)i * sin(te) * sin(fi));
		z = (int)((double)origin.z + (double)i * cos(fi) - zK * ((double)i - ro / 2.0) * ((double)i - ro / 2.0) + zA);

		if (posFunc(Position(x,y,z)))
		{
			return true;
		}

		++i;
	}

	return false;
}

/**
 * Iterate through some subset of map tiles.
 * @param save Map data.
 * @param gs Square subset of map area.
 * @param func Call back.
 */
template<typename TileFunc>
void iterateTiles(SavedBattleGame* save, MapSubset gs, TileFunc func)
{
	const auto totalSizeX = save->getMapSizeX();
	const auto totalSizeY = save->getMapSizeY();
	const auto totalSizeZ = save->getMapSizeZ();

	gs = MapSubset::intersection(gs, MapSubset{ totalSizeX, totalSizeY });
	if (gs)
	{
		for (int z = 0; z < totalSizeZ; ++z)
		{
			auto rowStart = save->getTile(Position{ gs.beg_x, gs.beg_y, z });
			for (auto stepsY = gs.size_y(); stepsY != 0; --stepsY, rowStart += totalSizeX)
			{
				auto curr = rowStart;
				for (auto stepX = gs.size_x(); stepX != 0; --stepX, curr += 1)
				{
					func(curr);
				}
			}
		}
	}
}

/**
 * Generate square subset of map using position and radius.
 * @param position Starting position.
 * @param radius Radius of area.
 * @return Subset of map.
 */
MapSubset mapArea(Position position, int radius)
{
	return { std::make_pair(position.x - radius, position.x + radius + 1), std::make_pair(position.y - radius, position.y + radius + 1) };
}

MapSubset mapAreaExpand(MapSubset gs, int radius)
{
	return { std::make_pair(gs.beg_x - radius, gs.end_x + radius), std::make_pair(gs.beg_y - radius, gs.end_y + radius) };
}



constexpr static Uint32 MaskBlockDirMul = 9;
constexpr static Uint32 MaskBlockDirOffset = MaskBlockDirMul + 1;

/**
 * Calculate byte mask that is used to access cached data.
 * @param dir Direction for 0 to 7 or -1 as no direction when we check direct up or down direction.
 * @param z Value +1 as up, -1 as down, 0 as same level
 * @return Mask corresponding given direction and level.
 */
constexpr static Uint32 selectBit(int dir, int z)
{
	return 1u << (MaskBlockDirOffset + MaskBlockDirMul * z + dir);
}

constexpr static Uint32 MaskBlockDown = selectBit(-1, -1);
constexpr static Uint32 MaskBlockUp = selectBit(-1, +1);

constexpr static Uint32 MaskFire = selectBit(+7, +1) << 1;
constexpr static Uint32 MaskSmoke =  selectBit(+7, +1) << 2;



template<typename T>
bool getBlockDir(const T& td, int dir, int z)
{
	return td.blockDir & selectBit(dir, z);
}
template<typename T>
void addBlockDir(T& td, int dir, int z, bool p)
{
	td.blockDir |= p * selectBit(dir, z);
}

template<typename T>
bool getBlockUp(const T& td)
{
	return td.blockDir & MaskBlockUp;
}
template<typename T>
void addBlockUp(T& td, bool p)
{
	td.blockDir |= p * MaskBlockUp;
}

template<typename T>
bool getBlockDown(const T& td)
{
	return td.blockDir & MaskBlockDown;
}
template<typename T>
void addBlockDown(T& td,bool p)
{
	td.blockDir |= p * MaskBlockDown;
}

template<typename T>
bool getFire(const T& td)
{
	return td.blockDir & MaskFire;
}
template<typename T>
void addFire(T& td, bool p)
{
	td.blockDir |= p * MaskFire;
}

template<typename T>
bool getSmoke(const T& td)
{
	return td.blockDir & MaskSmoke;
}
template<typename T>
void addSmoke(T& td, bool p)
{
	td.blockDir |= p * MaskSmoke;
}

template<typename T>
bool getBigWallDir(const T& td, int dir)
{
	return td.bigWall & (1u << dir);
}
template<typename T>
void addBigWallDir(T& td, int dir, bool p)
{
	td.bigWall |= p * (1u << dir);
}

////////////////////////////////////////////////////////////
//					light propagation
////////////////////////////////////////////////////////////

/**
 * Index to component of Pos
 */
enum Axis : signed char
{
	X = 0,
	Y = 1,
	Z = 2,

	AxisMax = 3,
	AxisInvalid = -1,
};

/**
 * Index to component of Box
 */
enum BoxAxis : signed char
{
	B_X = X,
	B_Y = Y,
	B_Z = Z,

	E_X = AxisMax + X,
	E_Y = AxisMax + Y,
	E_Z = AxisMax + Z,

	BoxAxisMax = AxisMax + AxisMax,
	BoxAxisInvalid = AxisInvalid,
};

/**
 * Index to vertex of box
 */
enum BoxVertex : char
{
	V_000 = 0b000,
	V_001 = 0b001,
	V_010 = 0b010,
	V_011 = 0b011,
	V_100 = 0b100,
	V_101 = 0b101,
	V_110 = 0b110,
	V_111 = 0b111,
	BoxVertexMax = 8,
};

/**
 * 3d position, similar to `Position` but optimized for uniformed processing
 */
using Pos = std::array<int, AxisMax>;

/**
 * 3d Box, effective contains two `Pos`.
 * Both ends will be visited iterated if is end point.
 * But algorithm will skip first step in iteration.
 */
using Box = std::array<int, BoxAxisMax>;

constexpr Box fromPos(Pos a, Pos b)
{
	return {
		a[X],
		a[Y],
		a[Z],
		b[X],
		b[Y],
		b[Z],
	};
}
constexpr Pos add(Pos a, Pos b)
{
	for (int i = 0; i < AxisMax; ++i)
	{
		a[i] += b[i];
	}
	return a;
}

bool isIntersectingWith(const Box& target, const Box& limit)
{
	for (int i = 0; i < AxisMax; ++i)
	{
		if ((target[i] > limit[i + AxisMax]) || (limit[i] > target[i + AxisMax]))
		{
			return false;
		}
	}

	return true;
}

/**
 * Crop first argument to second, both need not empty intersection others wise it will return wrong answer.
 * @param target
 * @param limit
 */
void intersectWithUnchecked(Box& target, const Box& limit)
{
	for (int i = 0; i < AxisMax; ++i)
	{
		target[i] = std::max(target[i], limit[i]);
		target[i + AxisMax] = std::min(target[i + AxisMax], limit[i + AxisMax]);
	}
}

void expand(Box& box)
{
	constexpr static Box diff = { -1, -1, -1, 1, 1, 1 };
	for (int i = 0; i < BoxAxisMax; ++i)
	{
		box[i] += diff[i];
	}
}

constexpr std::array diffToAxis =
{
	AxisInvalid,
	X,  // 0b001
	Y,  // 0b010
	AxisInvalid,
	Z,  // 0b100
	AxisInvalid,
	AxisInvalid,
	AxisInvalid,
};


struct ConfigSide
{
	constexpr static ConfigSide fill(BoxVertex start, BoxVertex ii, BoxVertex jj)
	{
		// `kk` is determined as vertex that lie on orthogonal line to surface defined by vertexes `start`, `ii` and `jj`.
		const auto kk = BoxVertex(start ^ (V_111 - (start ^ jj) - (start ^ ii)));

		return ConfigSide
		{
			Direction::fill(start, ii),
			Direction::fill(start, jj),
			Direction::fill(start, kk),
		};
	}

	struct Direction
	{
		Axis axis = {};
		Sint8 dir = {};
		BoxAxis first = {};
		BoxAxis last = {};

		constexpr static Sint8 Plus = +1;
		constexpr static Sint8 Minus = -1;

		constexpr static Direction fill(BoxVertex from, BoxVertex to)
		{
			const bool asc = from < to;
			const Axis axis = diffToAxis[from ^ to];

			return Direction
			{
				axis,
				asc ? Plus : Minus,
				BoxAxis(axis + (asc ? 0 : AxisMax)),
				BoxAxis(axis + (asc ? AxisMax : 0)),
			};
		}
	};

	Direction i, j, k;
};

constexpr auto SquareLoopSize = 4;
using SquareLoop = std::array<BoxVertex, SquareLoopSize>;

/**
 * Object with definition of operation order
 */
constexpr std::array propagationSequence = ([]
{
	std::array<ConfigSide, 24> s = {};

	auto up = [](BoxVertex e) -> BoxVertex
	{
		return BoxVertex(e + V_100);
	};
	auto curr = [](const SquareLoop& a, int x)
	{
		return a[x];
	};
	auto next = [](const SquareLoop& a, int x)
	{
		return a[(x + 1) % SquareLoopSize];
	};
	auto prev = [](const SquareLoop& a, int x)
	{
		return a[(x - 1 + SquareLoopSize) % SquareLoopSize];
	};

	int total = 0;

	const SquareLoop floor_loop =
	{
		V_000,
		V_001,
		V_011,
		V_010,
	};
	for (int j = 0; j < SquareLoopSize; ++j, ++total)
	{
		s[total] = ConfigSide::fill(
			curr(floor_loop, j),
			next(floor_loop, j),
			prev(floor_loop, j)
		);
	}
	for (int j = 0; j < SquareLoopSize; ++j, ++total)
	{
		s[total] = ConfigSide::fill(
			up(curr(floor_loop, j)),
			up(next(floor_loop, j)),
			up(prev(floor_loop, j))
		);
	}
	for (int i = 0; i < SquareLoopSize; ++i)
	{
		SquareLoop side =
		{
			curr(floor_loop, i),
			next(floor_loop, i),
			up(next(floor_loop, i)),
			up(curr(floor_loop, i)),
		};
		for (int j = 0; j < SquareLoopSize; ++j, ++total)
		{
			s[total] = ConfigSide::fill(
				curr(side, j),
				next(side, j),
				prev(side, j)
			);
		}
	}

	return s;
}());


template<typename F>
void iterateEdge(const Box& b, const Box& e, const ConfigSide::Direction d, F&& f)
{
	auto begin = b[d.first];
	auto end = e[d.last];
	for (int i = begin; i != end; i += d.dir)
	{
		f(i);
	}
}

template<typename F>
void iterateSide(int k, const Box& b, const Box& e, const ConfigSide c, F&& f)
{
	Pos p = {};

	p[c.k.axis] = k;
	iterateEdge(b, e, c.j,
		[&](int pj)
		{
			p[c.j.axis] = pj;
			iterateEdge(b, e, c.i,
				[&](int pi)
				{
					p[c.i.axis] = pi;
					f(p);
				}
			);
		}
	);
}

template<typename F>
void iterateSurface(Box& box, const Box& limit, F&& f)
{
	Box beginBox = box;

	expand(box);

	Box crop = box;
	Box endBox = box;

	expand(endBox);

	intersectWithUnchecked(crop, limit);
	intersectWithUnchecked(beginBox, limit);
	intersectWithUnchecked(endBox, limit);

	for (auto& c : propagationSequence)
	{
		//check if plane of side is inside of limit box
		if (box[c.k.first] == crop[c.k.first])
		{
			iterateSide(box[c.k.first], beginBox, endBox, c, f);
		}
	}
}
template<typename F>
void iterateVolume(Pos start, int eventRadius, int range, MapSubset gs, int end_z, F&& f)
{
	auto b = fromPos(start, start);
	const auto limit = Box{ gs.beg_x, gs.beg_y, 0, gs.end_x - 1, gs.end_y - 1, end_z - 1 };

	//check that make sure that `intersectWithUnchecked` is guarantee to work correctly
	if (!isIntersectingWith(b, limit))
	{
		return;
	}

	if (eventRadius == 0)
	{
		f(start);
	}
	else
	{
		//expanding range without update as it should be already updated, only edge need processing, this is why we "transfer" one step to next loop.
		range++;
		eventRadius--;
		while (eventRadius-- > 0)
		{
			expand(b);
		}
	}

	while (range-- > 0)
	{
		iterateSurface(b, limit, f);
	}
}

constexpr static int dir_max = 9;
constexpr static int dir_x[dir_max] = { 0,  0, +1, +1, +1,  0, -1, -1, -1, };
constexpr static int dir_y[dir_max] = { 0, -1, -1,  0, +1, +1, +1,  0, -1, };
constexpr static int dir_z[dir_max] = { 0,  0,  0,  0,  0,  0,  0,  0,  0, };

constexpr static int dir_level_max = 3;
constexpr static int dir_level_x[dir_level_max] = {  0,  0,  0};
constexpr static int dir_level_y[dir_level_max] = {  0,  0,  0};
constexpr static int dir_level_z[dir_level_max] = { -1,  0, +1};

constexpr auto getPosOffsetByDirections(int dir)
{
	return Pos
	{
		dir_x[dir + 1],
		dir_y[dir + 1],
		dir_z[dir + 1],
	};
}
constexpr auto getPosUpDown(int dir)
{
	return Pos
	{
		dir_level_x[dir + 1],
		dir_level_y[dir + 1],
		dir_level_z[dir + 1],
	};
}

struct DirConfig
{
	signed char level;
	signed char dir;
	Pos offset;
	unsigned int mask;
	unsigned int next;
};

constexpr bool isSameCubFace(Pos a, Pos b)
{
	for (int i = 0; i < AxisMax; ++i)
	{
		if (a[i] != 0 && a[i] == b[i])
		{
			return true;
		}
	}
	return false;
}

/**
 * Work around for lack of `constexpr std::max` in some supported compilers
 */
constexpr int getMax(int a, int b)
{
	return a > b ? a : b;
}

/**
 * Work around for lack of `constexpr std::abs` in some supported compilers
 */
constexpr int getAbs(int a)
{
	return a >= 0 ? a : -a;
}

constexpr int getChebyshevDistance(Pos a, Pos b)
{
	int dis = 0;
	for (int i = 0; i < AxisMax; ++i)
	{
		dis = getMax(getAbs(a[i]-b[i]), dis);
	}
	return dis;
}

constexpr int dir3dMax = dir_level_max * dir_max;
constexpr int dir3dStartMask = (1 << dir3dMax) - 1;

/**
 * Object with definition of directions to check
 */
constexpr auto directions = ([]
{
	auto array = std::array<DirConfig, dir3dMax>{};

	for (int i = 0; i < dir3dMax; ++i)
	{
		auto& a = array[i];
		a.level = (i / dir_max) - 1;
		a.dir = (i % dir_max) - 1;
		a.offset = add(getPosOffsetByDirections(a.dir), getPosUpDown(a.level));
		a.mask = selectBit(a.dir, a.level);
	}

	for (int i = 0; i < dir3dMax; ++i)
	{
		auto& a = array[i];
		for (int j = 0; j < dir3dMax; ++j)
		{
			const auto& b = array[j];

			if (isSameCubFace(a.offset, b.offset) && getChebyshevDistance(a.offset, b.offset) <= 1)
			{
				a.next |= b.mask;
			}
		}
	}

	return array;
}());

/**
 * Iterate through some subset of map tiles.
 * @param save Map data.
 */
template<typename TileWorkSet, typename TileBlockCache>
void iterateTilesLightMaxBound(SavedBattleGame* save, Position position, int eventRadius, int maxRange, MapSubset gsMap, TileWorkSet& work, const TileBlockCache& blockCache)
{
	if (position == TileEngine::invalid)
	{
		iterateTiles(save, gsMap,
			[&](int idx)
			{
				work[idx] = dir3dStartMask;
			}
		);

		return;
	}

	iterateTiles(save, gsMap,
		[&](int idx)
		{
			work[idx] = 0x0;
		}
	);
	iterateTiles(save, mapArea(position, eventRadius),
		[&](Tile* tile, int idx)
		{
			//TODO: on multi level maps we skip far levels to speed calculations,
			// but sometimes we could skip even more levels, for `position` and `eventRadius` we lose
			// detailed info what tiles were in reality affected,
			// in most cases most updates are limited to one level.
			if (std::abs(tile->getPosition().z - position.z) <= eventRadius)
			{
				work[idx] = dir3dStartMask;
			}
		}
	);


	const int map_mul_y = save->getMapSizeX();
	const int map_mul_z = map_mul_y * save->getMapSizeY();
	auto index = [map_mul_y, map_mul_z](Pos pp)
	{
		return pp[X] + pp[Y] * map_mul_y + pp[Z] * map_mul_z;
	};
	auto callback = [&](Pos pp)
	{
		const auto idx = index(pp);
		const auto c = work[idx];

		const auto check = c & ~getBlockDir(blockCache[idx]);
		if (check)
		{
			for (const auto& d : directions)
			{
				if (d.mask & check)
				{
					work[index(add(pp, d.offset))] |= c & d.next;
				}
			}
		}
	};


	iterateVolume(Pos{position.x, position.y, position.z}, eventRadius, maxRange, gsMap, save->getMapSizeZ(), callback);
}

} // namespace

constexpr int TileEngine::heightFromCenter[13];

constexpr Position TileEngine::invalid;
constexpr Position TileEngine::voxelTileSize;
constexpr Position TileEngine::voxelTileCenter;

/**
 * Sets up a TileEngine.
 * @param save Pointer to SavedBattleGame object.
 * @param voxelData List of voxel data.
 * @param maxViewDistance Max view distance in tiles.
 * @param maxDarknessToSeeUnits Threshold of darkness for LoS calculation.
 */
TileEngine::TileEngine(SavedBattleGame *save, Mod *mod) :
	_save(save), _voxelData(mod->getVoxelData()), _inventorySlotGround(mod->getInventoryGround()), _personalLighting(true), _cacheTile(0), _cacheTileBelow(0),
	_maxViewDistance(mod->getMaxViewDistance()), _maxViewDistanceSq(_maxViewDistance * _maxViewDistance),
	_maxVoxelViewDistance(_maxViewDistance * 16), _maxDarknessToSeeUnits(mod->getMaxDarknessToSeeUnits()),
	_maxStaticLightDistance(mod->getMaxStaticLightDistance()), _maxDynamicLightDistance(mod->getMaxDynamicLightDistance()),
	_enhancedLighting(mod->getEnhancedLighting())
{
	_blockVisibility.resize(save->getMapSizeXYZ());
	_cacheTilePos = invalid;

	if (Options::oxceTogglePersonalLightType == 2)
	{
		// persisted per campaign
		SavedGame* geosave = _save->getGeoscapeSave();
		if (geosave)
		{
			_personalLighting = geosave->getTogglePersonalLight();
		}
	}
	else if (Options::oxceTogglePersonalLightType == 1)
	{
		// persisted per battle
		_personalLighting = _save->getTogglePersonalLight();
	}

	_save->setTogglePersonalLightTemp(_personalLighting);
}

/**
 * Deletes the TileEngine.
 */
TileEngine::~TileEngine()
{

}

/**
  * Calculates sun shading for the whole terrain.
  */
void TileEngine::calculateSunShading(MapSubset gs)
{
	int power = 15 - _save->getGlobalShade();

	iterateTiles(
		_save,
		gs,
		[&](Tile* tile)
		{
			auto currLight = power;

			// At night/dusk sun isn't dropping shades blocked by roofs
			if (_save->getGlobalShade() <= 4)
			{
				int block = 0;
				int x = tile->getPosition().x;
				int y = tile->getPosition().y;
				for (int z = _save->getMapSizeZ()-1; z > tile->getPosition().z ; z--)
				{
					block += blockage(_save->getTile(Position(x, y, z)), O_FLOOR, DT_NONE);
					block += blockage(_save->getTile(Position(x, y, z)), O_OBJECT, DT_NONE, Pathfinding::DIR_DOWN);
				}
				if (block>0)
				{
					currLight -= 2;
				}
			}
			tile->addLight(currLight, LL_AMBIENT);
		}
	);
}

/// amount of light a fire generates from tile
const int fireLightPower = 15;

/// amount of light a fire generates from unit
const int unitFireLightPower = 15;

///  amount of light a fire generates from stunned unit
const int unitFireLightPowerStunned = 10;

/**
  * Recalculates lighting for the terrain: fire.
  */
void TileEngine::calculateTerrainBackground(MapSubset gs)
{
	// add lighting of fire
	iterateTiles(
		_save,
		mapAreaExpand(gs, getMaxStaticLightDistance() - 1),
		[&](Tile* tile)
		{
			auto currLight = 0;

			if (tile->getMapData(O_FLOOR))
			{
				currLight = std::max(currLight, tile->getMapData(O_FLOOR)->getLightSource());
			}
			if (tile->getMapData(O_OBJECT))
			{
				currLight = std::max(currLight, tile->getMapData(O_OBJECT)->getLightSource());
			}
			if (tile->getMapData(O_WESTWALL))
			{
				currLight = std::max(currLight, tile->getMapData(O_WESTWALL)->getLightSource());
			}
			if (tile->getMapData(O_NORTHWALL))
			{
				currLight = std::max(currLight, tile->getMapData(O_NORTHWALL)->getLightSource());
			}

			// fires
			if (tile->getFire())
			{
				currLight = std::max(currLight, unitFireLightPower);
			}

			if (currLight >= getMaxStaticLightDistance())
			{
				currLight = getMaxStaticLightDistance() - 1;
			}
			addLight(gs, tile->getPosition(), currLight, LL_FIRE);
		}
	);
}

/**
  * Recalculates lighting for the terrain: objects,items.
  */
void TileEngine::calculateTerrainItems(MapSubset gs)
{
	// add lighting of terrain
	iterateTiles(
		_save,
		mapAreaExpand(gs, getMaxDynamicLightDistance() - 1),
		[&](Tile* tile)
		{
			auto currLight = 0;

			for (const auto* bi : *tile->getInventory())
			{
				if (bi->getGlow())
				{
					currLight = std::max(currLight, bi->getGlowRange());
				}

				auto* bu = bi->getUnit();
				if (bu && bu->getFire())
				{
					currLight = std::max(currLight, unitFireLightPowerStunned);
				}
			}

			if (currLight >= getMaxDynamicLightDistance())
			{
				currLight = getMaxDynamicLightDistance() - 1;
			}
			addLight(gs, tile->getPosition(), currLight, LL_ITEMS);
		}
	);
}

/**
  * Recalculates lighting for the units.
  */
void TileEngine::calculateUnitLighting(MapSubset gs)
{
	for (BattleUnit *unit : *_save->getUnits())
	{
		if (unit->isOut())
		{
			continue;
		}

		int currLight = 0;
		// add lighting of unit
		if (unit->getFaction() == FACTION_PLAYER)
		{
			currLight = std::max(currLight, _personalLighting ? unit->getArmor()->getPersonalLightFriend() : 0);
		}
		else if (unit->getFaction() == FACTION_HOSTILE)
		{
			currLight = std::max(currLight, unit->getArmor()->getPersonalLightHostile());
		}
		else if (unit->getFaction() == FACTION_NEUTRAL)
		{
			currLight = std::max(currLight, unit->getArmor()->getPersonalLightNeutral());
		}

		const BattleItem *handWeapons[] = { unit->getLeftHandWeapon(), unit->getRightHandWeapon() };
		for (const BattleItem *w : handWeapons)
		{
			if (!w) continue;

			if (w->getGlow())
			{
				currLight = std::max(currLight, w->getGlowRange());
			}

			auto u = w->getUnit();
			if (u && u->getFire())
			{
				currLight = std::max(currLight, unitFireLightPowerStunned);
			}
		}
		// add lighting of units on fire
		if (unit->getFire())
		{
			currLight = std::max(currLight, unitFireLightPower);
		}

		if (currLight >= getMaxDynamicLightDistance())
		{
			currLight = getMaxDynamicLightDistance() - 1;
		}
		const auto size = unit->getArmor()->getSize();
		const auto pos = unit->getPosition();
		for (int x = 0; x < size; ++x)
		{
			for (int y = 0; y < size; ++y)
			{
				addLight(gs, pos + Position(x, y, 0), currLight, LL_UNITS);
			}
		}
	}
}

void TileEngine::calculateLighting(LightLayers layer, Position position, int eventRadius, bool terrianChanged)
{
	auto gsDynamic = MapSubset{ _save->getMapSizeX(), _save->getMapSizeY() };
	auto gsStatic = gsDynamic;

	if (position != invalid)
	{
		gsDynamic = mapArea(position, eventRadius + getMaxDynamicLightDistance());
		gsStatic = mapArea(position, eventRadius + getMaxStaticLightDistance());
	}

	if (terrianChanged)
	{
		iterateTiles(
			_save,
			mapArea(position, position != invalid ? eventRadius + 1 : 1000),
			[&](Tile* tile)
			{
				const auto currPos = tile->getPosition();
				const auto index = _save->getTileIndex(currPos);
				const auto mapData = tile->getMapData(O_OBJECT);
				auto &cache = _blockVisibility[index];

				cache = {};
				cache.height = -tile->getTerrainLevel();
				if (mapData)
				{
					if (mapData->getTUCost(MT_WALK) == Pathfinding::INVALID_MOVE_COST)
					{
						cache.height = 24;
					}
				}
				addSmoke(cache, tile->getSmoke() > 0);
				addFire(cache, tile->getFire() > 0);
				addBlockUp(cache, verticalBlockage(tile, _save->getAboveTile(tile), DT_NONE) > 127);
				addBlockDown(cache, verticalBlockage(tile, _save->getBelowTile(tile), DT_NONE) > 127);
				for (int dir = 0; dir < 8; ++dir)
				{
					Position pos = {};
					Pathfinding::directionToVector(dir, &pos);
					auto tileNext = _save->getTile(currPos + pos);
					auto result = 0;

					result = horizontalBlockage(tile, tileNext, DT_NONE, true);
					addBigWallDir(cache, dir, (result == -1));

					result = horizontalBlockage(tile, tileNext, DT_NONE);
					addBlockDir(cache, dir, 0, (result > 127 || result == -1));

					tileNext = _save->getTile(currPos + pos + Position{ 0, 0, 1 });
					addBlockDir(cache, dir, 1, verticalBlockage(tile, tileNext, DT_NONE) > 127);

					tileNext = _save->getTile(currPos + pos + Position{ 0, 0, -1 });
					addBlockDir(cache, dir, -1, verticalBlockage(tile, tileNext, DT_NONE) > 127);
				}
			}
		);
	}

	if (layer <= LL_FIRE)
	{
		iterateTiles(
			_save,
			gsStatic,
			[&](Tile* tile)
			{
				tile->resetLightMulti(layer);
			}
		);
	}

	iterateTiles(
		_save,
		gsDynamic,
		[&](Tile* tile)
		{
			tile->resetLightMulti(std::max(layer, LL_ITEMS));
		}
	);

	if (layer <= LL_AMBIENT) calculateSunShading(gsStatic);
	if (layer <= LL_FIRE) calculateTerrainBackground(gsStatic);
	if (layer <= LL_ITEMS) calculateTerrainItems(gsDynamic);
	if (layer <= LL_UNITS) calculateUnitLighting(gsDynamic);
}

/**
 * Adds circular light pattern starting from center and losing power with distance travelled.
 * @param center Center.
 * @param power Power.
 * @param layer Light is separated in 4 layers: Ambient, Tiles, Items, Units.
 */
void TileEngine::addLight(MapSubset gs, Position center, int power, LightLayers layer)
{
	if (power <= 0)
	{
		return;
	}

	const auto fire = layer == LL_FIRE;
	const auto items = layer == LL_ITEMS;
	const auto units = layer == LL_UNITS;
	const auto ground = items || fire;
	const auto tileHeight = _save->getTile(center)->getTerrainLevel();
	const auto divide = (fire ? 8 : 4);
	const auto accuracy = TileEngine::voxelTileSize / divide;
	const auto offsetCenter = (accuracy / 2 + Position(-1, -1, (ground ? 0 : accuracy.z/4) - tileHeight * accuracy.z / 24));
	const auto offsetTarget = (accuracy / 2 + Position(-1, -1, 0));
	const auto clasicLighting = !(getEnhancedLighting() & ((fire ? 1 : 0) | (items ? 2 : 0) | (units ? 4 : 0)));
	const auto topTargetVoxel = static_cast<Sint16>(_save->getMapSizeZ() * accuracy.z - 1);
	const auto topCenterVoxel = static_cast<Sint16>((getBlockUp(_blockVisibility[_save->getTileIndex(center)]) ? (center.z + 1) : _save->getMapSizeZ()) * accuracy.z - 1);
	const auto maxFirePower = std::min(15, getMaxStaticLightDistance() - 1);

	iterateTiles(
		_save,
		MapSubset::intersection(gs, mapArea(center, power - 1)),
		[&](Tile* tile)
		{
			const auto target = tile->getPosition();
			const auto diff = target - center;
			const auto distance = (int)Round(Position::distance(target.toVoxel(), center.toVoxel()) / Position::TileXY);
			const auto targetLight = tile->getLightMulti(layer);
			auto currLight = power - distance;

			if (currLight <= targetLight)
			{
				return;
			}
			if (clasicLighting)
			{
				tile->addLight(currLight, layer);
				return;
			}

			Position startVoxel = (center * accuracy) + offsetCenter;
			Position endVoxel = (target * accuracy) + offsetTarget + Position(0, 0, std::max(0, (_blockVisibility[_save->getTileIndex(target)].height - 1) / (2 * divide)));
			Position offsetA{ 1, 0, 0 };
			Position offsetB{ -1, 1, 0 };
			if ((diff.x > 0) ^ (diff.y > 0))
			{
				offsetA = { 1, 1, 0 };
				offsetB = { -1, -1, 0 };
			}

			startVoxel += offsetA;
			endVoxel += offsetA;
			Position lastTileA = center;
			Position lastTileB = center;
			auto stepsA = 0;
			auto stepsB = 0;
			auto lightA = currLight;
			auto lightB = currLight;

			//Do not peek out your head outside map
			startVoxel.z = std::min(startVoxel.z, topCenterVoxel);
			endVoxel.z = std::min(endVoxel.z, topTargetVoxel);

			auto calculateBlock = [&](Position point, Position &lastPoint, int &light, int &steps)
			{
				const auto height = (point.z % accuracy.z) * divide;
				point = point / accuracy;
				if (light <= 0)
				{
					return true;
				}
				if (point == lastPoint)
				{
					return false;
				}

				const auto difference = point - lastPoint;
				const auto dir = Pathfinding::vectorToDirection(difference);
				const auto& cache = _blockVisibility[_save->getTileIndex(lastPoint)];

				auto result = getBlockDir(cache, dir, difference.z);
				if (result && difference.z == 0 && getBigWallDir(cache, dir))
				{
					if (point == target)
					{
						result = false;
					}
				}

				if (steps > 1)
				{
					if (getFire(cache) && fire && light <= maxFirePower) //some tile on path have fire, skip further calculation because destination tile should be lighted by this fire.
					{
						result = true;
					}
					else if (getSmoke(cache))
					{
						light -= 1;
					}
					if (height < cache.height)
					{
						light -= 2;
					}
				}
				++steps;
				lastPoint = point;
				if (result || light < targetLight)
				{
					light = 0;
					return true;
				}
				return false;
			};

			calculateLineHelper(startVoxel, endVoxel,
				[&](Position voxel)
				{
					auto resultA = calculateBlock(voxel, lastTileA, lightA, stepsA);
					auto resultB = calculateBlock(voxel + offsetB, lastTileB, lightB, stepsB);
					return resultA && resultB;
				},
				[&](Position voxel)
				{
					return false;
				}
			);

			currLight = (lightA + lightB) / 2;
			if (currLight > targetLight)
			{
				tile->addLight(currLight, layer);
			}
		}
	);
}

/**
 * Setups the internal event visibility search space reduction system. This system defines a narrow circle sector around
 * a given event as viewed from an external observer. This allows narrowing down which tiles/units may need to be updated for
 * the observer based on the event affecting visibility at the event itself and beyond it in its direction.
 * Imagines a circle around the event of eventRadius, calculates its tangents, and places points at the circle's tangent
 * intersections for later bounds checking.
 * @param observerPos Position of the observer of this event.
 * @param eventPos The centre of the event. Ie a moving unit's position, centre of explosion, a single destroyed tile, etc.
 * @param eventRadius Radius big enough to fully envelop the event. Ie for a single tile change, set radius to 1.
 * @return true if area is unlimited.
 *
*/
bool TileEngine::setupEventVisibilitySector(const Position &observerPos, const Position &eventPos, const int &eventRadius)
{
	if (eventRadius == 0 || eventPos == Position(-1, -1, -1) || Position::distance2dSq(observerPos, eventPos) <= eventRadius * eventRadius)
	{
		_eventVisibilityObserverPos = Position{ -1, -1, -1 };
		return true;
	}
	else
	{
		//Use search space reduction by updating within a narrow circle sector covering the event and any
		//units beyond it (they can now be hidden or revealed based on what occurred at the event position)
		//So, with a circle at eventPos of radius eventRadius, define its tangent points as viewed from this unit.
		Position posDiff = eventPos - observerPos;
		float a = asinf(eventRadius / sqrtf(posDiff.x * posDiff.x + posDiff.y * posDiff.y));
		float b = atan2f(posDiff.y, posDiff.x);
		float t1 = b - a;
		float t2 = b + a;
		//Define the points where the lines tangent to the circle intersect it. Note: resulting positions are relative to observer, not in direct tile space.
		_eventVisibilitySectorL.x = roundf(eventPos.x + eventRadius * sinf(t1)) - observerPos.x;
		_eventVisibilitySectorL.y = roundf(eventPos.y - eventRadius * cosf(t1)) - observerPos.y;
		_eventVisibilitySectorR.x = roundf(eventPos.x - eventRadius * sinf(t2)) - observerPos.x;
		_eventVisibilitySectorR.y = roundf(eventPos.y + eventRadius * cosf(t2)) - observerPos.y;
		_eventVisibilityObserverPos = observerPos;
		return false;
	}
}

/**
 * Checks whether toCheck is within a previously setup eventVisibilitySector. See setupEventVisibilitySector(...).
 * May be used to rapidly reduce the search space when updating unit and tile visibility.
 * @param toCheck The position to check.
 * @return true if within the circle sector.
 */
inline bool TileEngine::inEventVisibilitySector(const Position &toCheck) const
{
	if (_eventVisibilityObserverPos != Position{ -1, -1, -1 })
	{
		Position posDiff = toCheck - _eventVisibilityObserverPos;
		//Is toCheck within the arc as defined by the two tangent points?
		return (!(-_eventVisibilitySectorL.x * posDiff.y + _eventVisibilitySectorL.y * posDiff.x > 0) &&
			(-_eventVisibilitySectorR.x * posDiff.y + _eventVisibilitySectorR.y * posDiff.x > 0));
	}
	else
	{
		return true;
	}
}

/**
* Updates line of sight of a single soldier in a narrow arc around a given event position.
* @param unit Unit to check line of sight of.
* @param eventPos The centre of the event which necessitated the FOV update. Used to optimize which tiles to update.
* @param eventRadius The radius of a circle able to fully encompass the event, in tiles. Hence: 1 for a single tile event.
* @return True when new aliens are spotted.
*/
bool TileEngine::calculateUnitsInFOV(BattleUnit* unit, const Position eventPos, const int eventRadius)
{
	size_t oldNumVisibleUnits = unit->getUnitsSpottedThisTurn().size();
	bool useTurretDirection = false;
	if (Options::strafe && (unit->getTurretType() > -1)) {
		useTurretDirection = true;
	}

	if (unit->isOut())
		return false;

	Position posSelf = unit->getPosition();
	if (setupEventVisibilitySector(posSelf, eventPos, eventRadius))
	{
		//Asked to do a full check. Or the event is overlapping our tile. Better check everything.
		unit->clearVisibleUnits();
	}

	//Loop through all units specified and figure out which ones we can actually see.
	for (auto* bu : *_save->getUnits())
	{
		Position posOther = bu->getPosition();
		if (!bu->isOut() && (unit->getId() != bu->getId()))
		{
			int sizeOther = bu->getArmor()->getSize();
			int totalUnitTiles = 0;
			int unitTilesNotInViewSector = 0;
			bool visibilityChecked = false;
			bool visibilityStatus = false;

			for (int x = 0; x < sizeOther; ++x)
			{
				for (int y = 0; y < sizeOther; ++y)
				{
					totalUnitTiles++;
					Position posToCheck = posOther + Position(x, y, 0);
					//If we can now find any unit within the arc defined by the event tangent points, its visibility may have been affected by the event.
					if (inEventVisibilitySector(posToCheck))
					{
						if (!unit->checkViewSector(posToCheck, useTurretDirection))
						{
							//Unit within arc, but not in view sector. If it just walked out we need to remove it.
							unitTilesNotInViewSector++;
						}
						else if ( visibilityChecked ? visibilityStatus : visible(unit, _save->getTile(posToCheck))) // (distance is checked here)
						{
							visibilityChecked = true;
							visibilityStatus = true;

							//Unit (or part thereof) visible to one or more eyes of this unit.
							if (unit->getFaction() == FACTION_PLAYER)
							{
								bu->setVisible(true);
							}
							if ((bu->getFaction() == FACTION_HOSTILE && unit->getFaction() != FACTION_HOSTILE)
								|| ( (bu->getFaction() != FACTION_HOSTILE && unit->getFaction() == FACTION_HOSTILE ))
								&& !unit->hasVisibleUnit(bu))
							{
								unit->addToVisibleUnits(bu);
								unit->addToVisibleTiles(bu->getTile());
								unit->addToLofTiles(bu->getTile());
								bu->setTileLastSpotted(_save->getTileIndex(bu->getPosition()), unit->getFaction());
								bu->setTileLastSpotted(_save->getTileIndex(bu->getPosition()), unit->getFaction(), true);
								bu->setTurnsSinceSeen(0, unit->getFaction());
								bu->setTurnsSinceSpotted(0);
								bu->setTurnsLeftSpottedForSnipers(std::max(unit->getSpotterDuration(), bu->getTurnsLeftSpottedForSnipers())); // defaults to 0 = no information given to snipers
							}

							if (unit->getFaction() != bu->getFaction())
							{
								bu->setTurnsSinceSpottedByFaction(unit->getFaction(), 0);
								bu->setTurnsLeftSpottedForSnipersByFaction(
									unit->getFaction(),
									std::max(unit->getSpotterDuration(), bu->getTurnsLeftSpottedForSnipersByFaction(unit->getFaction()))
								); // defaults to 0 = no information given to snipers
							}

							x = y = sizeOther; //If a unit's tile is visible there's no need to check the others: break the loops.
						}
						else
						{
							visibilityChecked = true;
							visibilityStatus = false;

							//Within arc, but not visible. Need to check to see if whatever happened at eventPos blocked a previously seen unit.
							unitTilesNotInViewSector++;
						}
					}
				}
			}
			if (unitTilesNotInViewSector == totalUnitTiles)
			{
				unit->removeFromVisibleUnits(bu);
			}
		}
	}
	if(unit->getUnitsSpottedThisTurn().size() != oldNumVisibleUnits)
	{
		unit->checkForReactivation(_save);
	}

	// we only react when there are at least the same amount of visible units as before AND the checksum is different
	// this way we stop if there are the same amount of visible units, but a different unit is seen
	// or we stop if there are more visible units seen
	if (unit->getUnitsSpottedThisTurn().size() > oldNumVisibleUnits && !unit->getVisibleUnits()->empty())
	{
		return true;
	}
	return false;
}

/**
* Calculates line of sight of tiles for a player controlled soldier.
* If supplied with an event position differing from the soldier's position, it will only
* calculate tiles within a narrow arc.
* @param unit Unit to check line of sight of.
* @param eventPos The centre of the event which necessitated the FOV update. Used to optimize which tiles to update.
* @param eventRadius The radius of a circle able to fully encompass the event, in tiles. Hence: 1 for a single tile event.
*/
void TileEngine::calculateTilesInFOV(BattleUnit* unit, const Position eventPos, const int eventRadius)
{
	bool useTurretDirection = false;
	bool skipNarrowArcTest = false;
	int direction;
	if (Options::strafe && (unit->getTurretType() > -1))
	{
		direction = unit->getTurretDirection();
		useTurretDirection = true;
	}
	else
	{
		direction = unit->getDirection();
	}
	if (eventRadius == 1 && !unit->checkViewSector(eventPos, useTurretDirection))
	{
		// The event wasn't meant for us and/or visible for us.
		return;
	}
	else if (unit->isOut())
	{
		unit->clearVisibleTiles();
		return;
	}
	Position posSelf = unit->getPosition();
	if (setupEventVisibilitySector(posSelf, eventPos, eventRadius))
	{
		// Asked to do a full check. Or unit within event. Should update all.
		unit->clearVisibleTiles();
		skipNarrowArcTest = true;
	}

	// Only recalculate bresenham lines to tiles that are at the event or further away.
	const int distanceSqrMin = skipNarrowArcTest ? 0 : std::max(Position::distance2dSq(posSelf, eventPos) - eventRadius * eventRadius, 0);

	// Variables for finding the tiles to test based on the view direction.
	Position posTest;
	std::vector<Position> _trajectory;
	bool swap = (direction == 0 || direction == 4);
	const int signX[8] = {+1, +1, +1, +1, -1, -1, -1, -1};
	const int signY[8] = {-1, -1, -1, +1, +1, +1, -1, -1};
	int y1, y2;

	if ((unit->getHeight() + unit->getFloatHeight() + -_save->getTile(unit->getPosition())->getTerrainLevel()) >= 24 + 4)
	{
		Tile* tileAbove = _save->getTile(posSelf + Position(0, 0, 1));
		if (tileAbove && tileAbove->hasNoFloor(0))
		{
			++posSelf.z;
		}
	}
	// Test all tiles within view cone for visibility.
	for (int x = 0; x <= getMaxViewDistance(); ++x) // TODO: Possible improvement: find the intercept points of the arc at max view distance and choose a more intelligent sweep of values when an event arc is defined.
	{
		if (direction & 1)
		{
			y1 = 0;
			y2 = getMaxViewDistance();
		}
		else
		{
			y1 = -x;
			y2 = x;
		}
		for (int y = y1; y <= y2; ++y) // TODO: Possible improvement: find the intercept points of the arc at max view distance and choose a more intelligent sweep of values when an event arc is defined.
		{
			const int distanceSqr = x * x + y * y;
			if (distanceSqr <= getMaxViewDistanceSq() && distanceSqr >= distanceSqrMin)
			{
				posTest.x = posSelf.x + signX[direction] * (swap ? y : x);
				posTest.y = posSelf.y + signY[direction] * (swap ? x : y);
				// Only continue if the column of tiles at (x,y) is within the narrow arc of interest (if enabled)
				if (inEventVisibilitySector(posTest))
				{
					for (int z = 0; z < _save->getMapSizeZ(); z++)
					{
						posTest.z = z;

						if (_save->getTile(posTest)) // inside map?
						{
							// this sets tiles to discovered if they are in LOS - tile visibility is not calculated in voxelspace but in tilespace
							// large units have "4 pair of eyes"
							int size = unit->getArmor()->getSize();
							for (int xo = 0; xo < size; xo++)
							{
								for (int yo = 0; yo < size; yo++)
								{
									Position poso = posSelf + Position(xo, yo, 0);
									_trajectory.clear();
									int tst = calculateLineTile(poso, posTest, _trajectory);
									if (tst > 127)
									{
										// Vision impacted something before reaching posTest. Throw away the impact point.
										_trajectory.pop_back();
									}
									// Reveal all tiles along line of vision. Note: needed due to width of bresenham stroke.
									for (const auto& posVisited : _trajectory)
									{
										// Add tiles to the visible list only once. BUT we still need to calculate the whole trajectory as
										//  this bresenham line's period might be different from the one that originally revealed the tile.
										if (!unit->hasVisibleTile(_save->getTile(posVisited)))
										{
											unit->addToVisibleTiles(_save->getTile(posVisited));
											if (unit->getFaction() == FACTION_PLAYER)
											{
												_save->getTile(posVisited)->setVisible(+1);
												_save->getTile(posVisited)->setDiscovered(true, O_FLOOR);

												// walls to the east or south of a visible tile, we see that too
												Tile* t = _save->getTile(Position(posVisited.x + 1, posVisited.y, posVisited.z));
												if (t)
													t->setDiscovered(true, O_WESTWALL);
												t = _save->getTile(Position(posVisited.x, posVisited.y + 1, posVisited.z));
												if (t)
													t->setDiscovered(true, O_NORTHWALL);
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

/**
* Recalculates line of sight of a soldier.
* @param unit Unit to check line of sight of.
* @param doTileRecalc True (default) to recalculate the visible tiles for this unit.
* @param doUnitRecalc True (default) to recalculate the visible units for this unit.
* @return True when new aliens are spotted.
*/
bool TileEngine::calculateFOV(BattleUnit *unit, bool doTileRecalc, bool doUnitRecalc)
{
	//Force a full FOV recheck for this unit.
	if (doTileRecalc) calculateTilesInFOV(unit);
	return doUnitRecalc ? calculateUnitsInFOV(unit) : false;
}

/**
 * Gets the origin voxel of a unit's eyesight
 * @param currentUnit The watcher.
 * @return Approximately an eyeball voxel.
 */
Position TileEngine::getSightOriginVoxel(BattleUnit *currentUnit, Tile *tileTarget, BattleActionOrigin relOrigin)
{
	const auto pos = currentUnit->getPosition();
	auto* tile = currentUnit->getTile();

	// determine the origin and target voxels for the raytrace
	Position originVoxel;
	originVoxel = pos.toVoxel() + Position(8, 8, 0);
	originVoxel.z += -tile->getTerrainLevel();
	originVoxel.z += currentUnit->getHeight() + currentUnit->getFloatHeight() - 1; //one voxel lower (eye level)
	Tile *tileAbove = _save->getAboveTile(tile);
	if (currentUnit->isBigUnit())
	{
		originVoxel.x += 8;
		originVoxel.y += 8;
		originVoxel.z += 1; //topmost voxel
	}
	if (originVoxel.z >= (pos.z + 1)*Position::TileZ && (!tileAbove || !tileAbove->hasNoFloor(0)))
	{
		while (originVoxel.z >= (pos.z + 1)*Position::TileZ)
		{
			originVoxel.z--;
		}
	}

	if (Options::battleRealisticAccuracy &&
		Options::oxceEnableOffCentreShooting &&
		tileTarget)
	{
		// Adjuct target tile to the centre of unit
		Position adjustedPos;
		BattleUnit *targetUnit = tileTarget->getUnit();
		if (targetUnit)
		{
			int targetSize = targetUnit->getArmor()->getSize();
			Position targetVoxel = targetUnit->getPosition().toVoxel() + Position(8*targetSize, 8*targetSize, 0);
			adjustedPos = targetVoxel.toTile();
		}

		int direction = getDirectionTo(pos, adjustedPos);
		int unitSize = currentUnit->getArmor()->getSize();
		originVoxel.x = pos.toVoxel().x;
		originVoxel.y = pos.toVoxel().y;

		const int dirXshift[8] = {5, 6, 8,10,11,10, 8, 6};
		const int dirYshift[8] = {8, 6, 5, 6, 8,10,11,10};

		// Offset for different relative origin values
		switch (relOrigin)
		{
		case BattleActionOrigin::CENTRE:
			originVoxel.x += 8 * unitSize;
			originVoxel.y += 8 * unitSize;
			break;

		case BattleActionOrigin::LEFT:
			originVoxel.x += dirXshift[ direction ] * unitSize;
			originVoxel.y += dirYshift[ direction ] * unitSize;
			break;

		case BattleActionOrigin::RIGHT:
			direction = (direction + 4) % 8;
			originVoxel.x += dirXshift[ direction ] * unitSize;
			originVoxel.y += dirYshift[ direction ] * unitSize;
			break;
		};
	}

	return originVoxel;
}

namespace
{

/**
 * Calculate max visible distance.
 * @param te TileEngine
 * @param tile Target tile that is look at
 * @param currentUnit Unit that look on tile or unit
 * @param targetUnit Unit that is look at
 * @return Tuple of get<0>: effective visible distance that consider camouflage and shade, get<1>: max unit visibility distance in tiles independent of target darkness
 */
std::tuple<int, int> getVisibleDistanceMaxHelper(TileEngine* te, const Tile* tile, const BattleUnit* currentUnit, const BattleUnit *targetUnit)
{
	bool targetIsDark = tile->getShade() > te->getMaxDarknessToSeeUnits();
	bool targetOnFire = (targetUnit && targetUnit->getFire() > 0);
	if (targetOnFire)
	{
		// Note: fire cancels enemy's camouflage
		targetUnit = nullptr;
		targetIsDark = false;
	}

	const int viewDistanceAtDarkTiles = currentUnit->getMaxViewDistanceAtDark(targetUnit);
	const int viewDistanceAtDayTiles = currentUnit->getMaxViewDistanceAtDay(targetUnit);

	// global max distance, independent of unit
	const int visibleDistanceGlobalMaxVoxel = te->getMaxVoxelViewDistance();
	// max distance, affected by target unit too
	int visibleDistanceMaxVoxel = visibleDistanceGlobalMaxVoxel;
	// unit max distance, mix of dark and day range
	int visibleDistanceUnitMaxTile = std::min(
		te->getMaxViewDistance(),
		std::max(
			viewDistanceAtDarkTiles,
			viewDistanceAtDayTiles
		)
	);

	// during dark aliens can see 20 tiles, xcom can see 9 by default... unless overridden by armor
	if (targetIsDark)
	{
		visibleDistanceMaxVoxel = std::min(
			visibleDistanceGlobalMaxVoxel,
			viewDistanceAtDarkTiles * Position::TileXY
		);
	}
	// during day (or if enough other light) both see 20 tiles ... unless overridden by armor
	else
	{
		visibleDistanceMaxVoxel = std::min(
			visibleDistanceGlobalMaxVoxel,
			viewDistanceAtDayTiles * Position::TileXY
		);
	}

	// small buffer that allow for very short visibility distance still work in smoke or some diagonal directions still be visible
	visibleDistanceMaxVoxel += Position::TileXY / 4;

	return std::make_tuple(visibleDistanceMaxVoxel, visibleDistanceUnitMaxTile);
}

/**
 * Get data for given trajectory
 * @param te TileEngine
 * @param save SavedBattleGame for
 * @param currentUnit Unit that look on tile or unit
 * @param originVoxel Start trajectory voxel
 * @param scanVoxel End trajectory voxel
 * @return Tuple of get<0>: visibleDistanceVoxels, get<1>: densityOfSmoke, get<2>: densityOfFire
 */
std::tuple<int, int, int, int, int> getTrajectoryDataHelper(TileEngine* te, const SavedBattleGame* save, const BattleUnit* currentUnit, Position originVoxel, Position scanVoxel)
{
	std::vector<Position> _trajectory;

	// predict used distance to avoid multiple allocations
	Position diff = (originVoxel - scanVoxel);
	_trajectory.reserve(std::max({std::abs(diff.x), std::abs(diff.y), std::abs(diff.z)}) + 1);

	// now check if we really see it taking into account smoke tiles
	// initial smoke "density" of a smoke grenade is around 15 per tile
	// we do density/3 to get the decay of visibility
	// so in fresh smoke we should only have 4 tiles of visibility
	// this is traced in voxel space, with smoke affecting visibility every step of the way
	te->calculateLineVoxel(originVoxel, scanVoxel, true, &_trajectory, const_cast<BattleUnit*>(currentUnit));
	const int trajectorySize = _trajectory.size();
	float densityOfSmoke = 0;
	float densityOfFire = 0;
	float densityOfSmokeNearUnit = 0;
	float densityOfFireeNearUnit = 0;
	float visibleDistanceVoxels = 0;
	Position trackTile(-1, -1, -1);
	const Tile *t = 0;

	for (int i = 0; i < trajectorySize; i++)
	{
		auto posTile =  _trajectory.at(i).toTile();
		auto step = te->trajectoryStepSize(_trajectory, i);
		if (trackTile != posTile)
		{
			trackTile = posTile;
			t = save->getTile(trackTile);
		}
		if (t == nullptr)
			continue;
		visibleDistanceVoxels += step;
		if (t->getFire() == 0)
		{
			densityOfSmoke += step * t->getSmoke();
		}
		else
		{
			densityOfFire += step * t->getSmoke(); // this boost fire blocking visibility for thermo vision as usually smoke value is bigger
		}
		if (visibleDistanceVoxels < Position::TileXY*2)
		{
			if (t->getFire() == 0)
			{
				densityOfSmokeNearUnit += step * t->getSmoke();
			}
			else
			{
				densityOfFireeNearUnit += step * t->getSmoke(); // this boost fire blocking visibility for thermo vision as usually smoke value is bigger
			}
		}
	}

	return std::make_tuple((int)visibleDistanceVoxels, (int)densityOfSmoke, (int)densityOfFire, (int)densityOfSmokeNearUnit, (int)densityOfFireeNearUnit);
}

}


/**
 * Checks for an opposing unit on this tile.
 * @param currentUnit The watcher.
 * @param tile The tile to check for
 * @return True if visible.
 */
bool TileEngine::visible(BattleUnit *currentUnit, Tile *tile)
{
	// if there is no tile or no unit, we can't see it
	if (!tile || !tile->getUnit())
	{
		return false;
	}

	// friendlies are always seen
	if (currentUnit->getFaction() == tile->getUnit()->getFaction()) return true;

	// if beyond global max. range, nobody can see anyone
	int currentDistanceSq = Position::distance2dSq(currentUnit->getPosition(), tile->getPosition());
	if (currentDistanceSq > getMaxViewDistanceSq())
	{
		return false;
	}

	// psi vision
	int psiVisionDistance = currentUnit->getPsiVision();
	bool fearImmune = tile->getUnit()->getArmor()->getFearImmune();
	if (psiVisionDistance > 0 && !fearImmune)
	{
		int psiCamo = tile->getUnit()->getArmor()->getPsiCamouflage();
		if (psiCamo > 0)
		{
			psiVisionDistance = std::min(psiVisionDistance, psiCamo);
		}
		else if (psiCamo < 0)
		{
			psiVisionDistance = std::max(0, psiVisionDistance + psiCamo);
		}
		if (currentDistanceSq <= (psiVisionDistance * psiVisionDistance))
		{
			return true; // we already sense the unit, no need to check obstacles or smoke
		}
	}

	const auto [visibleDistanceMaxVoxel, visibleDistanceUnitMaxTile] = getVisibleDistanceMaxHelper(this, tile, currentUnit, tile->getUnit());

	Position originVoxel = getSightOriginVoxel(currentUnit);

	Position scanVoxel;
	bool unitSeen = canTargetUnit(&originVoxel, tile, &scanVoxel, currentUnit, false);

	if (!unitSeen &&
		Options::battleRealisticAccuracy &&
		Options::oxceEnableOffCentreShooting)
	{
		for (const auto &relPos : { BattleActionOrigin::LEFT, BattleActionOrigin::RIGHT })
		{
			originVoxel = getSightOriginVoxel(currentUnit, tile, relPos);
			unitSeen = canTargetUnit(&originVoxel, tile, &scanVoxel, currentUnit, false);
			if (unitSeen) break;
		}
	}

	// heat vision 100% = smoke effectiveness 0%
	int smokeDensityFactor = 100 - Clamp(currentUnit->getVisibilityThroughSmoke(), 0, 100);
	int fireDensityFactor = 100 - Clamp(currentUnit->getVisibilityThroughFire(), 0, 100);

	if (unitSeen)
	{
		const auto [visibleDistanceVoxels, densityOfSmoke, densityOfFire, densityOfSmokeNearUnit, densityOfFireeNearUnit] = getTrajectoryDataHelper(this, _save, currentUnit, originVoxel, scanVoxel);

		// 3  - coefficient of calculation (see getTrajectoryDataHelper).
		// 20 - maximum view distance in vanilla Xcom.
		// 100 - % for smokeDensityFactor.
		// 16 - for voxel scale calculation.
		// Even if MaxViewDistance will be increased via ruleset, smoke will keep effect.
		int visibilityQuality = visibleDistanceMaxVoxel - visibleDistanceVoxels - ((densityOfSmoke - densityOfSmokeNearUnit / 2) * smokeDensityFactor + (densityOfFire - densityOfFireeNearUnit / 2) * fireDensityFactor) * visibleDistanceMaxVoxel/(3 * 20 * 100 * 16);
		ModScript::VisibilityUnit::Output arg{ visibilityQuality, visibilityQuality, ScriptTag<BattleUnitVisibility>::getNullTag() };
		ModScript::VisibilityUnit::Worker worker{ currentUnit, tile->getUnit(), tile, visibleDistanceVoxels, visibleDistanceMaxVoxel, visibleDistanceUnitMaxTile, densityOfSmoke, densityOfFire, densityOfSmokeNearUnit, densityOfFireeNearUnit };
		worker.execute(currentUnit->getArmor()->getScript<ModScript::VisibilityUnit>(), arg);
		unitSeen = 0 < arg.getFirst();
	}
	return unitSeen;
}

/**
 * Checks to see if a tile is visible through darkness, obstacles and smoke.
 * Note: psi vision, camouflage/anti-camouflage are intentionally removed.
 * @param action Current battle action.
 * @param tile The tile to check for.
 * @return True if visible.
 */
bool TileEngine::isTileInLOS(BattleAction *action, Tile *tile, bool drawing)
{
	// if there is no tile, we can't see it
	if (!tile)
	{
		return false;
	}

	BattleUnit *currentUnit = action->actor;

	// if beyond global max. range, nobody can see anything
	int currentDistanceSq = Position::distance2dSq(currentUnit->getPosition(), tile->getPosition());
	if (currentDistanceSq > getMaxViewDistanceSq())
	{
		return false;
	}

	const auto [visibleDistanceMaxVoxel, visibleDistanceUnitMaxTile] = getVisibleDistanceMaxHelper(this, tile, currentUnit, /*targetUnit*/ nullptr);

	// We MUST build a temp action, because current action doesn't yet have updated target (when only aiming)
	BattleAction tempAction;
	tempAction.actor = currentUnit;
	tempAction.type = action->type;
	tempAction.target = tile->getPosition();
	tempAction.weapon = action->weapon;

	Position originVoxel = getOriginVoxel(tempAction, currentUnit->getTile());
	Position scanVoxel;
	std::vector<Position> _trajectory;
	bool seen = false;

	bool forceFire = Options::forceFire && _save->isCtrlPressed(true) && _save->getSide() == FACTION_PLAYER;

	// Primary LOF check
	if (forceFire)
	{
		// if force-firing, always aim at the center of the target tile
		scanVoxel = tile->getPosition().toVoxel() + TileEngine::voxelTileCenter;
	}
	else if (tile->getMapData(O_OBJECT) != 0)
	{
		if (canTargetTile(&originVoxel, tile, O_OBJECT, &scanVoxel, currentUnit, false))
		{
			seen = true;
		}
		else
		{
			scanVoxel = tile->getPosition().toVoxel() + Position(8, 8, 10);
		}
	}
	else if (tile->getMapData(O_NORTHWALL) != 0)
	{
		if (canTargetTile(&originVoxel, tile, O_NORTHWALL, &scanVoxel, currentUnit, false))
		{
			seen = true;
		}
		else
		{
			scanVoxel = tile->getPosition().toVoxel() + Position(8, 0, 9);
		}
	}
	else if (tile->getMapData(O_WESTWALL) != 0)
	{
		if (canTargetTile(&originVoxel, tile, O_WESTWALL, &scanVoxel, currentUnit, false))
		{
			seen = true;
		}
		else
		{
			scanVoxel = tile->getPosition().toVoxel() + Position(0, 8, 9);
		}
	}
	else if (tile->getMapData(O_FLOOR) != 0)
	{
		if (canTargetTile(&originVoxel, tile, O_FLOOR, &scanVoxel, currentUnit, false))
		{
			seen = true;
		}
		else
		{
			scanVoxel = tile->getPosition().toVoxel() + Position(8, 8, 2);
		}
	}
	else
	{
		scanVoxel = tile->getPosition().toVoxel() + Position(8, 8, 12);
	}

	// Secondary LOF check
	if (!seen)
	{
		int test = calculateLineVoxel(originVoxel, scanVoxel, false, &_trajectory, currentUnit);
		if (test == V_EMPTY)
		{
			// We have hit nothing at all, LOF is clear (Note: _trajectory is empty)
			seen = true;
		}
		else if (test == V_OUTOFBOUNDS)
		{
			// FIXME/TESTME
			seen = false;
		}
		else
		{
			// Let's assume we hit and double-check
			seen = true;

			// inspired by Projectile::calculateTrajectory()
			Position hitPos = _trajectory.at(0).toTile();
			if (test == V_UNIT && _save->getTile(hitPos) && _save->getTile(hitPos)->getUnit() == 0) //no unit? must be lower
			{
				hitPos = Position(hitPos.x, hitPos.y, hitPos.z - 1);
			}

			if (hitPos != tempAction.target /*&& tempAction.result.empty()*/)
			{
				if (test == V_NORTHWALL)
				{
					if (hitPos.y - 1 != tempAction.target.y)
					{
						seen = false;
					}
				}
				else if (test == V_WESTWALL)
				{
					if (hitPos.x - 1 != tempAction.target.x)
					{
						seen = false;
					}
				}
				else if (test == V_UNIT)
				{
					BattleUnit *hitUnit = _save->getTile(hitPos)->getUnit();
					BattleUnit *targetUnit = drawing ? tile->getUnit() : tile->getOverlappingUnit(_save);
					if (hitUnit != targetUnit)
					{
						seen = false;
					}
				}
				else
				{
					seen = false;
				}
			}
		}
	}

	// LOS check uses sight origin voxel (LOF check uses origin voxel)
	originVoxel = getSightOriginVoxel(currentUnit);

	// heat vision 100% = smoke effectiveness 0%
	int smokeDensityFactor = 100 - Clamp(currentUnit->getVisibilityThroughSmoke(), 0, 100);
	int fireDensityFactor = 100 - Clamp(currentUnit->getVisibilityThroughFire(), 0, 100);

	if (seen)
	{
		const auto [visibleDistanceVoxels, densityOfSmoke, densityOfFire, densityOfSmokeNearUnit, densityOfFireeNearUnit] = getTrajectoryDataHelper(this, _save, currentUnit, originVoxel, scanVoxel);

		// 3  - coefficient of calculation (see getTrajectoryDataHelper).
		// 20 - maximum view distance in vanilla Xcom.
		// 100 - % for smokeDensityFactor.
		// 16 - for voxel scale calculation.
		// Even if MaxViewDistance will be increased via ruleset, smoke will keep effect.
		int visibilityQuality = visibleDistanceMaxVoxel - visibleDistanceVoxels - ((densityOfSmoke - densityOfSmokeNearUnit / 2) * smokeDensityFactor + (densityOfFire - densityOfFireeNearUnit / 2) * fireDensityFactor) * visibleDistanceMaxVoxel/(3 * 20 * 100 * 16);
		ModScript::VisibilityUnit::Output arg{ visibilityQuality, visibilityQuality, ScriptTag<BattleUnitVisibility>::getNullTag() };
		ModScript::VisibilityUnit::Worker worker{ currentUnit, /*targetUnit*/ nullptr, tile, visibleDistanceVoxels, visibleDistanceMaxVoxel, visibleDistanceUnitMaxTile, densityOfSmoke, densityOfFire, densityOfSmokeNearUnit, densityOfFireeNearUnit };
		worker.execute(currentUnit->getArmor()->getScript<ModScript::VisibilityUnit>(), arg);
		seen = 0 < arg.getFirst();
	}
	return seen;
}

/**
 * Checks for how exposed unit is for another unit.
 * @param originVoxel Voxel of trace origin (eye or gun's barrel).
 * @param tile The tile to check for.
 * @param excludeUnit Is self (not to hit self).
 * @param exposedVoxels [Optional] Array of positions of exposed voxels (function fills it)
 * @return Degree of exposure (as percent).
 */
double TileEngine::checkVoxelExposure(Position *originVoxel, Tile *tile, BattleUnit *excludeUnit, bool isDebug, std::vector<Position> *exposedVoxels, bool isSimpleMode)
{
	isDebug = isDebug && _save->getDebugMode();
	if (excludeUnit && excludeUnit->isAIControlled()) isSimpleMode = true;

	std::vector<Position> _trajectory;
	Position scanVoxel;
	BattleUnit *targetUnit = tile->getUnit();
	if (targetUnit == nullptr) return 0; //no unit in this tile, even if it elevated and appearing in it.
	if (targetUnit == excludeUnit) return 0; //skip self
	Position targetVoxel = targetUnit->getPosition().toVoxel();

	int targetMinHeight = targetVoxel.z - tile->getTerrainLevel();
	int targetFloatHeight = targetUnit->getFloatHeight();
	targetMinHeight += targetFloatHeight;

	int heightRange;
	if (!targetUnit->isOut())
		heightRange = targetUnit->getHeight();
	else
		heightRange = 12;

	int targetMaxHeight = targetMinHeight + heightRange;

	int unitRadius = targetUnit->getRadiusVoxels();
	int targetSize = targetUnit->getArmor()->getSize();
	targetVoxel += Position(8*targetSize, 8*targetSize, 0); // center of unit

	int unitMin_X = targetVoxel.x - unitRadius - 1;
	int unitMin_Y = targetVoxel.y - unitRadius - 1;
	int unitMax_X = targetVoxel.x + unitRadius + 1;
	int unitMax_Y = targetVoxel.y + unitRadius + 1;

	// sliceTargets[ unitRadius ] = {0, 0} and won't be overwritten further
	int sliceTargetsX[ BattleUnit::BIG_MAX_RADIUS*2 + 1 ] = { 0 };
	int sliceTargetsY[ BattleUnit::BIG_MAX_RADIUS*2 + 1 ] = { 0 };

	// vector manipulation to make scan work in view-space
	Position relPos = targetVoxel - *originVoxel;

	for ( int testRadius = unitRadius; testRadius > 0; --testRadius) // slice for every voxel of a radius!
	{
		double normal = testRadius/sqrt((double)(relPos.x*relPos.x + relPos.y*relPos.y));
		int relX = (int)floor(((double)relPos.y)*normal+0.5);
		int relY = (int)floor(((double)-relPos.x)*normal+0.5);

		sliceTargetsX[ unitRadius - testRadius ] = relX;
		sliceTargetsY[ unitRadius - testRadius ] = relY;
		sliceTargetsX[ unitRadius + testRadius ] = -relX;
		sliceTargetsY[ unitRadius + testRadius ] = -relY;
	}

	int relX = sliceTargetsX[0];
	int relY = sliceTargetsY[0];
	int sliceTargetsTopBottom[] = { relY, -relX, -relY, relX }; // front/back scan points

	std::vector<std::string> scanArray;
	scanArray.reserve(24);
	const char symbols[] = {'.','_','/','\\','o','u','x'};

	// scan rays from top to bottom, every voxel of target cylinder
	int total=0;
	int visible=0;

	// for examlpe hovertank/plasma has floating height of 6, so its bottom is on level 7, with voxels 0-6 below it.
	int bottomHeight = targetMinHeight + 1;

	int floorElevation = targetMinHeight % Position::TileZ;
	if (floorElevation < 2)
	{
		bottomHeight = targetMinHeight - floorElevation + 2; // can't check height 0-1 (bug?)
	}

	// Reduce number of checks in simple mode
	int simplifyDivider = unitRadius;
	if (targetSize == 2) simplifyDivider = 4;

	for (int height = targetMaxHeight; height >= bottomHeight; height -= 2)
	{
		std::string scanLine;
		scanVoxel.z = height;

		for (int j = 0; j <= unitRadius*2; ++j)
		{
			// Skip voxels in "simple" mode. usually to speed up AI calculations
			if (isSimpleMode && (height + j) % simplifyDivider != 0)
			{
				scanLine += '.';
				continue; // scan every N-th voxel
			}

			++total;
			scanVoxel.x = targetVoxel.x + sliceTargetsX[j];
			scanVoxel.y = targetVoxel.y + sliceTargetsY[j];

			_trajectory.clear();
			int test = calculateLineVoxel(*originVoxel, scanVoxel, false, &_trajectory, excludeUnit);
			if (test == V_UNIT)
			{
				int impactX = _trajectory.at(0).x;
				int impactY = _trajectory.at(0).y;
				int impactZ = _trajectory.at(0).z;

				if (impactX >= unitMin_X && impactX <= unitMax_X &&
					impactY >= unitMin_Y && impactY <= unitMax_Y &&
					impactZ >= targetMinHeight+1 && impactZ <= targetMaxHeight)
				{
					++visible;
					if (exposedVoxels) exposedVoxels->emplace_back(scanVoxel);
					scanLine += '#';
				}
				else
					scanLine += symbols[ test+1 ]; // overlapped by another unit
			}

			else
			{
				if ( test == V_EMPTY )	--total;
				scanLine += symbols[ test+1 ]; // V_EMPTY = -1
			}
		}
		scanLine += " " + std::to_string( height % Position::TileZ );
		scanArray.emplace_back( scanLine );

		// Additional bottom layer for units with odd height
		if (targetFloatHeight > 1 && heightRange % 2 == 0 && height - bottomHeight == 1) ++height;
	}
	double exposure = (double)visible / total;

	if (isDebug)
	{
		Log(LOG_INFO) << " ";
		for ( const auto &line : scanArray )
			Log(LOG_INFO) << line;
		Log(LOG_INFO) << " ";
	}

	if (exposure < 0.1) // Check near/far parts of target cylinder
	{
		bool aimFromAbove = originVoxel->z > targetMaxHeight;
		bool aimFromBelow = originVoxel->z < targetMinHeight + 1;
		if (!aimFromAbove && !aimFromBelow) return exposure; // Aiming horizontally, cannot see any additional voxels

		// sliceTargetsTopBottom[] points order: front, back
		// If aiming from above: check "top back" and "bottom front" points
		int heights[] = { targetMinHeight+1, targetMaxHeight };

		// If aiming from below: check "bottom back" and "top front" points
		if (aimFromBelow) std::swap( heights[0], heights[1] );

		for ( int i = 0; i < 2; ++i)
		{
			scanVoxel.z = heights[ i ];
			scanVoxel.x = targetVoxel.x + sliceTargetsTopBottom[ i * 2 ];
			scanVoxel.y = targetVoxel.y + sliceTargetsTopBottom[ i * 2 + 1];

			_trajectory.clear();
			int test = calculateLineVoxel(*originVoxel, scanVoxel, false, &_trajectory, excludeUnit);
			if (test == V_UNIT)
			{
				int impactX = _trajectory.at(0).x;
				int impactY = _trajectory.at(0).y;
				int impactZ = _trajectory.at(0).z;

				if (impactX >= unitMin_X && impactX <= unitMax_X &&
					impactY >= unitMin_Y && impactY <= unitMax_Y &&
					impactZ >= targetMinHeight+1 && impactZ <= targetMaxHeight)
				{
					exposure += 0.05;
					if (exposedVoxels) exposedVoxels->emplace_back(scanVoxel);
				}
			}
		}
	}

	return exposure;
}

/**
 * Checks for another unit available for targeting and what particular voxel.
 * @param originVoxel Voxel of trace origin (eye or gun's barrel).
 * @param tile The tile to check for.
 * @param scanVoxel is returned coordinate of hit.
 * @param excludeUnit is self (not to hit self).
 * @param rememberObstacles Remember obstacles for no LOF indicator?
 * @param potentialUnit is a hypothetical unit to draw a virtual line of fire for AI. if left blank, this function behaves normally.
 * @return True if the unit can be targetted.
 */
bool TileEngine::canTargetUnit(Position *originVoxel, Tile *tile, Position *scanVoxel, BattleUnit *excludeUnit, bool rememberObstacles, BattleUnit *potentialUnit)
{
	std::vector<Position> _trajectory;

	BattleUnit *targetUnit;
	bool hypothetical = potentialUnit != 0;
	if (potentialUnit == 0)
	{
		targetUnit = tile->getUnit();
		if (targetUnit == 0) return false; //no unit in this tile, even if it elevated and appearing in it.
	}
	else
		targetUnit = potentialUnit;

	if (targetUnit == excludeUnit) return false; //skip self

	Position tempScanVoxel;
	if (scanVoxel == nullptr) scanVoxel = &tempScanVoxel; // stub in case we don't plan to return found point

	bool isPlayer = true;
	bool isUnderAIcontrol = false;

	if (excludeUnit)
	{
		if (excludeUnit->getFaction() != FACTION_PLAYER ) isPlayer = false;
		if (excludeUnit->isAIControlled()) isUnderAIcontrol = true;
	}

	Position targetVoxel = targetUnit->getPosition().toVoxel();

	int targetMinHeight = targetVoxel.z - tile->getTerrainLevel();
	int targetFloatHeight = targetUnit->getFloatHeight();
	targetMinHeight += targetFloatHeight;

	int heightRange;
	if (!targetUnit->isOut())
		heightRange = targetUnit->getHeight();
	else
		heightRange = 12;

	int targetMaxHeight = targetMinHeight + heightRange;

	int unitRadius = targetUnit->getRadiusVoxels();
	int targetSize = targetUnit->getArmor()->getSize();

	targetVoxel += Position(8*targetSize, 8*targetSize, 0); // center of unit

	int unitMin_X = targetVoxel.x - unitRadius - 1;
	int unitMin_Y = targetVoxel.y - unitRadius - 1;
	int unitMax_X = targetVoxel.x + unitRadius + 1;
	int unitMax_Y = targetVoxel.y + unitRadius + 1;

	if (isPlayer && !isUnderAIcontrol) // Precise targeting for human player
	{
		static int verticalSlices[26] = { 0 }; // Up to 11 frontal section points and 2 for front/back
		bool aimFromAbove = (originVoxel->z > targetMaxHeight ? true : false);
		bool aimFromBelow = (originVoxel->z < targetMinHeight ? true : false);

		int shiftCount = 1; // indexes 0, 1 for (0,0)
		Position relPos = targetVoxel - *originVoxel;

		if (targetSize == 1)
		{
			// Add (radius-1) and (radius) slices
			for ( int testRadius = unitRadius-1; testRadius <= unitRadius; ++testRadius)
			{
				float normal = testRadius / sqrt((float)(relPos.x*relPos.x + relPos.y*relPos.y));
				int relX = floor(((float)relPos.y)*normal+0.5);
				int relY = floor(((float)-relPos.x)*normal+0.5);

				verticalSlices[ ++shiftCount ] = relX; // starting from index 2
				verticalSlices[ ++shiftCount ] = relY;
				verticalSlices[ ++shiftCount ] = -relX;
				verticalSlices[ ++shiftCount ] = -relY;

				if (testRadius == unitRadius && (aimFromAbove || aimFromBelow))
				{
					verticalSlices[ ++shiftCount ] = relY;
					verticalSlices[ ++shiftCount ] = -relX;
					verticalSlices[ ++shiftCount ] = -relY;
					verticalSlices[ ++shiftCount ] = relX;
				}
			}
		}
		else // size = 2
		{
			for ( int testRadius = 3; testRadius <= 15; testRadius += 3 )
			{
				float normal = testRadius / sqrt((float)(relPos.x*relPos.x + relPos.y*relPos.y));
				int relX = floor(((float)relPos.y)*normal+0.5);
				int relY = floor(((float)-relPos.x)*normal+0.5);

				verticalSlices[ ++shiftCount ] = relX;
				verticalSlices[ ++shiftCount ] = relY;
				verticalSlices[ ++shiftCount ] = -relX;
				verticalSlices[ ++shiftCount ] = -relY;

				if (testRadius == 15 && (aimFromAbove || aimFromBelow))
				{
					verticalSlices[ ++shiftCount ] = relY;
					verticalSlices[ ++shiftCount ] = -relX;
					verticalSlices[ ++shiftCount ] = -relY;
					verticalSlices[ ++shiftCount ] = relX;
				}
			}
		}

		int pointsCount = (shiftCount + 1) / 2;

		int targetCenterHeight;
		targetCenterHeight = (targetMaxHeight + targetMinHeight) / 2;
		targetCenterHeight += (targetMaxHeight - targetCenterHeight) % 2; // Center should have even number of voxels above it

		int  horizontalCount = heightRange / 2; // Number of horizontal slices for a target
		horizontalCount += horizontalCount % 2; // They are symmetrical relative to center so should be even number too

		if (horizontalCount > 12) horizontalCount = 12;
		if (horizontalCount <= 0) horizontalCount = 0;

		// Scan ray for every horizontal slice including center
		for (int hIdx = 0; hIdx <= horizontalCount; ++hIdx)
		{
			// Start from the center, gradually increase vertical distance in both directions
			scanVoxel->z = targetCenterHeight + heightFromCenter[ hIdx ];

			if (scanVoxel->z < targetMinHeight+1 || scanVoxel->z > targetMaxHeight)
				continue;

			// Scan ray for every vertical slice in selected horizontal plane
			for (int vIdx = 0; vIdx < pointsCount; ++vIdx)
			{
				// Skip unnecessary checks
				bool checkTopBottom = (aimFromAbove || aimFromBelow) && vIdx >= pointsCount - 2;
				if (checkTopBottom && scanVoxel->z > targetMinHeight+2 && scanVoxel->z < targetMaxHeight ) continue;

				// Start from the center, increase horizontal distance in both directions
				scanVoxel->x = targetVoxel.x + verticalSlices[ vIdx * 2 ];
				scanVoxel->y = targetVoxel.y + verticalSlices[ vIdx * 2 + 1 ];

				_trajectory.clear();
				int test = calculateLineVoxel(*originVoxel, *scanVoxel, false, &_trajectory, excludeUnit);
				if (test == V_UNIT)
				{
					if (_trajectory.empty()) assert(false);

					int impactX = _trajectory.at(0).x;
					int impactY = _trajectory.at(0).y;
					int impactZ = _trajectory.at(0).z;

					//voxel of hit must be inside of scanned box
					if (impactX >= unitMin_X && impactX <= unitMax_X &&
						impactY >= unitMin_Y && impactY <= unitMax_Y &&
						impactZ >= targetMinHeight+1 && impactZ <= targetMaxHeight)
					{
						return true;
					}
				}

				else if (test == V_EMPTY && hypothetical && !_trajectory.empty())
				{
					return true;
				}

				if (rememberObstacles && _trajectory.size()>0)
				{
					Tile *tileObstacle = _save->getTile(_trajectory.at(0).toTile());
					if (tileObstacle) tileObstacle->setObstacle(test);
				}
			}
		}
		return false;
	}

	else // simplified targeting for AI
	{
		// vector manipulation to make scan work in view-space
		Position relPos = targetVoxel - *originVoxel;

		float normal = unitRadius/sqrt((float)(relPos.x*relPos.x + relPos.y*relPos.y));
		int relX = floor(((float)relPos.y)*normal+0.5);
		int relY = floor(((float)-relPos.x)*normal+0.5);

		// Targeting order: center, right, left, front, back
		int verticalSlices[] = { 0,0, relX,relY, -relX,-relY, relY,-relX, -relY,relX };

		int horizontalSlices[] = // Targeting order: 3/4 height, 1/4 height, top, bottom
			{
				targetMinHeight + heightRange * 3 / 4,
				targetMinHeight + 1 + (int)ceil(heightRange * 0.25f),
				targetMaxHeight,
				targetMinHeight + 1
			};

		// Scan for every horizontal slice
		for (int hIdx = 0; hIdx < 4; ++hIdx)
		{
			scanVoxel->z = horizontalSlices[ hIdx ];

			// Scan ray for every vertical slice in selected horizontal plane
			for (int vIdx = 0; vIdx < 5; ++vIdx)
			{
				// Scan front/back slices only on top/bottom planes
				if (hIdx < 2 && vIdx > 2) break;

				scanVoxel->x = targetVoxel.x + verticalSlices[ vIdx * 2 ];
				scanVoxel->y = targetVoxel.y + verticalSlices[ vIdx * 2 + 1];

				_trajectory.clear();
				int test = calculateLineVoxel(*originVoxel, *scanVoxel, false, &_trajectory, excludeUnit);
				if (test == V_UNIT)
				{
					if (_trajectory.empty()) assert(false);

					int impactX = _trajectory.at(0).x;
					int impactY = _trajectory.at(0).y;
					int impactZ = _trajectory.at(0).z;

					//voxel of hit must be inside of scanned box
					if (impactX >= unitMin_X && impactX <= unitMax_X &&
						impactY >= unitMin_Y && impactY <= unitMax_Y &&
						impactZ >= targetMinHeight+1 && impactZ <= targetMaxHeight)
					{
						return true;
					}
				}

				else if (test == V_EMPTY && hypothetical && !_trajectory.empty())
				{
					return true;
				}
			}
		}
		return false;
	}
}

/**
 * Checks for a tile part available for targeting and what particular voxel.
 * @param originVoxel Voxel of trace origin (gun's barrel).
 * @param tile The tile to check for.
 * @param part Tile part to check for.
 * @param scanVoxel Is returned coordinate of hit.
 * @param excludeUnit Is self (not to hit self).
 * @param rememberObstacles Remember obstacles for no LOF indicator?
 * @return True if the tile can be targetted.
 */
bool TileEngine::canTargetTile(Position *originVoxel, Tile *tile, int part, Position *scanVoxel, BattleUnit *excludeUnit, bool rememberObstacles)
{
	static int sliceObjectSpiral[82] = {8,8, 8,6, 10,6, 10,8, 10,10, 8,10, 6,10, 6,8, 6,6, //first circle
		8,4, 10,4, 12,4, 12,6, 12,8, 12,10, 12,12, 10,12, 8,12, 6,12, 4,12, 4,10, 4,8, 4,6, 4,4, 6,4, //second circle
		8,1, 12,1, 15,1, 15,4, 15,8, 15,12, 15,15, 12,15, 8,15, 4,15, 1,15, 1,12, 1,8, 1,4, 1,1, 4,1}; //third circle
	static int westWallSpiral[14] = {0,7, 0,9, 0,6, 0,11, 0,4, 0,13, 0,2};
	static int northWallSpiral[14] = {7,0, 9,0, 6,0, 11,0, 4,0, 13,0, 2,0};

	Position targetVoxel = Position((tile->getPosition().x * 16), (tile->getPosition().y * 16), tile->getPosition().z * 24);
	std::vector<Position> _trajectory;

	int *spiralArray;
	int spiralCount;

	int minZ = 0, maxZ = 0;
	bool minZfound = false, maxZfound = false;
	bool dummy = false;

	if (part == O_OBJECT)
	{
		spiralArray = sliceObjectSpiral;
		spiralCount = 41;
	}
	else
	if (part == O_NORTHWALL)
	{
		spiralArray = northWallSpiral;
		spiralCount = 7;
	}
	else
	if (part == O_WESTWALL)
	{
		spiralArray = westWallSpiral;
		spiralCount = 7;
	}
	else if (part == O_FLOOR)
	{
		spiralArray = sliceObjectSpiral;
		spiralCount = 41;
		minZfound = true; minZ=0;
		maxZfound = true; maxZ=0;
	}
	else if (part == MapData::O_DUMMY) // used only for no line of fire indicator
	{
		spiralArray = sliceObjectSpiral;
		spiralCount = 41;
		minZfound = true; minZ = 12;
		maxZfound = true; maxZ = 12;
	}
	else
	{
		return false;
	}
	voxelCheckFlush();
// find out height range

	if (!minZfound)
	{
		for (int j = 1; j < 12; ++j)
		{
			if (minZfound) break;
			for (int i = 0; i < spiralCount; ++i)
			{
				int tX = spiralArray[i*2];
				int tY = spiralArray[i*2+1];
				if (voxelCheck(Position(targetVoxel.x + tX, targetVoxel.y + tY, targetVoxel.z + j*2),0,true) == part) //bingo
				{
					if (!minZfound)
					{
						minZ = j*2;
						minZfound = true;
						break;
					}
				}
			}
		}
	}

	if (!minZfound)
	{
		if (rememberObstacles)
		{
			// dummy attempt (only to highlight obstacles)
			minZfound = true;
			minZ = 10;
			dummy = true;
		}
		else
		{
			return false;//empty object!!!
		}
	}

	if (!maxZfound)
	{
		for (int j = 10; j >= 0; --j)
		{
			if (maxZfound) break;
			for (int i = 0; i < spiralCount; ++i)
			{
				int tX = spiralArray[i*2];
				int tY = spiralArray[i*2+1];
				if (voxelCheck(Position(targetVoxel.x + tX, targetVoxel.y + tY, targetVoxel.z + j*2),0,true) == part) //bingo
				{
					if (!maxZfound)
					{
						maxZ = j*2;
						maxZfound = true;
						break;
					}
				}
			}
		}
	}

	if (!maxZfound)
	{
		if (rememberObstacles)
		{
			// dummy attempt (only to highlight obstacles)
			maxZfound = true;
			maxZ = 10;
			dummy = true;
		}
		else
		{
			return false;//it's impossible to get there
		}
	}

	if (minZ > maxZ) minZ = maxZ;
	int rangeZ = maxZ - minZ;
	if (rangeZ>10) rangeZ = 10; //as above, clamping height range to prevent buffer overflow
	int centerZ = (maxZ + minZ)/2;

	for (int j = 0; j <= rangeZ; ++j)
	{
		scanVoxel->z = targetVoxel.z + centerZ + heightFromCenter[j];
		for (int i = 0; i < spiralCount; ++i)
		{
			scanVoxel->x = targetVoxel.x + spiralArray[i*2];
			scanVoxel->y = targetVoxel.y + spiralArray[i*2+1];
			_trajectory.clear();
			int test = calculateLineVoxel(*originVoxel, *scanVoxel, false, &_trajectory, excludeUnit);
			if (test == part && !dummy) //bingo
			{
				if (_trajectory.at(0).x/16 == scanVoxel->x/16 &&
					_trajectory.at(0).y/16 == scanVoxel->y/16 &&
					_trajectory.at(0).z/24 == scanVoxel->z/24)
				{
					return true;
				}
			}
			if (rememberObstacles && _trajectory.size()>0)
			{
				Tile *tileObstacle = _save->getTile(_trajectory.at(0).toTile());
				if (tileObstacle) tileObstacle->setObstacle(test);
			}
		}
	}
	return false;
}

Position TileEngine::adjustTargetVoxelFromTileType(Position *originVoxel, Tile *targetTile, BattleUnit *excludeUnit, bool rememberObstacles)
{
	if (targetTile == nullptr || targetTile->getPosition() == TileEngine::invalid) return TileEngine::invalid;
	Position targetVoxel;

	if (targetTile->getMapData(O_OBJECT) != 0)
	{
		if (!canTargetTile(originVoxel, targetTile, O_OBJECT, &targetVoxel, excludeUnit, rememberObstacles))
		{
			targetVoxel = targetTile->getPosition().toVoxel() + Position(8, 8, 10);
		}
	}
	else if (targetTile->getMapData(O_NORTHWALL) != 0)
	{
		if (!canTargetTile(originVoxel, targetTile, O_NORTHWALL, &targetVoxel, excludeUnit, rememberObstacles))
		{
			targetVoxel = targetTile->getPosition().toVoxel() + Position(8, 0, 9);
		}
	}
	else if (targetTile->getMapData(O_WESTWALL) != 0)
	{
		if (!canTargetTile(originVoxel, targetTile, O_WESTWALL, &targetVoxel, excludeUnit, rememberObstacles))
		{
			targetVoxel = targetTile->getPosition().toVoxel() + Position(0, 8, 9);
		}
	}
	else if (targetTile->getMapData(O_FLOOR) != 0)
	{
		if (!canTargetTile(originVoxel, targetTile, O_FLOOR, &targetVoxel, excludeUnit, rememberObstacles))
		{
			targetVoxel = targetTile->getPosition().toVoxel() + Position(8, 8, 2);
		}
	}
	else
	{
		// dummy attempt (only to highlight obstacles)
		canTargetTile(originVoxel, targetTile, MapData::O_DUMMY, &targetVoxel, excludeUnit, rememberObstacles);

		// target nothing, targets the middle of the tile
		targetVoxel = targetTile->getPosition().toVoxel() + TileEngine::voxelTileCenter;
	}

	return targetVoxel;
}

/**
 * Calculates line of sight of a soldiers within range of the Position
 * (used when terrain has changed, which can reveal new parts of terrain or units).
 * @param position Position of the changed terrain.
 * @param eventRadius Radius of circle big enough to encompass the event.
 * @param updateTiles true to do an update of visible tiles.
 * @param appendToTileVisibility true to append only new tiles and skip previously seen ones.
 */
void TileEngine::calculateFOV(Position position, int eventRadius, const bool updateTiles, const bool appendToTileVisibility)
{
	int updateRadius;
	if (eventRadius == -1)
	{
		eventRadius = getMaxViewDistance();
		updateRadius = getMaxViewDistanceSq();
	}
	else
	{
		//Need to grab units which are out of range of the centre of the event, but can still see the edge of the effect.
		updateRadius = getMaxViewDistance() + (eventRadius > 0 ? eventRadius : 0);
		updateRadius *= updateRadius;
	}
	for (auto* bu : *_save->getUnits())
	{
		if (Position::distance2dSq(position, bu->getPosition()) <= updateRadius) //could this unit have observed the event?
		{
			if (updateTiles)
			{
				if (!appendToTileVisibility)
				{
					bu->clearVisibleTiles();
				}
				calculateTilesInFOV(bu, position, eventRadius);
			}

			calculateUnitsInFOV(bu, position, eventRadius);
		}
	}
}

/**
 * Checks if a sniper from the opposing faction sees this unit. The unit with the highest reaction score will be compared with the current unit's reaction score.
 * If it's higher, a shot is fired when enough time units, a weapon and ammo are available.
 * @param unit The unit to check reaction fire upon.
 * @return True if reaction fire took place.
 */
bool TileEngine::checkReactionFire(BattleUnit *unit, const BattleAction &originalAction)
{
	if (_save->isPreview())
	{
		return false;
	}

	// reaction fire only triggered when the actioning unit is of the currently playing side, and is still on the map (alive)
	if (unit->getFaction() != _save->getSide() || unit->getTile() == 0)
	{
		return false;
	}

	std::vector<ReactionScore> spotters = getSpottingUnits(unit);
	bool result = false;

	// not mind controlled, or controlled by the player
	if (unit->getFaction() == unit->getOriginalFaction()
		|| unit->getFaction() != FACTION_HOSTILE)
	{
		// get the first man up to bat.
		ReactionScore *reactor = getReactor(spotters, unit);
		// start iterating through the possible reactors until the current unit is the one with the highest score.
		while (reactor != 0)
		{
			if (reactor->count > 10 || !tryReaction(reactor, unit, originalAction))
			{
				for (std::vector<ReactionScore>::iterator i = spotters.begin(); i != spotters.end(); ++i)
				{
					if (&(*i) == reactor)
					{
						spotters.erase(i);
						reactor = 0;
						break;
					}
				}
				// can't make a reaction snapshot for whatever reason, boot this guy from the vector.
				// avoid setting result to true, but carry on, just cause one unit can't react doesn't mean the rest of the units in the vector (if any) can't
				reactor = getReactor(spotters, unit);
				continue;
			}
			// nice shot, kid. don't get cocky.
			result = true;
			reactor->reactionScore -= reactor->reactionReduction;
			reactor->count += 1;
			reactor = getReactor(spotters, unit);
		}
	}
	return result;
}

/**
 * Creates a vector of units that can spot this unit.
 * @param unit The unit to check for spotters of.
 * @return A vector of units that can see this unit.
 */
std::vector<TileEngine::ReactionScore> TileEngine::getSpottingUnits(BattleUnit* unit)
{
	std::vector<TileEngine::ReactionScore> spotters;
	Tile *tile = unit->getTile();
	int threshold = unit->getReactionScore();
	// no reaction on civilian turn.
	if (_save->getSide() != FACTION_NEUTRAL)
	{
		for (auto* bu : *_save->getUnits())
		{
				// not dead/unconscious
			if (!bu->isOut() &&
				// not dying or not about to pass out
				!bu->isOutThresholdExceed() &&
				// have any chances for reacting
				bu->getReactionScore() >= threshold &&
				// not a friend
				bu->getFaction() != _save->getSide() &&
				// not a civilian, or a civilian shooting at bad non-ignored guys
				(bu->getFaction() != FACTION_NEUTRAL || (unit->getFaction() == FACTION_HOSTILE && !unit->isIgnoredByAI())) &&
				// closer than 20 tiles
				Position::distance2dSq(unit->getPosition(), bu->getPosition()) <= getMaxViewDistanceSq())
			{
				BattleAction falseAction;
				falseAction.type = BA_SNAPSHOT;
				falseAction.actor = bu;
				falseAction.target = unit->getPosition();
				Position originVoxel = getOriginVoxel(falseAction, 0);
				Position targetVoxel;
				AIModule *ai = bu->getAIModule();

				// Inquisitor's note regarding 'gotHit' variable
				// in vanilla, the 'hitState' flag is the only part of this equation that comes into play.
				// any time a unit takes damage, this flag is set, then it would be reset by a call to
				// a function analogous to SavedBattleGame::resetUnitHitStates(), any time:
				// 1: a unit was selected by being clicked on.
				// 2: either "next unit" button was pressed.
				// 3: the inventory screen was accessed. (i didn't look too far into this one, it's possible it's only called in the pre-mission equip screen)
				// 4: the same place where we call it, immediately before every move the AI makes.
				// this flag is responsible for units turning around to respond to hits, and is in keeping with the details listed on http://www.ufopaedia.org/index.php/Reaction_fire_triggers
				// we've gone for a slightly different implementation: AI units keep a list of which units have hit them and don't forget until the end of the player's turn.
				// this method is in keeping with the spirit of the original feature, but much less exploitable by players.
				// the hitState flag in our implementation allows player units to turn and react as they did in the original, (which is far less cumbersome than giving them all an AI module)
				// we don't extend the same "enhanced aggressor memory" courtesy to players, because in the original, they could only turn and react to damage immediately after it happened.
				// this is because as much as we want the player's soldiers dead, we don't want them to feel like we're being unfair about it.

				bool gotHit = (ai != 0 && ai->getWasHitBy(unit->getId())) || (ai == 0 && bu->getHitState());

				if (!gotHit && Mod::EXTENDED_MELEE_REACTIONS == 2)
				{
					// to allow melee reactions when attacked from any side, not just from the front
					gotHit = bu->wasMeleeAttackedBy(unit->getId());
				}

					// can actually see the target Tile, or we got hit
				if ((bu->checkViewSector(unit->getPosition()) || gotHit) &&
					// can actually target the unit
					canTargetUnit(&originVoxel, tile, &targetVoxel, bu, false) &&
					// can actually see the unit
					visible(bu, tile))
				{
					if (bu->getFaction() == FACTION_PLAYER)
					{
						unit->setVisible(true);
					}
					bu->addToVisibleUnits(unit);
					ReactionScore rs = determineReactionType(bu, unit);
					if (rs.attackType != BA_NONE)
					{
						int reactionFireThreshold = _save->getBattleGame()->getMod()->getReactionFireThreshold(bu->getFaction());
						if (reactionFireThreshold > 0)
						{
							BattleItem *weapon = rs.weapon;
							int accuracy = BattleUnit::getFiringAccuracy(BattleActionAttack::GetBeforeShoot(rs.attackType, rs.unit, weapon), _save->getBattleGame()->getMod());
							int distanceSq = unit->distance3dToUnitSq(bu);
							int distance = (int)std::ceil(sqrt(float(distanceSq)));

							{
								int upperLimit, lowerLimit;
								int dropoff = weapon->getRules()->calculateLimits(upperLimit, lowerLimit, _save->getDepth(), rs.attackType);

								if (distance > upperLimit)
								{
									accuracy -= (distance - upperLimit) * dropoff;
								}
								else if (distance < lowerLimit)
								{
									accuracy -= (lowerLimit - distance) * dropoff;
								}
							}

							bool outOfRange = weapon->getRules()->isOutOfRange(distanceSq);

							if (Options::useChanceToHit)
							{
								auto targetSize = unit->getArmor()->getSize();
								accuracy = Projectile::getHitChance(distance, accuracy, _save->getMod()->getHitChancesTable( targetSize ));
							}

							if (accuracy >= reactionFireThreshold && !outOfRange)
							{
								spotters.push_back(rs);
							}
						}
						else
						{
							spotters.push_back(rs);
						}
					}
				}
			}
		}
	}
	return spotters;
}

/**
 * Gets the unit with the highest reaction score from the spotter vector.
 * @param spotters The vector of spotting units.
 * @param unit The unit to check scores against.
 * @return The unit with the highest reactions.
 */
TileEngine::ReactionScore *TileEngine::getReactor(std::vector<TileEngine::ReactionScore> &spotters, BattleUnit *unit)
{
	ReactionScore *best = 0;
	for (std::vector<ReactionScore>::iterator i = spotters.begin(); i != spotters.end(); ++i)
	{
		if (!(*i).unit->isOut() && !(*i).unit->getRespawn() && (!best || (*i).reactionScore > best->reactionScore))
		{
			best = &(*i);
		}
	}
	if (best &&(unit->getReactionScore() <= best->reactionScore))
	{
		if (best->unit->getOriginalFaction() == FACTION_PLAYER)
		{
			best->unit->addReactionExp();
		}
	}
	else
	{
		best = 0;
	}
	return best;
}

/**
 * Checks the validity of a snap shot performed here.
 * @param unit The unit to check sight from.
 * @param target The unit to check sight TO.
 * @return True if the target is valid.
 */
TileEngine::ReactionScore TileEngine::determineReactionType(BattleUnit *unit, BattleUnit *target)
{
	ReactionScore reaction =
	{
		unit,
		nullptr,
		BA_NONE,
		unit->getReactionScore(),
		0.0,
		1,
	};

	// to avoid 0 that cause infinite loop we set minimal reaction handled by logic, should correctly handle units with 1 point in reaction and 1/1000 TU
	if (reaction.reactionScore <= 0.001)
	{
		return reaction;
	}

	auto setReaction = [](ReactionScore& re, BattleActionType type, BattleItem* weapon)
	{
		re.attackType = type;
		re.weapon = weapon;
		re.reactionReduction = 1.0 * BattleActionCost(type, re.unit, weapon).Time * re.unit->getBaseStats()->reactions / re.unit->getBaseStats()->tu;
	};

	std::vector<BattleItem*> reactionWeapons;
	// 1. first try the preferred weapon (player units only... to prevent abuse)
	bool isPlayer = (unit->getFaction() == FACTION_PLAYER);
	if (isPlayer)
	{
		if (BattleItem* preferredWeapon = unit->getWeaponForReactions())
		{
			reactionWeapons.push_back(preferredWeapon);
		}
	}
	// 2. then prioritize melee
	if (BattleItem* meleeWeapon = unit->getUtilityWeapon(BT_MELEE))
	{
		reactionWeapons.push_back(meleeWeapon);
	}
	// 3. then the rest (AI: quickest weapon, Player: last selected/main weapon)
	if (BattleItem* otherWeapon = unit->getMainHandWeapon(!isPlayer, true, true))
	{
		reactionWeapons.push_back(otherWeapon);
	}

	int tempDirection = unit->getDirection();
	if (Mod::EXTENDED_MELEE_REACTIONS == 2)
	{
		// temporarily face the target to allow melee reactions when attacked from any side, not just from the front
		tempDirection = getDirectionTo(unit->getPosition(), target->getPosition());
	}

	BattleItem* disabledLeft = nullptr;
	BattleItem* disabledRight = nullptr;
	// player units only... to prevent abuse
	if (isPlayer)
	{
		BattleItem* leftHandItem = unit->getLeftHandWeapon();
		BattleItem* rightHandItem = unit->getRightHandWeapon();
		BattleItem* emptyHandItem = nullptr;
		if ((!leftHandItem && unit->isLeftHandDisabledForReactions()) || (!rightHandItem && unit->isRightHandDisabledForReactions()))
		{
			auto typesToCheck = { BT_MELEE, BT_PSIAMP, BT_FIREARM/*, BT_MEDIKIT, BT_SCANNER, BT_MINDPROBE*/ };
			for (auto& type : typesToCheck)
			{
				emptyHandItem = unit->getSpecialWeapon(type);
				if (emptyHandItem && emptyHandItem->getRules()->isSpecialUsingEmptyHand())
				{
					break;
				}
				emptyHandItem = nullptr;
			}
		}
		disabledLeft = unit->isLeftHandDisabledForReactions() ? (leftHandItem ? leftHandItem : emptyHandItem) : nullptr;
		disabledRight = unit->isRightHandDisabledForReactions() ? (rightHandItem ? rightHandItem : emptyHandItem) : nullptr;
	}

	for (auto* weapon : reactionWeapons)
	{
		if (weapon == disabledLeft || weapon == disabledRight)
		{
			// the player doesn't want to react with this weapon
			continue;
		}

		if (_save->canUseWeapon(weapon, unit, false, BA_HIT))
		{
			// has a weapon capable of melee and is in melee range
			if (validMeleeRange(unit, target, tempDirection) &&
				weapon->getAmmoForAction(BA_HIT) &&
				BattleActionCost(BA_HIT, unit, weapon).haveTU())
			{
				setReaction(reaction, BA_HIT, weapon);
				return reaction;
			}
		}
		if (_save->canUseWeapon(weapon, unit, false, BA_SNAPSHOT))
		{
			// has a gun capable of snap shot with ammo
			if (weapon->getRules()->getBattleType() == BT_FIREARM &&
				!weapon->getRules()->isOutOfRange(unit->distance3dToUnitSq(target)) &&
				weapon->getAmmoForAction(BA_SNAPSHOT) &&
				BattleActionCost(BA_SNAPSHOT, unit, weapon).haveTU())
			{
				setReaction(reaction, BA_SNAPSHOT, weapon);
				return reaction;
			}
		}
	}

	return reaction;
}

/**
 * Attempts to perform a reaction snap shot.
 * @param unit The unit to check sight from.
 * @param target The unit to check sight TO.
 * @return True if the action should (theoretically) succeed.
 */
bool TileEngine::tryReaction(ReactionScore *reaction, BattleUnit *target, const BattleAction &originalAction)
{
	BattleAction action;
	action.cameraPosition = _save->getBattleState()->getMap()->getCamera()->getMapOffset();
	action.actor = reaction->unit;
	action.weapon = reaction->weapon;
	action.type = reaction->attackType;

	if (!_save->canUseWeapon(action.weapon, action.actor, false, action.type))
	{
		return false;
	}

	action.target = target->getPosition();
	action.updateTU();

	auto unit = action.actor;
	auto ammo = action.weapon->getAmmoForAction(action.type);
	if (ammo && action.haveTU())
	{
		action.targeting = true;

		// hostile units will go into an "aggro" state when they react.
		if (unit->getFaction() == FACTION_HOSTILE)
		{
			AIModule *ai = unit->getAIModule();
			if (ai == 0)
			{
				// should not happen, but just in case...
				ai = new AIModule(_save, unit, 0);
				unit->setAIModule(ai);
			}

			int radius = ammo->getRules()->getExplosionRadius({ action.type, action.actor, action.weapon, ammo });
			if (action.type != BA_HIT && radius > 0 &&
				ai->explosiveEfficacy(action.target, unit, radius, -1) == 0)
			{
				action.targeting = false;
			}
		}

		if (action.targeting)
		{
			int moveType = originalAction.getMoveType();
			int meleeReactionChance = Mod::EXTENDED_MELEE_REACTIONS > 0 ? 100 : 0;
			int reactionChance = BA_HIT != originalAction.type ? 100 : meleeReactionChance;
			int dist = Position::distance2d(unit->getPositionVexels(), target->getPositionVexels());
			int arc = getArcDirection(getDirectionTo(unit->getPositionVexels(), target->getPositionVexels()), unit->getDirection());
			auto *origTarg = _save->getTile(originalAction.target) ? _save->getTile(originalAction.target)->getUnit() : nullptr;

			ModScript::ReactionCommon::Output arg{ reactionChance, dist };
			ModScript::ReactionCommon::Worker worker{ target, unit, action.weapon, action.type, reaction->count, originalAction.weapon, originalAction.skillRules, originalAction.type, origTarg, moveType, arc, _save };
			if (originalAction.weapon)
			{
				worker.execute(originalAction.weapon->getRules()->getScript<ModScript::ReactionWeaponAction>(), arg);
			}

			//Use AI to check whether our shot should actually hit
			if (!action.actor->getAIModule())
			{
				// for some reason the unit had no AI routine assigned..
				action.actor->setAIModule(new AIModule(_save, action.actor, 0));
			}
			if (action.actor->getAIModule()->brutalScoreFiringMode(&action, target, true, true) <= 0)
				return false;

			worker.execute(target->getArmor()->getScript<ModScript::ReactionUnitAction>(), arg);

			worker.execute(unit->getArmor()->getScript<ModScript::ReactionUnitReaction>(), arg);

			if (RNG::percent(arg.getFirst()))
			{
				_save->appendToHitLog(HITLOG_REACTION_FIRE, unit->getFaction());

				if (action.type == BA_HIT)
				{
					_save->getBattleGame()->statePushBack(new MeleeAttackBState(_save->getBattleGame(), action));
				}
				else
				{
					_save->getBattleGame()->statePushBack(new ProjectileFlyBState(_save->getBattleGame(), action));
				}
				return true;
			}
		}
	}
	return false;
}

/**
 * Handling of hitting tile.
 * @param tile targeted tile.
 * @param damage power of hit.
 * @param type damage type of hit.
 * @return whether a smoke (1) or fire (2) effect was produced
 */
int TileEngine::hitTile(Tile* tile, int damage, const RuleDamageType* type)
{
	if (damage >= type->SmokeThreshold)
	{
		// smoke from explosions always stay 6 to 14 turns - power of a smoke grenade is 60
		if (tile->getSmoke() < _save->getBattleGame()->getMod()->getTooMuchSmokeThreshold() && tile->getTerrainLevel() > -24)
		{
			tile->setFire(0);
			if (damage >= type->SmokeThreshold * 2)
				tile->setSmoke(RNG::generate(7, 15)); // for SmokeThreshold == 0
			else
				tile->setSmoke(RNG::generate(7, 15) * (damage - type->SmokeThreshold) / type->SmokeThreshold);
			return 1;
		}
	}
	else if (damage >= type->FireThreshold)
	{
		if (!tile->isVoid())
		{
			if (tile->getFire() == 0 && (tile->getMapData(O_FLOOR) || tile->getMapData(O_OBJECT)))
			{
				if (damage >= type->FireThreshold * 2)
					tile->setFire(tile->getFuel() + 1); // for FireThreshold == 0
				else
					tile->setFire(tile->getFuel() * (damage - type->FireThreshold) / type->FireThreshold + 1);
				tile->setSmoke(std::max(1, std::min(15 - (tile->getFlammability() / 10), 12)));
				return 2;
			}
		}
	}
	return 0;
}

/**
 * Handling of experience training.
 * @param unit hitter.
 * @param weapon weapon causing the damage.
 * @param target targeted unit.
 * @param rangeAtack is ranged attack or not?
 * @return Was experience awarded or not?
 */
bool TileEngine::awardExperience(BattleActionAttack attack, BattleUnit *target, bool rangeAtack)
{
	if (_save->isPreview())
	{
		return false;
	}

	auto unit = attack.attacker;
	auto weapon = attack.weapon_item;

	if (!target)
	{
		return false;
	}

	if (!weapon)
	{
		return false;
	}

	// Mana experience - this is a temporary/experimental approach, can be improved later after modder feedback
	unit->addManaExp(weapon->getRules()->getManaExperience());

	using upExpType = void (BattleUnit::*)();

	ExperienceTrainingMode expType = weapon->getRules()->getExperienceTrainingMode();
	upExpType expFuncA = nullptr;
	upExpType expFuncB = nullptr;
	int expMultiply = 100;

	if (expType > ETM_DEFAULT)
	{
		// can train psi strength and psi skill only if psi skill is already > 0
		if (expType >= ETM_PSI_STRENGTH && expType <= ETM_PSI_STRENGTH_OR_SKILL_2X)
		{
			// cannot use "unit->getBaseStats()->psiSkill", because armor can give +psiSkill bonus
			if (unit->getGeoscapeSoldier() && unit->getGeoscapeSoldier()->getCurrentStats()->psiSkill <= 0)
				return false;
		}

		switch (weapon->getRules()->getExperienceTrainingMode())
		{
		case ETM_MELEE_100: expFuncA = &BattleUnit::addMeleeExp; break;
		case ETM_MELEE_50: expMultiply = 50; expFuncA = &BattleUnit::addMeleeExp; break;
		case ETM_MELEE_33: expMultiply = 33; expFuncA = &BattleUnit::addMeleeExp; break;
		case ETM_FIRING_100: expFuncA = &BattleUnit::addFiringExp; break;
		case ETM_FIRING_50: expMultiply = 50; expFuncA = &BattleUnit::addFiringExp; break;
		case ETM_FIRING_33: expMultiply = 33; expFuncA = &BattleUnit::addFiringExp; break;
		case ETM_THROWING_100: expFuncA = &BattleUnit::addThrowingExp; break;
		case ETM_THROWING_50: expMultiply = 50; expFuncA = &BattleUnit::addThrowingExp; break;
		case ETM_THROWING_33: expMultiply = 33; expFuncA = &BattleUnit::addThrowingExp; break;
		case ETM_FIRING_AND_THROWING: expFuncA = &BattleUnit::addFiringExp; expFuncB = &BattleUnit::addThrowingExp; break;
		case ETM_FIRING_OR_THROWING: if (RNG::percent(50)) { expFuncA = &BattleUnit::addFiringExp; } else { expFuncA = &BattleUnit::addThrowingExp; } break;
		case ETM_REACTIONS: expMultiply = 100; expFuncA = &BattleUnit::addReactionExp; break;
		case ETM_REACTIONS_AND_MELEE: expFuncA = &BattleUnit::addReactionExp; expFuncB = &BattleUnit::addMeleeExp; break;
		case ETM_REACTIONS_AND_FIRING: expFuncA = &BattleUnit::addReactionExp; expFuncB = &BattleUnit::addFiringExp; break;
		case ETM_REACTIONS_AND_THROWING: expFuncA = &BattleUnit::addReactionExp; expFuncB = &BattleUnit::addThrowingExp; break;
		case ETM_REACTIONS_OR_MELEE: if (RNG::percent(50)) { expFuncA = &BattleUnit::addReactionExp; } else { expFuncA = &BattleUnit::addMeleeExp; } break;
		case ETM_REACTIONS_OR_FIRING: if (RNG::percent(50)) { expFuncA = &BattleUnit::addReactionExp; } else { expFuncA = &BattleUnit::addFiringExp; } break;
		case ETM_REACTIONS_OR_THROWING: if (RNG::percent(50)) { expFuncA = &BattleUnit::addReactionExp; } else { expFuncA = &BattleUnit::addThrowingExp; } break;
		case ETM_BRAVERY: expFuncA = &BattleUnit::addBraveryExp; break;
		case ETM_BRAVERY_2X: expMultiply = 200; expFuncA = &BattleUnit::addBraveryExp; break;
		case ETM_BRAVERY_AND_REACTIONS: expFuncA = &BattleUnit::addBraveryExp; expFuncB = &BattleUnit::addReactionExp; break;
		case ETM_BRAVERY_OR_REACTIONS: if (RNG::percent(50)) { expFuncA = &BattleUnit::addBraveryExp; } else { expFuncA = &BattleUnit::addReactionExp; } break;
		case ETM_BRAVERY_OR_REACTIONS_2X: expMultiply = 200; if (RNG::percent(50)) { expFuncA = &BattleUnit::addBraveryExp; } else { expFuncA = &BattleUnit::addReactionExp; } break;
		case ETM_PSI_STRENGTH: expFuncA = &BattleUnit::addPsiStrengthExp; break;
		case ETM_PSI_STRENGTH_2X: expMultiply = 200; expFuncA = &BattleUnit::addPsiStrengthExp; break;
		case ETM_PSI_SKILL: expFuncA = &BattleUnit::addPsiSkillExp; break;
		case ETM_PSI_SKILL_2X: expMultiply = 200; expFuncA = &BattleUnit::addPsiSkillExp; break;
		case ETM_PSI_STRENGTH_AND_SKILL: expFuncA = &BattleUnit::addPsiStrengthExp; expFuncB = &BattleUnit::addPsiSkillExp; break;
		case ETM_PSI_STRENGTH_AND_SKILL_2X: expMultiply = 200; expFuncA = &BattleUnit::addPsiStrengthExp; expFuncB = &BattleUnit::addPsiSkillExp; break;
		case ETM_PSI_STRENGTH_OR_SKILL: if (RNG::percent(50)) { expFuncA = &BattleUnit::addPsiStrengthExp; } else { expFuncA = &BattleUnit::addPsiSkillExp; } break;
		case ETM_PSI_STRENGTH_OR_SKILL_2X: expMultiply = 200; if (RNG::percent(50)) { expFuncA = &BattleUnit::addPsiStrengthExp; } else { expFuncA = &BattleUnit::addPsiSkillExp; } break;
		case ETM_NOTHING:
		default:
			return false;
		}
	}
	else
	{
		// GRENADES AND PROXIES
		if (weapon->getRules()->getBattleType() == BT_GRENADE || weapon->getRules()->getBattleType() == BT_PROXIMITYGRENADE)
		{
			if (Mod::EXTENDED_EXPERIENCE_AWARD_SYSTEM)
			{
				expType = ETM_THROWING_100;
				expFuncA = &BattleUnit::addThrowingExp; // e.g. acid grenade, stun grenade, HE grenade, smoke grenade, proxy grenade, ...
			}
			else
			{
				expType = ETM_FIRING_100;
				expFuncA = &BattleUnit::addFiringExp; // vanilla compatibility
			}
		}
		// MELEE
		else if (weapon->getRules()->getBattleType() == BT_MELEE)
		{
			expType = ETM_MELEE_100;
			expFuncA = &BattleUnit::addMeleeExp; // e.g. cattle prod, cutlass, rope, ...
		}
		// MEDIKITS
		else if (weapon->getRules()->getBattleType() == BT_MEDIKIT)
		{
			// medikits don't train anything by default
			return false;
		}
		// FIREARMS and other
		else
		{
			if (!rangeAtack)
			{
				expType = ETM_MELEE_100;
				expFuncA = &BattleUnit::addMeleeExp; // e.g. rifle/shotgun gun butt, ...
			}
			else if (!Mod::EXTENDED_EXPERIENCE_AWARD_SYSTEM)
			{
				expType = ETM_FIRING_100;
				expFuncA = &BattleUnit::addFiringExp; // vanilla compatibility
			}
			else if (weapon->getArcingShot(attack.type))
			{
				expType = ETM_THROWING_100;
				expFuncA = &BattleUnit::addThrowingExp; // e.g. flamethrower, javelins, combat bow, grenade launcher, black powder bomb, stick grenade, acid flask, apple, ...
			}
			else
			{
				int maxRange = weapon->getRules()->getMaxRange();
				if (maxRange > 10)
				{
					expType = ETM_FIRING_100;
					expFuncA = &BattleUnit::addFiringExp; // e.g. harpoon gun, shotgun, assault rifle, rocket launcher, small launcher, heavy cannon, blaster launcher, ...
				}
				else if (maxRange > 1)
				{
					expType = ETM_THROWING_100;
					expFuncA = &BattleUnit::addThrowingExp; // e.g. throwing knives, zapper, ...
				}
				else if (maxRange == 1)
				{
					expType = ETM_MELEE_100;
					expFuncA = &BattleUnit::addMeleeExp; // e.g. hammer, chainsaw, fusion torch, ...
				}
				else
				{
					return false; // what is this? no training!
				}
			}
		}
	}

	if (weapon->getRules()->getBattleType() != BT_MEDIKIT)
	{
		// only enemies count, not friends or neutrals
		if (target->getOriginalFaction() != FACTION_HOSTILE) expMultiply = 0;

		if (Mod::EXTENDED_EXPERIENCE_AWARD_SYSTEM)
		{
			// mind-controlled enemies don't count though!
			if (target->getFaction() != FACTION_HOSTILE) expMultiply = 0;
		}
	}

	expMultiply = ModScript::scriptFunc2<ModScript::AwardExperience>(
		target->getArmor(),
		expMultiply, expType,
		unit, target, weapon, attack.type
	);

	for (int i = expMultiply / 100; i > 0; --i)
	{
		if (expFuncA) (unit->*expFuncA)();
		if (expFuncB) (unit->*expFuncB)();
	}
	if (RNG::percent(expMultiply % 100))
	{
		if (expFuncA) (unit->*expFuncA)();
		if (expFuncB) (unit->*expFuncB)();
	}

	return true;
}

/**
 * Handling of hitting unit.
 * @param unit hitter.
 * @param clipOrWeapon clip or weapon causing the damage.
 * @param target targeted unit.
 * @param relative angle of hit.
 * @param damage power of hit.
 * @param type damage type of hit.
 * @return Did unit get hit?
 */
bool TileEngine::hitUnit(BattleActionAttack attack, BattleUnit *target, const Position &relative, int damage, const RuleDamageType *type, bool rangeAtack)
{
	if (_save->isPreview())
	{
		return false;
	}
	if (!target || target->getHealth() <= 0)
	{
		return false;
	}

	const int healthOrig = target->getHealth();
	const int stunLevelOrig = target->getStunlevel();

	target->damage(relative, damage, type, _save, attack);

	const int healthDamage = healthOrig - target->getHealth();
	const int stunDamage = target->getStunlevel() - stunLevelOrig;

	// hit log
	if (attack.attacker)
	{
		if (healthDamage > 0 || stunDamage > 0)
		{
			int damagePercent = ((healthDamage + stunDamage) * 100) / target->getBaseStats()->health;
			if (damagePercent <= 20)
			{
				_save->appendToHitLog(HITLOG_SMALL_DAMAGE, attack.attacker->getFaction());
			}
			else
			{
				_save->appendToHitLog(HITLOG_BIG_DAMAGE, attack.attacker->getFaction());
			}
		}
		else
		{
			_save->appendToHitLog(HITLOG_NO_DAMAGE, attack.attacker->getFaction());
		}
	}

	// single place for firing/throwing/melee experience training
	if (attack.attacker && attack.attacker->getOriginalFaction() == FACTION_PLAYER)
	{
		awardExperience(attack, target, rangeAtack);
	}

	// Use case: an xcom soldier throwing a smoke grenade on a dying unit should not override the previously remembered murderer
	bool isRelevant = true;
	if (attack.attacker
		&& healthDamage <= 0
		&& target->getMurdererId() > 0
		&& (target->getFire() > 0 || target->getFatalWounds() > 0 || target->hasNegativeHealthRegen()))
	{
		isRelevant = false;
	}

	if (isRelevant && attack.attacker)
	{
		// Record the last unit to hit our victim. If a victim dies without warning*, this unit gets the credit.
		// *Because the unit died in a fire or bled out.
		target->setMurdererId(attack.attacker->getId());
		target->setMurdererWeapon("STR_WEAPON_UNKNOWN");
		target->setMurdererWeaponAmmo("STR_WEAPON_UNKNOWN");
		if (attack.weapon_item)
		{
			target->setMurdererWeapon(attack.weapon_item->getRules()->getName());
		}
		if (attack.damage_item)
		{
			target->setMurdererWeaponAmmo(attack.damage_item->getRules()->getName());
		}
	}

	return true;
}

/**
 * Handles bullet/weapon hits.
 *
 * A bullet/weapon hits a voxel.
 * @param center Center of the explosion in voxelspace.
 * @param power Power of the explosion.
 * @param type The damage type of the explosion.
 * @param unit The unit that caused the explosion.
 * @param clipOrWeapon clip or weapon causing the damage.
 */
void TileEngine::hit(BattleActionAttack attack, Position center, int power, const RuleDamageType *type, bool rangeAtack, int terrainMeleeTilePart)
{
	bool terrainChanged = false; //did the hit destroy a tile thereby changing line of sight?
	int effectGenerated = 0; //did the hit produce smoke (1), fire/light (2) or disabled a unit (3) ?
	Position tilePos = center.toTile();
	Tile *tile = _save->getTile(tilePos);
	if (!tile || power <= 0)
	{
		return;
	}

	voxelCheckFlush();
	const auto part = (terrainMeleeTilePart > 0) ? (VoxelType)terrainMeleeTilePart : voxelCheck(center, attack.attacker);
	const auto damage = type->getRandomDamage(power);
	const auto tileFinalDamage = type->getTileFinalDamage(type->getRandomDamageForTile(power, damage));
	if (part >= V_FLOOR && part <= V_OBJECT)
	{
		bool nothing = true;
		if (terrainMeleeTilePart == 0 && (part == V_FLOOR || part == V_OBJECT))
		{
			for (auto* bi : *tile->getInventory())
			{
				if (hitUnit(attack, bi->getUnit(), Position(0,0,0), damage, type, rangeAtack))
				{
					if (bi->getGlow()) effectGenerated = 2; //Any glowing corpses?
					nothing = false;
					break;
				}
			}
		}
		if (nothing)
		{
			const auto tp = static_cast<TilePart>(part);
			//Do we need to update the visibility of units due to smoke/fire?
			effectGenerated = hitTile(tile, damage, type);
			//If a tile was destroyed we may have revealed new areas for one or more observers
			if (tileFinalDamage >= tile->getMapData(tp)->getArmor()) terrainChanged = true;

			if (part == V_OBJECT && _save->getMissionType() == "STR_BASE_DEFENSE")
			{
				if (tileFinalDamage >= tile->getMapData(O_OBJECT)->getArmor() && tile->getMapData(O_OBJECT)->isBaseModule())
				{
					_save->getModuleMap()[(center.x/16)/10][(center.y/16)/10].second--;
				}
			}
			if (tile->damage(tp, tileFinalDamage, _save->getObjectiveType()))
			{
				_save->addDestroyedObjective();
			}
		}
	}
	else if (part == V_UNIT)
	{
		BattleUnit *bu = tile->getOverlappingUnit(_save);
		if (bu && bu->getHealth() > 0)
		{
			int verticaloffset = 0;
			if (bu != tile->getUnit())
			{
				verticaloffset = 24;
			}
			const int sz = bu->getArmor()->getSize() * 8;
			const Position target = bu->getPosition().toVoxel() + Position(sz,sz, bu->getFloatHeight() - tile->getTerrainLevel());
			const Position relative = (center - target) - Position(0,0,verticaloffset);

			hitUnit(attack, bu, relative, damage, type, rangeAtack);
			if (bu->getFire())
			{
				effectGenerated = 2;
			}
		}
	}
	//Recalculate relevant item/unit locations and visibility depending on what happened during the hit
	if (terrainChanged || effectGenerated)
	{
		resetVisibilityCache();
		applyGravity(tile);
		auto layer = LL_ITEMS;
		if (part == V_FLOOR && _save->getTile(tilePos - Position(0, 0, 1)))
		{
			layer = LL_AMBIENT; // roof destroyed, update sunlight in this tile column
		}
		else if (terrainChanged || effectGenerated)
		{
			layer = LL_FIRE; // spawned fire or smoke that can block light.
		}
		calculateLighting(layer, tilePos, 1, true);
		calculateFOV(tilePos, 1, true, terrainChanged); //append any new units or tiles revealed by the terrain change
	}
	else
	{
		// script could affect visibility of units, fast check if something is changed.
		calculateFOV(tilePos, 1, false); //skip updating of tiles
	}
	//Note: If bu was knocked out this will have no effect on unit visibility quite yet, as it is not marked as out
	//and will continue to block visibility at this point in time.
}

/**
 * Handles explosions.
 *
 * HE, smoke and fire explodes in a circular pattern on 1 level only. HE however damages floor tiles of the above level. Not the units on it.
 * HE destroys an object if its armor is lower than the explosive power, then it's HE blockage is applied for further propagation.
 * See http://www.ufopaedia.org/index.php?title=Explosions for more info.
 * @param center Center of the explosion in voxelspace.
 * @param power Power of the explosion.
 * @param type The damage type of the explosion.
 * @param maxRadius The maximum radius of the explosion.
 * @param unit The unit that caused the explosion.
 * @param clipOrWeapon The clip or weapon that caused the explosion.
 */
void TileEngine::explode(BattleActionAttack attack, Position center, int power, const RuleDamageType *type, int maxRadius, bool rangeAtack)
{
	const Position centetTile = center.toTile();
	int hitSide = 0;
	int diagonalWall = 0;
	int power_;
	std::map<Tile*, int> tilesAffected;
	std::vector<BattleItem*> toRemove;
	std::pair<std::map<Tile*, int>::iterator, bool> ret;

	if (type->FireBlastCalc)
	{
		power /= 2;
	}

	int exHeight = Clamp(Options::battleExplosionHeight, 0, 3);
	int vertdec = 1000; //default flat explosion

	switch (exHeight)
	{
	case 1:
		vertdec = 3.0f * type->RadiusReduction;
		break;
	case 2:
		vertdec = 1.0f * type->RadiusReduction;
		break;
	case 3:
		vertdec = 0.5f * type->RadiusReduction;
	}

	Tile *origin = _save->getTile(Position(centetTile));
	Tile *dest = nullptr;
	if (origin->isBigWall()) //pre-calculations for bigwall deflection
	{
		diagonalWall = origin->getMapData(O_OBJECT)->getBigWall();
		if (diagonalWall == Pathfinding::BIGWALLNWSE) //  3 |
			hitSide = (center.x % 16 - center.y % 16) > 0 ? 1 : -1;
		if (diagonalWall == Pathfinding::BIGWALLNESW) //  2 --
			hitSide = (center.x % 16 + center.y % 16 - 15) > 0 ? 1 : -1;
	}

	for (int fi = -90; fi <= 90; fi += 5)
	{
		// raytrace every 3 degrees makes sure we cover all tiles in a circle.
		for (int te = 0; te <= 360; te += 3)
		{
			double cos_te = cos(Deg2Rad(te));
			double sin_te = sin(Deg2Rad(te));
			double sin_fi = sin(Deg2Rad(fi));
			double cos_fi = cos(Deg2Rad(fi));

			origin = _save->getTile(centetTile);
			dest = origin;
			double l = 0;
			int tileX, tileY, tileZ;
			power_ = power;
			while (power_ > 0 && l <= maxRadius)
			{
				if (power_ > 0)
				{
					ret = tilesAffected.insert(std::make_pair(dest, 0)); // check if we had this tile already affected

					const int tileDmg = type->getTileFinalDamage(power_);
					if (tileDmg > ret.first->second)
					{
						ret.first->second = tileDmg;
					}
					if (ret.second)
					{
						const int damage = type->getRandomDamage(power_);
						BattleUnit *bu = dest->getOverlappingUnit(_save);

						toRemove.clear();
						if (bu)
						{
							if (dest->getPosition() == centetTile)
							{
								// direct hit, similar to ground zero but AI will remember attacker, done for compatibility
								hitUnit(attack, bu, Position(0, 0, 0), damage, type, rangeAtack);
							}
							else if (
									(
										Position::distance2dSq(dest->getPosition(), centetTile) < 4
										&& dest->getPosition().z == centetTile.z
									)
									|| dest->getPosition().z > centetTile.z
								)
							{
								// ground zero effect is in effect, or unit is above explosion
								hitUnit(attack, bu, Position(0, 0, -1), damage, type, rangeAtack);
							}
							else
							{
								// directional damage relative to explosion position.
								// units above the explosion will be hit in the legs, units lateral to or below will be hit in the torso
								hitUnit(attack, bu, centetTile + Position(0, 0, 5) - dest->getPosition(), damage, type, rangeAtack);
							}

							// Affect all items and units in inventory
							const int itemDamage = bu->getOverKillDamage();
							if (itemDamage > 0)
							{
								for (auto* bi : *bu->getInventory())
								{
									if (!hitUnit(attack, bi->getUnit(), Position(0, 0, 0), itemDamage, type, rangeAtack) && type->getItemFinalDamage(itemDamage) > bi->getRules()->getArmor())
									{
										toRemove.push_back(bi);
									}
								}
							}
						}
						// Affect all items and units on ground
						for (auto* bi : *dest->getInventory())
						{
							if (!hitUnit(attack, bi->getUnit(), Position(0, 0, 0), damage, type) && type->getItemFinalDamage(damage) > bi->getRules()->getArmor())
							{
								toRemove.push_back(bi);
							}
						}
						for (auto* bi : toRemove)
						{
							_save->removeItem(bi);
						}

						hitTile(dest, damage, type);
					}
				}

				l += 1.0;

				tileX = int(floor(centetTile.x + 0.5 + l * sin_te * cos_fi));
				tileY = int(floor(centetTile.y + 0.5 + l * cos_te * cos_fi));
				tileZ = int(floor(centetTile.z + 0.5 + l * sin_fi));

				origin = dest;
				dest = _save->getTile(Position(tileX, tileY, tileZ));

				if (!dest) break; // out of map!

				// blockage by terrain is deducted from the explosion power
				power_ -= type->RadiusReduction; // explosive damage decreases by 10 per tile
				if (origin->getPosition().z != tileZ)
					power_ -= vertdec; //3d explosion factor

				if (type->FireBlastCalc)
				{
					int dir;
					Pathfinding::vectorToDirection(origin->getPosition() - dest->getPosition(), dir);
					if (dir != -1 && dir %2) power_ -= 0.5f * type->RadiusReduction; // diagonal movement costs an extra 50% for fire.
				}
				if (l > 0.5) {
					if ( l > 1.5)
					{
						power_ -= verticalBlockage(origin, dest, type->ResistType, false) * 2;
						power_ -= horizontalBlockage(origin, dest, type->ResistType, false) * 2;
					}
					else //tricky bigwall deflection /Volutar
					{
						bool skipObject = diagonalWall == 0;
						if (diagonalWall == Pathfinding::BIGWALLNESW) // --
						{
							if (hitSide<0 && te >= 135 && te < 315)
								skipObject = true;
							if (hitSide>0 && ( te < 135 || te > 315))
								skipObject = true;
						}
						if (diagonalWall == Pathfinding::BIGWALLNWSE) // |
						{
							if (hitSide>0 && te >= 45 && te < 225)
								skipObject = true;
							if (hitSide<0 && ( te < 45 || te > 225))
								skipObject = true;
						}
						power_ -= verticalBlockage(origin, dest, type->ResistType, skipObject) * 2;
						power_ -= horizontalBlockage(origin, dest, type->ResistType, skipObject) * 2;

					}
				}
			}
		}
	}

	// now detonate the tiles affected by explosion
	if (type->ToTile > 0.0f)
	{
		for (auto& pair : tilesAffected)
		{
			if (detonate(pair.first, pair.second))
			{
				_save->addDestroyedObjective();
			}
			applyGravity(pair.first);
			Tile *j = _save->getTile(pair.first->getPosition() + Position(0,0,1));
			if (j)
				applyGravity(j);
		}
	}
	calculateLighting(LL_AMBIENT, centetTile, maxRadius + 1, true); // roofs could have been destroyed and fires could have been started
	calculateFOV(centetTile, maxRadius + 1, true, true);
	if (attack.attacker && Position::distance2d(centetTile, attack.attacker->getPosition()) > maxRadius + 1)
	{
		// unit is away from blast but its visibility can be affected by scripts.
		calculateFOV(centetTile, 1, false);
	}
}

/**
 * Applies the explosive power to the tile parts. This is where the actual destruction takes place.
 * Must affect 9 objects (6 box sides and the object inside plus 2 outer walls).
 * @param tile Tile affected.
 * @return True if the objective was destroyed.
 */
bool TileEngine::detonate(Tile* tile, int explosive)
{
	if (explosive == 0) return false; // no damage applied for this tile
	bool objective = false;
	Tile* tiles[9];
	static const TilePart parts[9]={O_FLOOR,O_WESTWALL,O_NORTHWALL,O_FLOOR,O_WESTWALL,O_NORTHWALL,O_OBJECT,O_OBJECT,O_OBJECT}; //6th is the object of current
	Position pos = tile->getPosition();

	tiles[0] = _save->getTile(Position(pos.x, pos.y, pos.z+1)); //ceiling
	tiles[1] = _save->getTile(Position(pos.x+1, pos.y, pos.z)); //east wall
	tiles[2] = _save->getTile(Position(pos.x, pos.y+1, pos.z)); //south wall
	tiles[3] = tiles[4] = tiles[5] = tiles[6] = tile;
	tiles[7] = _save->getTile(Position(pos.x, pos.y-1, pos.z)); //north bigwall
	tiles[8] = _save->getTile(Position(pos.x-1, pos.y, pos.z)); //west bigwall

	int remainingPower, fireProof, fuel;
	bool destroyed, bigwalldestroyed = true, skipnorthwest = false;
	for (int i = 8; i >=0; --i)
	{
		if (!tiles[i] || !tiles[i]->getMapData(parts[i]))
			continue; //skip out of map and emptiness
		int bigwall = tiles[i]->getMapData(parts[i])->getBigWall();
		if (i > 6 && !( (bigwall==1) || (bigwall==8) || (i==8 && bigwall==6) || (i==7 && bigwall==7)))
			continue;
		if ((bigwall!=0)) skipnorthwest = true;
		if (!bigwalldestroyed && i<6) //when ground shouldn't be destroyed
			continue;
		if (skipnorthwest && (i == 2 || i == 1)) continue;
		remainingPower = explosive;
		destroyed = false;
		int volume = 0;
		TilePart currentpart = parts[i], currentpart2;
		int diemcd;
		fireProof = tiles[i]->getFlammability(currentpart);
		fuel = tiles[i]->getFuel(currentpart) + 1;
		// get the volume of the object by checking it's loftemps objects.
		for (int j = 0; j < 12; j++)
		{
			if (tiles[i]->getMapData(currentpart)->getLoftID(j) != 0)
				++volume;
		}
		if ( i == 6 &&
			(bigwall == 2 || bigwall == 3) && //diagonals
			tiles[i]->getMapData(currentpart)->getArmor() > remainingPower) //not enough to destroy
		{
			bigwalldestroyed = false;
		}
		// iterate through tile armor and destroy if can
		while (	tiles[i]->getMapData(currentpart) &&
				tiles[i]->getMapData(currentpart)->getArmor() <= remainingPower &&
				tiles[i]->getMapData(currentpart)->getArmor() != 255)
		{
			if ( i == 6 && (bigwall == 2 || bigwall == 3)) //diagonals for the current tile
			{
				bigwalldestroyed = true;
			}
			if ( i == 6 && (bigwall == 6 || bigwall == 7 || bigwall == 8)) //n/w/nw
			{
				skipnorthwest = false;
			}
			remainingPower -= tiles[i]->getMapData(currentpart)->getArmor();
			destroyed = true;
			if (_save->getMissionType() == "STR_BASE_DEFENSE" &&
				tiles[i]->getMapData(currentpart)->isBaseModule())
			{
				_save->getModuleMap()[tile->getPosition().x/10][tile->getPosition().y/10].second--;
			}
			//this trick is to follow transformed object parts (object can become a ground)
			diemcd = tiles[i]->getMapData(currentpart)->getDieMCD();
			if (diemcd!=0)
				currentpart2 = tiles[i]->getMapData(currentpart)->getDataset()->getObject(diemcd)->getObjectType();
			else
				currentpart2 = currentpart;
			if (tiles[i]->destroy(currentpart, _save->getObjectiveType()))
				objective = true;
			currentpart =  currentpart2;
			if (tiles[i]->getMapData(currentpart)) // take new values
			{
				fireProof = tiles[i]->getFlammability(currentpart);
				fuel = tiles[i]->getFuel(currentpart) + 1;
				if (tiles[i]->getMapData(currentpart)->getArmor() == 0)
					break;
			}
		}
		// set tile on fire
		if (fireProof < remainingPower)
		{
			if (tiles[i]->getMapData(O_FLOOR) || tiles[i]->getMapData(O_OBJECT))
			{
				tiles[i]->setFire(fuel);
				tiles[i]->setSmoke(Clamp(15 - (fireProof / 10), 1, 12));
			}
		}
		// add some smoke if tile was destroyed and not set on fire
		if (destroyed)
		{
			if (tiles[i]->getFire() && !tiles[i]->getMapData(O_FLOOR) && !tiles[i]->getMapData(O_OBJECT))
			{
				tiles[i]->setFire(0);// if the object set the floor on fire, and the floor was subsequently destroyed, the fire needs to go out
			}

			if (!tiles[i]->getFire())
			{
				int smoke = RNG::generate(1, (volume / 2) + 3) + (volume / 2);
				if (smoke > tiles[i]->getSmoke())
				{
					tiles[i]->setSmoke(Clamp(smoke, 0, 15));
				}
			}
		}
	}
	return objective;
}

/**
 * Checks for chained explosions.
 *
 * Chained explosions are explosions which occur after an explosive map object is destroyed.
 * May be due a direct hit, other explosion or fire.
 * @return tile on which a explosion occurred
 */
Tile *TileEngine::checkForTerrainExplosions()
{
	if (_save->isPreview())
	{
		return 0;
	}

	for (int i = 0; i < _save->getMapSizeXYZ(); ++i)
	{
		if (_save->getTile(i)->getExplosive())
		{
			return _save->getTile(i);
		}
	}
	return 0;
}

/**
 * Calculates the amount of power that is blocked going from one tile to another on a different level.
 * @param startTile The tile where the power starts.
 * @param endTile The adjacent tile where the power ends.
 * @param type The type of power/damage.
 * @return Amount of blockage of this power.
 */
int TileEngine::verticalBlockage(Tile *startTile, Tile *endTile, ItemDamageType type, bool skipObject)
{
	int block = 0;

	// safety check
	if (startTile == 0 || endTile == 0) return 255;

	auto startPos = startTile->getPosition();
	auto endPos = endTile->getPosition();
	int direction = endPos.z - startPos.z;

	if (direction == 0 ) return 0;

	Tile *tmpTile = nullptr;
	if (direction < 0) // down
	{
		tmpTile = startTile;
		block += blockage(tmpTile, O_FLOOR, type);
		if (!skipObject)
			block += blockage(tmpTile, O_OBJECT, type, Pathfinding::DIR_DOWN);
		if (startPos.x != endPos.x || startPos.y != endPos.y)
		{
			tmpTile = _save->getTile(Position(endPos.x, endPos.y, startPos.z));
			block += horizontalBlockage(startTile, tmpTile, type, skipObject);
			block += blockage(tmpTile, O_FLOOR, type);
			if (!skipObject)
				block += blockage(tmpTile, O_OBJECT, type, Pathfinding::DIR_DOWN);
		}
	}
	else if (direction > 0) // up
	{
		tmpTile = _save->getTile(Position(startPos.x, startPos.y, startPos.z + 1));
		block += blockage(tmpTile, O_FLOOR, type);
		if (!skipObject)
			block += blockage(tmpTile, O_OBJECT, type, Pathfinding::DIR_UP);
		if (startPos.x != endPos.x || startPos.y != endPos.y)
		{
			tmpTile = _save->getTile(Position(endPos.x, endPos.y, startPos.z + 1));
			block += horizontalBlockage(startTile, tmpTile, type, skipObject);
			block += blockage(tmpTile, O_FLOOR, type);
			if (!skipObject)
				block += blockage(tmpTile, O_OBJECT, type, Pathfinding::DIR_UP);
		}
	}

	return block;
}

/**
 * Calculates the amount of power that is blocked going from one tile to another on the same level.
 * @param startTile The tile where the power starts.
 * @param endTile The adjacent tile where the power ends.
 * @param type The type of power/damage.
 * @return Amount of blockage.
 */
int TileEngine::horizontalBlockage(Tile *startTile, Tile *endTile, ItemDamageType type, bool skipObject)
{
	const Position oneTileNorth = Position(0, -1, 0);
	const Position oneTileEast = Position(1, 0, 0);
	const Position oneTileSouth = Position(0, 1, 0);
	const Position oneTileWest = Position(-1, 0, 0);

	// safety check
	if (startTile == 0 || endTile == 0) return 255;

	auto startPos = startTile->getPosition();
	auto endPos = endTile->getPosition();
	if (startPos.z != endPos.z) return 0;
	Tile *tmpTile = nullptr;

	int direction;
	Pathfinding::vectorToDirection(endPos - startPos, direction);
	if (direction == -1) return 0;
	int block = 0;

	switch(direction)
	{
	case 0:	// north
		block = blockage(startTile, O_NORTHWALL, type);
		break;
	case 1: // north east
		if (type == DT_NONE) //this is two-way diagonal visibility check, used in original game
		{
			block = blockage(startTile, O_NORTHWALL, type) + blockage(endTile, O_WESTWALL, type); //up+right

			tmpTile = _save->getTile(startPos + oneTileNorth);
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNESW)
				block += blockage(tmpTile, O_OBJECT, type, 3);

			if (block == 0) break; //this way is opened

			tmpTile = _save->getTile(startPos + oneTileEast);
			block = blockage(tmpTile, O_NORTHWALL, type)
				+ blockage(tmpTile, O_WESTWALL, type); //right+up
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNESW)
				block += blockage(tmpTile, O_OBJECT, type, 7);
		}
		else
		{
			tmpTile = _save->getTile(startPos + oneTileEast);
			block = (blockage(startTile, O_NORTHWALL, type) + blockage(endTile,O_WESTWALL, type))/2
				+ (blockage(tmpTile, O_WESTWALL, type)
				+ blockage(tmpTile, O_NORTHWALL, type))/2;

			tmpTile = _save->getTile(startPos + oneTileNorth);
			block += (blockage(tmpTile, O_OBJECT, type, 4)
				+ blockage(tmpTile, O_OBJECT, type, 6))/2;
		}
		break;
	case 2: // east
		block = blockage(endTile, O_WESTWALL, type);
		break;
	case 3: // south east
		if (type == DT_NONE)
		{
			tmpTile = _save->getTile(startPos + oneTileSouth);
			block = blockage(tmpTile, O_NORTHWALL, type)
				+ blockage(endTile, O_WESTWALL, type); //down+right
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNWSE)
				block += blockage(tmpTile, O_OBJECT, type, 1);

			if (block == 0) break; //this way is opened

			tmpTile = _save->getTile(startPos + oneTileEast);
			block = blockage(tmpTile, O_WESTWALL, type)
				+ blockage(endTile, O_NORTHWALL, type); //right+down
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNWSE)
				block += blockage(tmpTile, O_OBJECT, type, 5);
		}
		else
		{
			block = (blockage(endTile, O_WESTWALL, type) + blockage(endTile, O_NORTHWALL, type))/2
				+ (blockage(_save->getTile(startPos + oneTileEast), O_WESTWALL, type)
				+ blockage(_save->getTile(startPos + oneTileSouth), O_NORTHWALL, type))/2;
			block += (blockage(_save->getTile(startPos + oneTileSouth), O_OBJECT, type, 0)
				+ blockage(_save->getTile(startPos + oneTileEast), O_OBJECT, type, 6))/2;
		}
		break;
	case 4: // south
		block = blockage(endTile,O_NORTHWALL, type);
		break;
	case 5: // south west
		if (type == DT_NONE)
		{
			tmpTile = _save->getTile(startPos + oneTileSouth);
			block = blockage(tmpTile, O_NORTHWALL, type)
				+ blockage(tmpTile, O_WESTWALL, type); //down+left
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNESW)
				block += blockage(tmpTile, O_OBJECT, type, 7);

			if (block == 0) break; //this way is opened

			block = blockage(startTile, O_WESTWALL, type) + blockage(endTile, O_NORTHWALL, type); //left+down
			tmpTile = _save->getTile(startPos + oneTileWest);
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNESW)
				block += blockage(tmpTile, O_OBJECT, type, 3);
		}
		else
		{
			block = (blockage(endTile,O_NORTHWALL, type) + blockage(startTile,O_WESTWALL, type))/2
				+ (blockage(_save->getTile(startPos + oneTileSouth),O_WESTWALL, type)
				+ blockage(_save->getTile(startPos + oneTileSouth),O_NORTHWALL, type))/2;
			block += (blockage(_save->getTile(startPos + oneTileSouth),O_OBJECT, type, 0)
				+ blockage(_save->getTile(startPos + oneTileWest),O_OBJECT, type, 2))/2;
		}
		break;
	case 6: // west
		block = blockage(startTile,O_WESTWALL, type);
		break;
	case 7: // north west

		if (type == DT_NONE)
		{
			tmpTile = _save->getTile(startPos + oneTileNorth);
			block = blockage(startTile, O_NORTHWALL, type)
				+ blockage(tmpTile, O_WESTWALL, type); //up+left
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNWSE)
				block += blockage(tmpTile, O_OBJECT, type, 5);

			if (block == 0) break; //this way is opened

			tmpTile = _save->getTile(startPos + oneTileWest);
			block = blockage(startTile, O_WESTWALL, type)
				+ blockage(tmpTile, O_NORTHWALL, type); //left+up
			if (tmpTile && tmpTile->getMapData(O_OBJECT) && tmpTile->getMapData(O_OBJECT)->getBigWall() != Pathfinding::BIGWALLNWSE)
				block += blockage(tmpTile, O_OBJECT, type, 1);
		}
		else
		{
			block = (blockage(startTile,O_WESTWALL, type) + blockage(startTile,O_NORTHWALL, type))/2
				+ (blockage(_save->getTile(startPos + oneTileNorth),O_WESTWALL, type)
				+ blockage(_save->getTile(startPos + oneTileWest),O_NORTHWALL, type))/2;
			block += (blockage(_save->getTile(startPos + oneTileNorth),O_OBJECT, type, 4)
				+ blockage(_save->getTile(startPos + oneTileWest),O_OBJECT, type, 2))/2;
		}
		break;
	}

	if (!skipObject || (type==DT_NONE && startTile->isBigWall()) )
		block += blockage(startTile,O_OBJECT, type, direction);

	if (type != DT_NONE)
	{
		// not too sure about removing this line,
		// i have a sneaking suspicion we might end up blocking things that we shouldn't

		//if (skipObject) return block;

		direction += 4;
		if (direction > 7)
			direction -= 8;
		if (endTile->isBigWall())
			block += blockage(endTile,O_OBJECT, type, direction, true);
	}
	else
	{
		if ( block <= 127 )
		{
			direction += 4;
			if (direction > 7)
				direction -= 8;
			if (blockage(endTile,O_OBJECT, type, direction, true) > 127){
				return -1; //hit bigwall, reveal bigwall tile
			}
		}
	}

	return block;
}

/**
 * Calculates the amount this certain wall or floor-part of the tile blocks.
 * @param startTile The tile where the power starts.
 * @param part The part of the tile the power needs to go through.
 * @param type The type of power/damage.
 * @param direction Direction the power travels.
 * @return Amount of blockage.
 */
int TileEngine::blockage(Tile *tile, const TilePart part, ItemDamageType type, int direction, bool checkingFromOrigin)
{
	int blockage = 0;

	if (tile == 0) return 255; // probably outside the map here

	MapData *mapData = tile->getMapData(part);
	if (mapData)
	{
		bool check = true;
		int wall = -1;
		if (direction != -1)
		{
			wall = tile->getMapData(O_OBJECT)->getBigWall();

			if (type != DT_SMOKE &&
				checkingFromOrigin &&
				(wall == Pathfinding::BIGWALLNESW ||
				wall == Pathfinding::BIGWALLNWSE))
			{
				check = false;
			}
			switch (direction)
			{
			case 0: // north
				if (wall == Pathfinding::BIGWALLWEST ||
					wall == Pathfinding::BIGWALLEAST ||
					wall == Pathfinding::BIGWALLSOUTH ||
					wall == Pathfinding::BIGWALLEASTANDSOUTH)
				{
					check = false;
				}
				break;
			case 1: // north east
				if (wall == Pathfinding::BIGWALLWEST ||
					wall == Pathfinding::BIGWALLSOUTH)
				{
					check = false;
				}
				break;
			case 2: // east
				if (wall == Pathfinding::BIGWALLNORTH ||
					wall == Pathfinding::BIGWALLSOUTH ||
					wall == Pathfinding::BIGWALLWEST ||
					wall == Pathfinding::BIGWALLWESTANDNORTH)
				{
					check = false;
				}
				break;
			case 3: // south east
				if (wall == Pathfinding::BIGWALLNORTH ||
					wall == Pathfinding::BIGWALLWEST ||
					wall == Pathfinding::BIGWALLWESTANDNORTH)
				{
					check = false;
				}
				break;
			case 4: // south
				if (wall == Pathfinding::BIGWALLWEST ||
					wall == Pathfinding::BIGWALLEAST ||
					wall == Pathfinding::BIGWALLNORTH ||
					wall == Pathfinding::BIGWALLWESTANDNORTH)
				{
					check = false;
				}
				break;
			case 5: // south west
				if (wall == Pathfinding::BIGWALLNORTH ||
					wall == Pathfinding::BIGWALLEAST)
				{
					check = false;
				}
				break;
			case 6: // west
				if (wall == Pathfinding::BIGWALLNORTH ||
					wall == Pathfinding::BIGWALLSOUTH ||
					wall == Pathfinding::BIGWALLEAST ||
					wall == Pathfinding::BIGWALLEASTANDSOUTH)
				{
					check = false;
				}
				break;
			case 7: // north west
				if (wall == Pathfinding::BIGWALLSOUTH ||
					wall == Pathfinding::BIGWALLEAST ||
					wall == Pathfinding::BIGWALLEASTANDSOUTH)
				{
					check = false;
				}
				break;
			case 8: // up
			case 9: // down
				if (wall != 0 && wall != Pathfinding::BLOCK)
				{
					check = false;
				}
				break;
			default:
				break;
			}
		}
		else if (part == O_FLOOR &&
					mapData->getBlock(type) == 0)
		{
			if (type != DT_NONE)
			{
				blockage += mapData->getArmor();
			}
			else if (!mapData->isNoFloor())
			{
				return 256;
			}
		}

		if (check)
		{
			// -1 means we have a regular wall, and anything over 0 means we have a bigwall.
			if (type == DT_SMOKE && wall != 0 && !tile->isUfoDoorOpen(part))
			{
				return 256;
			}
			blockage += mapData->getBlock(type);
		}
	}

	// open ufo doors are actually still closed behind the scenes
	// so a special trick is needed to see if they are open, if they are, they obviously don't block anything
	if (tile->isUfoDoorOpen(part))
		blockage = 0;

	return blockage;
}

/**
 * Opens a door (if any) by rightclick, or by walking through it. The unit has to face in the right direction.
 * @param unit Unit.
 * @param rClick Whether the player right clicked.
 * @param dir Direction.
 * @return -1 there is no door, you can walk through;
 *		  0 normal door opened, make a squeaky sound and you can walk through;
 *		  1 ufo door is starting to open, make a whoosh sound, don't walk through;
 *		  3 ufo door is still opening, don't walk through it yet. (have patience, futuristic technology...)
 *		  4 not enough TUs
 *		  5 would contravene fire reserve
 */
int TileEngine::unitOpensDoor(BattleUnit *unit, bool rClick, int dir)
{
	int door = -1;
	int TUCost = 0;
	int size = unit->getArmor()->getSize();
	int z = unit->getTile()->getTerrainLevel() < -12 ? 1 : 0; // if we're standing on stairs, check the tile above instead.
	int doorsOpened = 0;
	Position doorCentre;

	if (dir == -1)
	{
		dir = unit->getDirection();
	}
	Tile *tile;
	for (int x = 0; x < size && door == -1; x++)
	{
		for (int y = 0; y < size && door == -1; y++)
		{
			std::vector<std::pair<Position, TilePart> > checkPositions;
			tile = _save->getTile(unit->getPosition() + Position(x,y,z));
			if (!tile) continue;

			switch (dir)
			{
			case 0: // north
				checkPositions.push_back(std::make_pair(Position(0, 0, 0), O_NORTHWALL)); // origin
				if (x != 0)
				{
					checkPositions.push_back(std::make_pair(Position(0, -1, 0), O_WESTWALL)); // one tile north
				}
				break;
			case 1: // north east
				checkPositions.push_back(std::make_pair(Position(0, 0, 0), O_NORTHWALL)); // origin
				checkPositions.push_back(std::make_pair(Position(1, -1, 0), O_WESTWALL)); // one tile north-east
				if (rClick)
				{
					checkPositions.push_back(std::make_pair(Position(1, 0, 0), O_WESTWALL)); // one tile east
					checkPositions.push_back(std::make_pair(Position(1, 0, 0), O_NORTHWALL)); // one tile east
				}
				break;
			case 2: // east
				checkPositions.push_back(std::make_pair(Position(1, 0, 0), O_WESTWALL)); // one tile east
				break;
			case 3: // south-east
				if (!y)
					checkPositions.push_back(std::make_pair(Position(1, 1, 0), O_WESTWALL)); // one tile south-east
				if (!x)
					checkPositions.push_back(std::make_pair(Position(1, 1, 0), O_NORTHWALL)); // one tile south-east
				if (rClick)
				{
					checkPositions.push_back(std::make_pair(Position(1, 0, 0), O_WESTWALL)); // one tile east
					checkPositions.push_back(std::make_pair(Position(0, 1, 0), O_NORTHWALL)); // one tile south
				}
				break;
			case 4: // south
				checkPositions.push_back(std::make_pair(Position(0, 1, 0), O_NORTHWALL)); // one tile south
				break;
			case 5: // south-west
				checkPositions.push_back(std::make_pair(Position(0, 0, 0), O_WESTWALL)); // origin
				checkPositions.push_back(std::make_pair(Position(-1, 1, 0), O_NORTHWALL)); // one tile south-west
				if (rClick)
				{
					checkPositions.push_back(std::make_pair(Position(0, 1, 0), O_WESTWALL)); // one tile south
					checkPositions.push_back(std::make_pair(Position(0, 1, 0), O_NORTHWALL)); // one tile south
				}
				break;
			case 6: // west
				checkPositions.push_back(std::make_pair(Position(0, 0, 0), O_WESTWALL)); // origin
				if (y != 0)
				{
					checkPositions.push_back(std::make_pair(Position(-1, 0, 0), O_NORTHWALL)); // one tile west
				}
				break;
			case 7: // north-west
				checkPositions.push_back(std::make_pair(Position(0, 0, 0), O_WESTWALL)); // origin
				checkPositions.push_back(std::make_pair(Position(0, 0, 0), O_NORTHWALL)); // origin
				if (x)
				{
					checkPositions.push_back(std::make_pair(Position(-1, -1, 0), O_WESTWALL)); // one tile north
				}
				if (y)
				{
					checkPositions.push_back(std::make_pair(Position(-1, -1, 0), O_NORTHWALL)); // one tile north
				}
				if (rClick)
				{
					checkPositions.push_back(std::make_pair(Position(0, -1, 0), O_WESTWALL)); // one tile north
					checkPositions.push_back(std::make_pair(Position(-1, 0, 0), O_NORTHWALL)); // one tile west
				}
				break;
			default:
				break;
			}

			TilePart part = O_FLOOR;
			for (const auto& pair : checkPositions)
			{
				if (door != -1)
				{
					break; // loop finished
				}
				tile = _save->getTile(unit->getPosition() + Position(x,y,z) + pair.first);
				if (tile)
				{
					door = tile->openDoor(pair.second, unit, _save->getBattleGame()->getReservedAction(), rClick, _save->getBattleGame()->getKneelReserved());
					if (door != -1)
					{
						part = pair.second;
						if (door == 0)
						{
							++doorsOpened;
							doorCentre = unit->getPosition() + Position(x, y, z) + pair.first;
						}
						else if (door == 1)
						{
							std::pair<int, Position> adjacentDoors = checkAdjacentDoors(unit->getPosition() + Position(x,y,z) + pair.first, pair.second);
							doorsOpened += adjacentDoors.first + 1;
							doorCentre = adjacentDoors.second;
						}
					}
				}
			}
			if (door == 0 && rClick)
			{
				if (part == O_WESTWALL)
				{
					part = O_NORTHWALL;
				}
				else
				{
					part = O_WESTWALL;
				}
				TUCost = tile->getTUCost(part, unit->getMovementType());
			}
			else if (door == 1 || door == 4)
			{
				TUCost = tile->getTUCost(part, unit->getMovementType());
			}
		}
	}

	if (door == 0 || door == 1)
	{
		if (_save->getBattleGame()->checkReservedTU(unit, TUCost, 0))
		{
			if (unit->spendTimeUnits(TUCost))
			{
				calculateLighting(LL_FIRE, doorCentre, doorsOpened, true);
				// Update FOV through the doorway.
				calculateFOV(doorCentre, doorsOpened, true, true);
				resetVisibilityCache();
				unit->updateEnemyKnowledge(_save->getTileIndex(unit->getPosition()), true, true);
			}
			else return 4;
		}
		else return 5;
	}
	return door;
}

/**
 * Opens any doors connected to this part at this position,
 * Keeps processing till it hits a non-ufo-door.
 * @param pos The starting position
 * @param part The part to open, defines which direction to check.
 * @return <The number of adjacent doors opened, Position of door centre>
 */
std::pair<int, Position> TileEngine::checkAdjacentDoors(Position pos, TilePart part)
{
	Position offset;
	int adjacentDoorsOpened = 0;
	int doorOffset = 0;
	bool westSide = (part == O_WESTWALL);
	for (int i = 1;; ++i)
	{
		offset = westSide ? Position(0,i,0):Position(i,0,0);
		Tile *tile = _save->getTile(pos + offset);
		if (tile && tile->isUfoDoor(part))
		{
			int doorAdj = tile->openDoor(part);
			if (doorAdj == 1) //only expecting ufo doors
			{
				adjacentDoorsOpened++;
				doorOffset++;
			}
		}
		else break;
	}
	for (int i = -1;; --i)
	{
		offset = westSide ? Position(0,i,0):Position(i,0,0);
		Tile *tile = _save->getTile(pos + offset);
		if (tile && tile->isUfoDoor(part))
		{
			int doorAdj = tile->openDoor(part);
			if (doorAdj == 1)
			{
				adjacentDoorsOpened++;
				doorOffset--;
			}
		}
		else break;
	}
	doorOffset /= 2; //Find ~centre of door
	return {adjacentDoorsOpened, pos + (westSide ? Position(0, doorOffset, 0) : Position(doorOffset, 0, 0))};
}

/**
 * Closes ufo doors.
 * @return Whether doors are closed.
 */
int TileEngine::closeUfoDoors()
{
	int doorsclosed = 0;

	// prepare a list of tiles on fire/smoke & close any ufo doors
	for (int i = 0; i < _save->getMapSizeXYZ(); ++i)
	{
		if (_save->getTile(i)->getUnit() && _save->getTile(i)->getUnit()->isBigUnit())
		{
			BattleUnit *bu = _save->getTile(i)->getUnit();
			Tile *tile = _save->getTile(i);
			Tile *oneTileNorth = _save->getTile(tile->getPosition() + Position(0, -1, 0));
			Tile *oneTileWest = _save->getTile(tile->getPosition() + Position(-1, 0, 0));
			if ((tile->isUfoDoorOpen(O_NORTHWALL) && oneTileNorth && oneTileNorth->getUnit() && oneTileNorth->getUnit() == bu) ||
				(tile->isUfoDoorOpen(O_WESTWALL) && oneTileWest && oneTileWest->getUnit() && oneTileWest->getUnit() == bu))
			{
				continue;
			}
		}
		doorsclosed += _save->getTile(i)->closeUfoDoor();
	}
	if(doorsclosed > 0)
		resetVisibilityCache();
	return doorsclosed;
}

/**
 * Calculates a line trajectory, using bresenham algorithm in 3D.
 * @param origin Origin tile.
 * @param target Target tile.
 * @param trajectory A vector of positions in which the trajectory is stored.
 * @return 0 or some value greater than .
 */
int TileEngine::calculateLineTile(Position origin, Position target, std::vector<Position> &trajectory, int minLightBlock)
{
	Position lastPoint = origin;
	int steps = 0;

	bool hit = calculateLineHelper(origin, target,
		[&](Position point)
		{
			trajectory.push_back(point);

			const auto difference = point - lastPoint;
			const auto dir = Pathfinding::vectorToDirection(difference);
			const auto& cache = _blockVisibility[_save->getTileIndex(lastPoint)];

			auto result = getBlockDir(cache, dir, difference.z);
			if (result && difference.z == 0 && getBigWallDir(cache, dir))
			{
				if (point == target)
				{
					result = false;
				}
			}
			if (minLightBlock > 0 && result)
			{
				MapData* objectMapData = _save->getTile(lastPoint)  ? _save->getTile(lastPoint)->getMapData(O_OBJECT) : nullptr;
				if (objectMapData && objectMapData->getLightBlock() < minLightBlock)
				{
					result = false;
				}
				else
				{
					MapData* objectMapData = _save->getTile(point) ? _save->getTile(point)->getMapData(O_OBJECT) : nullptr;
					if (objectMapData && objectMapData->getLightBlock() < minLightBlock)
					{
						result = false;
					}
				}
			}
			steps++;
			lastPoint = point;
			return result;
		},
		[&](Position point)
		{
			return false;
		}
	);
	if (hit)
	{
		return 256;
	}
	return 0;
}

/**
 * Calculates a line trajectory, using bresenham algorithm in 3D.
 * @param origin Origin in voxel.
 * @param target Target in voxel.
 * @param storeTrajectory True will store the whole trajectory - otherwise it just stores the last position.
 * @param trajectory A vector of positions in which the trajectory is stored.
 * @param excludeUnit Excludes this unit in the collision detection.
 * @param onlyVisible Skip invisible units? used in FPS view.
 * @param excludeAllBut [Optional] The only unit to be considered for ray hits.
 * @return the objectnumber(0-3) or unit(4) or out of map (5) or -1(hit nothing).
 */
VoxelType TileEngine::calculateLineVoxel(Position origin, Position target, bool storeTrajectory, std::vector<Position> *trajectory, BattleUnit *excludeUnit, BattleUnit *excludeAllBut, bool onlyVisible)
{
	VoxelType result;
	bool excludeAllUnits = false;
	if (_save->isBeforeGame())
	{
		excludeAllUnits = true; // don't start unit spotting before pre-game inventory stuff (large units on the craftInventory tile will cause a crash if they're "spotted")
	}

	bool hit = calculateLineHelper(origin, target,
		[&](Position point)
		{
			if (storeTrajectory && trajectory)
			{
				trajectory->push_back(point);
			}

			result = voxelCheck(point, excludeUnit, excludeAllUnits, onlyVisible, excludeAllBut);
			if (result != V_EMPTY)
			{
				if (trajectory)
				{ // store the position of impact
					trajectory->push_back(point);
				}
				return true;
			}
			return false;
		},
		[&](Position point)
		{
			//check for xy diagonal intermediate voxel step
			result = voxelCheck(point, excludeUnit, excludeAllUnits, onlyVisible, excludeAllBut);
			if (result != V_EMPTY)
			{
				if (trajectory != 0)
				{ // store the position of impact
					trajectory->push_back(point);
				}
				return true;
			}
			return false;
		}
	);
	if (hit)
	{
		return result;
	}
	return V_EMPTY;
}

/**
 * Calculates a parabola trajectory, used for throwing items.
 * @param origin Origin in voxelspace.
 * @param target Target in voxelspace.
 * @param storeTrajectory True will store the whole trajectory - otherwise it just stores the last position.
 * @param trajectory A vector of positions in which the trajectory is stored.
 * @param excludeUnit Makes sure the trajectory does not hit the shooter itself.
 * @param curvature How high the parabola goes: 1.0 is almost straight throw, 3.0 is a very high throw, to throw over a fence for example.
 * @param delta Is the deviation of the angles it should take into account, 0,0,0 is perfection.
 * @return The objectnumber(0-3) or unit(4) or out of map (5) or -1(hit nothing).
 */
int TileEngine::calculateParabolaVoxel(Position origin, Position target, bool storeTrajectory, std::vector<Position> *trajectory, BattleUnit *excludeUnit, double curvature, const Position delta)
{
	if (target == origin) return V_EMPTY;//just in case

	int result = V_EMPTY;
	Position lastPosition = origin;
	Position nextPosition = lastPosition;

	if (storeTrajectory && trajectory)
	{
		//initla value for small hack to glue `calculateLineVoxel` into one continuous arc
		trajectory->push_back(lastPosition);
	}

	calculateParabolaHelper(origin, target, curvature, delta,
		[&](Position p)
		{
			//passes through this point?
			nextPosition = p;

			if (storeTrajectory && trajectory)
			{
				//remove end point of previous trajectory part, because next one will add this point again
				trajectory->pop_back();
			}
			result = calculateLineVoxel(lastPosition, nextPosition, storeTrajectory, storeTrajectory ? trajectory : nullptr, excludeUnit);
			if (result != V_EMPTY)
			{
				if (!storeTrajectory && trajectory)
				{
					result = calculateLineVoxel(lastPosition, nextPosition, false, trajectory, excludeUnit); //pick the INSIDE position of impact
				}
				return true;
			}
			lastPosition = nextPosition;
			return false;
		}
	);

	return result;
}

/**
 * Calculates z "grounded" value for a particular voxel (used for projectile shadow).
 * @param voxel The voxel to trace down.
 * @return z coord of "ground".
 */
int TileEngine::castedShade(Position voxel)
{
	int zstart = voxel.z;
	Position tmpCoord = voxel.toTile();
	Tile *t = _save->getTile(tmpCoord);
	while (t && t->isVoid() && !t->getUnit())
	{
		zstart = tmpCoord.z* 24;
		--tmpCoord.z;
		t = _save->getTile(tmpCoord);
	}

	Position tmpVoxel = voxel;
	int z;

	voxelCheckFlush();
	for (z = zstart; z>0; z--)
	{
		tmpVoxel.z = z;
		if (voxelCheck(tmpVoxel, 0) != V_EMPTY) break;
	}
	return z;
}

/**
 * Traces voxel visibility.
 * @param voxel Voxel coordinates.
 * @return True if visible.
 */

bool TileEngine::isVoxelVisible(Position voxel)
{
	int zstart = voxel.z+3; // slight Z adjust
	if ((zstart/24)!=(voxel.z/24))
		return true; // visible!
	Position tmpVoxel = voxel;
	int zend = (zstart/24)*24 +24;

	voxelCheckFlush();
	for (int z = zstart; z<zend; z++)
	{
		tmpVoxel.z=z;
		// only OBJECT can cause additional occlusion (because of any shape)
		if (voxelCheck(tmpVoxel, 0) == V_OBJECT) return false;
		++tmpVoxel.x;
		if (voxelCheck(tmpVoxel, 0) == V_OBJECT) return false;
		++tmpVoxel.y;
		if (voxelCheck(tmpVoxel, 0) == V_OBJECT) return false;
	}
	return true;
}

/**
 * Checks if we hit a voxel.
 * @param voxel The voxel to check.
 * @param excludeUnit Don't do checks on this unit.
 * @param excludeAllUnits Don't do checks on any unit.
 * @param onlyVisible Whether to consider only visible units.
 * @param excludeAllBut If set, the only unit to be considered for ray hits.
 * @return The objectnumber(0-3) or unit(4) or out of map (5) or -1 (hit nothing).
 */
VoxelType TileEngine::voxelCheck(Position voxel, BattleUnit *excludeUnit, bool excludeAllUnits, bool onlyVisible, BattleUnit *excludeAllBut)
{
	if (voxel.x < 0 || voxel.y < 0 || voxel.z < 0) //preliminary out of map
	{
		return V_OUTOFBOUNDS;
	}
	Position pos = voxel.toTile();
	Tile *tile, *tileBelow;
	if (_cacheTilePos == pos)
	{
		tile = _cacheTile;
		tileBelow = _cacheTileBelow;
	}
	else
	{
		tile = _save->getTile(pos);
		if (!tile) // check if we are not out of the map
		{
			return V_OUTOFBOUNDS; //not even cache
		}
		tileBelow = _save->getBelowTile(tile);
		_cacheTilePos = pos;
		_cacheTile = tile;
		_cacheTileBelow = tileBelow;
 	}

	if (tile->isVoid() && tile->getUnit() == 0 && (!tileBelow || tileBelow->getUnit() == 0))
	{
		return V_EMPTY;
	}

	if (tile->hasGravLiftFloor() && (voxel.z % 24 == 0 || voxel.z % 24 == 1))
	{
		if (!(tileBelow && tileBelow->hasGravLiftFloor()))
		{
			return V_FLOOR;
		}
	}

	// first we check terrain voxel data, not to allow 2x2 units stick through walls
	for (int i = V_FLOOR; i <= V_OBJECT; ++i)
	{
		TilePart tp = (TilePart)i;
		MapData *mp = tile->getMapData(tp);
		if (((tp == O_WESTWALL) || (tp == O_NORTHWALL)) && tile->isUfoDoorOpen(tp))
			continue;
		if (mp != 0)
		{
			int x = 15 - voxel.x%16;
			int y = voxel.y%16;
			int idx = (mp->getLoftID((voxel.z%24)/2)*16) + y;
			if (_voxelData->at(idx) & (1 << x))
			{
				return (VoxelType)i;
			}
		}
	}

	if (!excludeAllUnits)
	{
		BattleUnit *unit = tile->getOverlappingUnit(_save);

		if (unit != 0 && !unit->isOut() && unit != excludeUnit && (!excludeAllBut || unit == excludeAllBut) && (!onlyVisible || unit->getVisible() ) )
		{
			Position tilepos;
			Position unitpos = unit->getPosition();
			int terrainHeight = 0;
			for (int x = 0; x < unit->getArmor()->getSize(); ++x)
			{
				for (int y = 0; y < unit->getArmor()->getSize(); ++y)
				{
					Tile *tempTile = _save->getTile(unitpos + Position(x,y,0));
					if (tempTile->getTerrainLevel() < terrainHeight)
					{
						terrainHeight = tempTile->getTerrainLevel();
					}
				}
			}
			int tz = unitpos.z*24 + unit->getFloatHeight() - terrainHeight; //bottom most voxel, terrain heights are negative, so we subtract.
			if ((voxel.z > tz) && (voxel.z <= tz + unit->getHeight()) )
			{
				int x = 15 - voxel.x%16;
				int y = voxel.y%16;
				int part = 0;
				if (unit->isBigUnit())
				{
					tilepos = tile->getPosition();
					constexpr static int parts[] = {1,0,3,2}; // Change order 0,1,2,3 -> 1,0,3,2  (read commit description)
					part = parts[tilepos.x - unitpos.x + (tilepos.y - unitpos.y)*2];
				}
				int idx = (unit->getLoftemps(part) * 16) + y;
				if (_voxelData->at(idx) & (1 << x))
				{
					return V_UNIT;
				}
			}
		}
	}
	return V_EMPTY;
}

void TileEngine::voxelCheckFlush()
{
	_cacheTilePos = invalid;
	_cacheTile = 0;
	_cacheTileBelow = 0;
}

/**
 * Toggles personal lighting on / off.
 */
void TileEngine::togglePersonalLighting()
{
	_personalLighting = !_personalLighting;

	if (Options::oxceTogglePersonalLightType == 2)
	{
		// persisted per campaign
		SavedGame* geosave = _save->getGeoscapeSave();
		if (geosave)
		{
			geosave->setTogglePersonalLight(_personalLighting);
		}
	}
	else if (Options::oxceTogglePersonalLightType == 1)
	{
		// persisted per battle
		_save->setTogglePersonalLight(_personalLighting);
	}

	_save->setTogglePersonalLightTemp(_personalLighting);
	calculateLighting(LL_UNITS);
	recalculateFOV();
}

/**
 * Calculate strength of psi attack based on range and victim.
 * @param type Type of attack.
 * @param attacker Unit attacking.
 * @param victim Attacked unit.
 * @param weapon Attack item.
 * @return Value greater than zero mean successful attack.
 */
int TileEngine::psiAttackCalculate(BattleActionAttack::ReadOnly attack, const BattleUnit *victim)
{
	if (!victim)
		return 0;

	auto type = attack.type;
	auto attacker = attack.attacker;
	auto weapon = attack.weapon_item;

	int attackStrength = BattleUnit::getPsiAccuracy(attack);
	int defenseStrength = 30 + victim->getArmor()->getPsiDefence(victim);

	auto dis = Position::distance(attacker->getPosition().toVoxel(), victim->getPosition().toVoxel());

	auto rng = RNG::globalRandomState().subSequence();
	int psiAttackResult = 0;

	psiAttackResult = ModScript::scriptFunc1<ModScript::TryPsiAttackItem>(
		weapon->getRules(),
		psiAttackResult,
		weapon, attacker, victim, attack.skill_rules, attackStrength, defenseStrength, type, &rng, (int)dis, (int)weapon->getRules()->getPsiAccuracyRangeReduction(dis),
		_save
	);

	psiAttackResult =  ModScript::scriptFunc1<ModScript::TryPsiAttackUnit>(
		victim->getArmor(),
		psiAttackResult,
		weapon, attacker, victim, attack.skill_rules, attackStrength, defenseStrength, type,
		_save
	);

	return psiAttackResult;
}

/**
 * Attempts a panic or mind control action.
 * @param action Pointer to an action.
 * @return Whether it failed or succeeded.
 */
bool TileEngine::psiAttack(BattleActionAttack attack, BattleUnit *victim)
{
	if (_save->isPreview())
	{
		return false;
	}

	if (!victim)
		return false;

	// Mana experience - this is a temporary/experimental approach, can be improved later after modder feedback
	attack.attacker->addManaExp(attack.weapon_item->getRules()->getManaExperience());

	bool isDefaultExpTrainingMode = (attack.weapon_item->getRules()->getExperienceTrainingMode() == ETM_DEFAULT);
	bool isNaturallyPsiCapable = true;
	if (attack.attacker->getGeoscapeSoldier() && attack.attacker->getGeoscapeSoldier()->getCurrentStats()->psiSkill <= 0)
	{
		isNaturallyPsiCapable = false;
	}
	bool isPsiRequired = attack.weapon_item->getRules()->isPsiRequired();

	if (isDefaultExpTrainingMode)
	{
		if (isNaturallyPsiCapable)
		{
			attack.attacker->addPsiSkillExp();
		}
	}
	if (Options::allowPsiStrengthImprovement && isPsiRequired)
	{
		victim->addPsiStrengthExp(); // experience for the victim, not the attacker
	}

	if (psiAttackCalculate(attack, victim) > 0)
	{
		if (isDefaultExpTrainingMode)
		{
			if (isNaturallyPsiCapable)
			{
				attack.attacker->addPsiSkillExp();
				attack.attacker->addPsiSkillExp();
			}
		}
		else if (attack.type == BA_PANIC || attack.type == BA_MINDCONTROL)
		{
			// Note: BA_USE is handled elsewhere
			awardExperience(attack, victim, false);
		}

		BattleUnitKills killStat;
		killStat.setUnitStats(victim);
		killStat.setTurn(_save->getTurn(), _save->getSide());
		killStat.weapon = attack.weapon_item->getRules()->getName();
		killStat.weaponAmmo = attack.weapon_item->getRules()->getName(); //Psi weapons got no ammo, just filling up the field
		killStat.faction = victim->getOriginalFaction();
		killStat.mission = _save->getGeoscapeSave()->getMissionStatistics()->size();
		killStat.id = victim->getId();

		if (attack.type == BA_PANIC)
		{
			int moraleLoss = victim->reduceByBravery(100);
			if (moraleLoss > 0)
				victim->moraleChange(-moraleLoss);
			victim->setMindControllerId(attack.attacker->getId());

			// Award Panic battle unit kill
			if (!attack.attacker->getStatistics()->duplicateEntry(STATUS_PANICKING, victim->getId()))
			{
				killStat.status = STATUS_PANICKING;
				if (!victim->isCosmetic())
				{
					attack.attacker->getStatistics()->kills.push_back(new BattleUnitKills(killStat));
				}
			}
		}
		else if (attack.type == BA_MINDCONTROL)
		{
			// Award MC battle unit kill
			if (!attack.attacker->getStatistics()->duplicateEntry(STATUS_TURNING, victim->getId()))
			{
				killStat.status = STATUS_TURNING;
				if (!victim->isCosmetic())
				{
					attack.attacker->getStatistics()->kills.push_back(new BattleUnitKills(killStat));
				}
			}
			victim->setMindControllerId(attack.attacker->getId());
			if (attack.weapon_item->getRules()->convertToCivilian() && victim->getOriginalFaction() == FACTION_HOSTILE)
			{
				victim->convertToFaction(FACTION_NEUTRAL);
				if (victim->getAIModule())
				{
					// rewire them to attack hostiles
					victim->getAIModule()->setTargetFaction(FACTION_HOSTILE);
				}
			}
			else
			{
				victim->convertToFaction(attack.attacker->getFaction());
				calculateLighting(LL_UNITS, victim->getPosition());
				calculateFOV(victim->getPosition()); //happens fairly rarely, so do a full recalc for units in range to handle the potential unit visible cache issues.
			}
			victim->recoverTimeUnits();
			victim->allowReselect();
			victim->setWantToEndTurn(false);
			victim->abortTurn(); // resets unit status to STANDING
			// if all units from either faction are mind controlled - auto-end the mission.
			if (_save->getSide() == FACTION_PLAYER && Options::allowPsionicCapture)
			{
				_save->getBattleGame()->autoEndBattle();
			}
		}
		return true;
	}
	else
	{
		if (Options::allowPsiStrengthImprovement && isPsiRequired)
		{
			victim->addPsiStrengthExp(); // experience for the victim, not the attacker
		}
		return false;
	}
}

/**
 * Calculate success rate of melee attack action.
 */
int TileEngine::meleeAttackCalculate(BattleActionAttack::ReadOnly attack, const BattleUnit *victim)
{
	if (!victim)
		return 0;

	int attackStrength = BattleUnit::getFiringAccuracy(attack, _save->getBattleGame()->getMod());
	int defenseStrength = victim->getArmor()->getMeleeDodge(victim);
	int arc = getArcDirection(getDirectionTo(victim->getPositionVexels(), attack.attacker->getPositionVexels()), victim->getDirection());
	int defenseStrengthPenalty = Clamp((int)(defenseStrength * (arc * victim->getArmor()->getMeleeDodgeBackPenalty() / 4.0f)), 0, std::max(0, defenseStrength));

	auto type = attack.type;
	auto attacker = attack.attacker;
	auto weapon = attack.weapon_item;

	auto rng = RNG::globalRandomState().subSequence();

	int meleeAttackResult = 0;

	meleeAttackResult = ModScript::scriptFunc1<ModScript::TryMeleeAttackItem>(
		weapon->getRules(),
		meleeAttackResult,
		weapon, attacker, victim, attack.skill_rules, attackStrength, defenseStrength, type, &rng, arc, defenseStrengthPenalty,
		_save
	);

	meleeAttackResult =  ModScript::scriptFunc1<ModScript::TryMeleeAttackUnit>(
		victim->getArmor(),
		meleeAttackResult,
		weapon, attacker, victim, attack.skill_rules, attackStrength, defenseStrength, type,
		_save
	);


	return meleeAttackResult;
}

/**
 *  Attempts a melee attack action.
 * @param action Pointer to an action.
 * @return Whether it failed or succeeded.
 */
bool TileEngine::meleeAttack(BattleActionAttack attack, BattleUnit *victim, int terrainMeleeTilePart)
{
	if (terrainMeleeTilePart > 0)
	{
		// terrain melee doesn't miss
		return true;
	}
	if (attack.type != BA_CQB)
	{
		// hit log - new melee attack
		_save->appendToHitLog(HITLOG_NEW_SHOT, attack.attacker->getFaction());

		// to allow melee reactions when attacked from any side, not just from the front
		if (victim && Mod::EXTENDED_MELEE_REACTIONS == 2)
		{
			victim->setMeleeAttackedBy(attack.attacker->getId());
		}
	}

	return meleeAttackCalculate(attack, victim) > 0;
}

/**
 * Remove the medikit from the game if consumable and empty.
 * @param action
 */
void TileEngine::medikitRemoveIfEmpty(BattleAction *action)
{
	// remove item if completely used up
	if (action->weapon->getRules()->isConsumable())
	{
		if (action->weapon->getPainKillerQuantity() == 0 && action->weapon->getStimulantQuantity() == 0 && action->weapon->getHealQuantity() == 0)
		{
			_save->removeItem(action->weapon);
		}
	}
}

bool TileEngine::medikitUse(BattleAction *action, BattleUnit *target, BattleMediKitAction originalMedikitAction, UnitBodyPart bodyPart)
{
	if (_save->isPreview())
	{
		return false;
	}

	BattleActionAttack attack;
	attack.type = action->type;
	attack.attacker = action->actor;
	attack.weapon_item = action->weapon;
	attack.damage_item = action->weapon;

	bool canContinueHealing = true;

	const RuleItem *rule = action->weapon->getRules();

	BattleMediKitType type = rule->getMediKitType();

	constexpr int medikitActionKey = 0;
	constexpr int bodyPartKey = 1;
	constexpr int woundRecoveryKey = 2;
	constexpr int healthRecoveryKey = 3;
	constexpr int energyRecoveryKey = 4;
	constexpr int stunRecoveryKey = 5;
	constexpr int manaRecoveryKey = 6;
	constexpr int moraleRecoveryKey = 7;
	constexpr int painkillerRecoveryKey = 8;

	action->weapon->spendHealingItemUse(originalMedikitAction);

	ModScript::HealUnit::Output args { };

	std::get<medikitActionKey>(args.data) += originalMedikitAction;
	std::get<bodyPartKey>(args.data) += (int)bodyPart;
	std::get<woundRecoveryKey>(args.data) += rule->getWoundRecovery();
	std::get<healthRecoveryKey>(args.data) += rule->getHealthRecovery();
	std::get<energyRecoveryKey>(args.data) += rule->getEnergyRecovery();
	std::get<stunRecoveryKey>(args.data) += rule->getStunRecovery();
	std::get<manaRecoveryKey>(args.data) += rule->getManaRecovery();
	std::get<moraleRecoveryKey>(args.data) += rule->getMoraleRecovery();
	std::get<painkillerRecoveryKey>(args.data) += (int)(rule->getPainKillerRecovery() * 100.0f);

	ModScript::HealUnit::Worker work { action->actor, action->weapon, _save, target, action->type };

	work.execute(target->getArmor()->getScript<ModScript::HealUnit>(), args);

	int medikitAction = std::get<medikitActionKey>(args.data);
	bodyPart = (UnitBodyPart)std::get<bodyPartKey>(args.data);
	int healthRecovery = std::get<healthRecoveryKey>(args.data);
	int woundRecovery = std::get<woundRecoveryKey>(args.data);
	int energyRecovery = std::get<energyRecoveryKey>(args.data);
	int stunRecovery = std::get<stunRecoveryKey>(args.data);
	int manaRecovery = std::get<manaRecoveryKey>(args.data);
	int moraleRecovery = std::get<moraleRecoveryKey>(args.data);
	float painkillerRecovery = std::get<painkillerRecoveryKey>(args.data) / 100.0f;

	// 0 = normal, 1 = heal, 2 = stim, 4 = pain
	if (medikitAction & BMA_PAINKILLER)
	{
		target->painKillers(moraleRecovery, painkillerRecovery);
	}

	if (medikitAction & BMA_STIMULANT)
	{
		target->stimulant(energyRecovery, stunRecovery, manaRecovery);
	}

	if (medikitAction & BMA_HEAL)
	{
		if (target->getFatalWound(bodyPart))
		{
			// award experience only if healed body part has a fatal wound (to prevent abuse)
			awardExperience(attack, target, false);
		}

		target->heal(bodyPart, woundRecovery, healthRecovery);
	}

	_save->getBattleGame()->playSound(action->weapon->getRules()->getHitSound());

	if (type == BMT_NORMAL) // normal medikit usage, track statistics
	{
		if (medikitAction & BMA_PAINKILLER)
		{
			action->actor->getStatistics()->appliedPainKill++;
		}
		if (medikitAction & BMA_STIMULANT)
		{
			action->actor->getStatistics()->appliedStimulant++;
		}
		if (medikitAction & BMA_HEAL)
		{
			action->actor->getStatistics()->woundsHealed++;
		}

		if (target->getStatus() == STATUS_UNCONSCIOUS && !target->isOutThresholdExceed())
		{
			if(target->getOriginalFaction() == FACTION_PLAYER)
			{
				action->actor->getStatistics()->revivedSoldier++;
			}
			else if(target->getOriginalFaction() == FACTION_HOSTILE)
			{
				action->actor->getStatistics()->revivedHostile++;
			}
			else
			{
				action->actor->getStatistics()->revivedNeutral++;
			}
			// if the unit has revived and has no more wounds, we cannot continue healing
			if (target->getFatalWounds() == 0)
			{
				canContinueHealing = false;
			}
		}
	}

	// check for casualties, revive unconscious unit (+ change status), re-calc fov & lighting
	updateGameStateAfterScript(attack, action->actor->getPosition());

	return canContinueHealing;
}

/**
 * Executes the skillUseUnit script hook and determines further steps.
 * @return True if the current action should be continued, false if the action should be cancelled (usually because the script took care of any effects itself).
 */
bool TileEngine::skillUse(BattleAction *action, const RuleSkill *skill)
{
	if (_save->isPreview())
	{
		return false;
	}

	bool continueAction = true;
	bool spendTu = false;
	std::string message;
	BattleUnit *actor = action->actor;
	bool hasTu = action->haveTU(&message);

	ModScript::SkillUseUnit::Output args { continueAction, spendTu };
	ModScript::SkillUseUnit::Worker work { actor, action->weapon, _save, skill, action->type, hasTu };

	work.execute(skill->getScript<ModScript::SkillUseUnit>(), args);

	continueAction = args.getFirst();
	spendTu = args.getSecond();

	if (spendTu)
	{
		action->actor->spendCost(static_cast<const RuleItemUseCost&>(*action));
	}

	if (!hasTu && !message.empty())
	{
		action->result = message;
	}

	return continueAction;
}

/**
 * Tries to conceal a unit. Only works if no unit of another faction is watching.
 */
bool TileEngine::tryConcealUnit(BattleUnit* unit)
{
	for (const auto* bu : *_save->getUnits())
	{
		if (bu->getFaction() != unit->getFaction() && bu->hasVisibleUnit(unit))
		{
			return false;
		}
	}

	unit->setTurnsSinceSpotted(255);
	for (UnitFaction faction : {UnitFaction::FACTION_PLAYER, UnitFaction::FACTION_HOSTILE, UnitFaction::FACTION_NEUTRAL})
	{
		if (faction != unit->getFaction())
			unit->setTurnsSinceSeen(255, faction);
	}
	unit->setTurnsLeftSpottedForSnipers(0);

	return true;
}

/**
 * Applies gravity to a tile. Causes items and units to drop.
 * @param t Tile.
 * @return Tile where the items end up in eventually.
 */
Tile *TileEngine::applyGravity(Tile *t)
{
	if (!t || (t->getInventory()->empty() && !t->getUnit())) return t; // skip this if there are no items

	BattleUnit *occupant = t->getUnit();

	if (occupant)
	{
		occupant->updateTileFloorState(_save);
		if (occupant->haveNoFloorBelow())
		{
			if (!occupant->isOutThresholdExceed())
			{
				if (occupant->getMovementType() == MT_FLY)
				{
					// move to the position you're already in. this will unset the kneeling flag, set the floating flag, etc.
					occupant->startWalking(occupant->getDirection(), occupant->getPosition(), _save);
					// and set our status to standing (rather than walking or flying) to avoid weirdness.
					occupant->abortTurn();
				}
				else
				{
					occupant->setPosition(occupant->getPosition()); // this is necessary to set the unit up for falling correctly, updating their "lastPos"
					_save->addFallingUnit(occupant);
				}
			}
		}
	}

	Tile *rt = t;
	while (rt->getPosition().z > 0 && rt->hasNoFloor(_save))
	{
		rt = _save->getBelowTile(rt);
	}

	for (auto* bi : *t->getInventory())
	{
		if (bi->getUnit() && t->getPosition() == bi->getUnit()->getPosition())
		{
			bi->getUnit()->setPosition(rt->getPosition());
		}
		if (t != rt)
		{
			rt->addItem(bi, bi->getSlot());
		}
	}

	if (t != rt)
	{
		// clear tile
		t->getInventory()->clear();
	}

	return rt;
}

/**
 * Drop item on ground.
 */
void TileEngine::itemDrop(Tile *t, BattleItem *item, bool updateLight)
{
	// don't spawn anything outside of bounds
	if (t == 0)
		return;

	Position p = t->getPosition();

	// don't ever drop fixed items
	if (item->getRules()->isFixed())
		return;

	BattleUnit* dropper = t->getUnit();
	if (dropper)
	{
		if (item->getRules()->getBattleType() == BT_GRENADE)
		{
			if (item->isFuseEnabled() && item->getRules()->getDamageType()->ResistType != DT_NONE)
			{
				int radius = item->getRules()->getExplosionRadius({BA_THROW, dropper, item, item});
				_save->getTileEngine()->setDangerZone(p, radius, dropper);
			}
		}
	}

	if (_save->getSide() != FACTION_PLAYER)
	{
		item->setTurnFlag(true);
	}

	itemMoveInventory(t, nullptr, item, _inventorySlotGround, 0, 0);

	applyGravity(t);

	if (updateLight)
	{
		calculateLighting(LL_ITEMS, p);
		calculateFOV(p, item->getVisibilityUpdateRange(), false);
	}
}

/**
 * Drop all unit items on ground.
 */
void TileEngine::itemDropInventory(Tile *t, BattleUnit *unit, bool unprimeItems, bool deleteFixedItems)
{
	Collections::removeIf(*unit->getInventory(),
		[&](BattleItem* i)
		{
			if (!i->getRules()->isFixed())
			{
				i->setOwner(nullptr);
				if (unprimeItems && i->getRules()->getFuseTimerType() != BFT_NONE)
				{
					if (i->getRules()->getCostUnprime().Time > 0 /* && !i->getRules()->getUnprimeActionName().empty() */ )
					{
						i->setFuseTimer(-1); // unprime explosives before dropping them
					}
				}
				t->addItem(i, _inventorySlotGround);
				if (i->getUnit() && i->getUnit()->getStatus() == STATUS_UNCONSCIOUS)
				{
					i->getUnit()->setPosition(t->getPosition());
				}
				return true;
			}
			else
			{
				if (deleteFixedItems)
				{
					// first unload all ammo
					for (int slot = 0; slot < RuleItem::AmmoSlotMax; ++slot)
					{
						if (i->needsAmmoForSlot(slot) && i->getAmmoForSlot(slot))
						{
							// unload the existing ammo (if any) from the weapon
							BattleItem* oldAmmo = i->setAmmoForSlot(slot, nullptr);
							if (oldAmmo)
							{
								itemDrop(t, oldAmmo, false);
							}
						}
					}

					// delete fixed items completely (e.g. when changing armor)
					i->setOwner(nullptr);
					_save->removeItem(i);
					return true;
				}
				else
				{
					// do nothing, fixed items cannot be moved (individually by the player)!
					return false;
				}
			}
		}
	);

	// handle special built-in items
	if (deleteFixedItems)
	{
		unit->removeSpecialWeapons(_save);
	}
}

/**
 * Move item to other place in inventory or ground.
 */
void TileEngine::itemMoveInventory(Tile *t, BattleUnit *unit, BattleItem *item, const RuleInventory *slot, int x, int y)
{
	// Handle dropping from/to ground.
	if (slot != item->getSlot())
	{
		if (slot == _inventorySlotGround)
		{
			BattleUnit* dropper = t->getUnit();
			Position p = t->getPosition();
			if (dropper)
			{
				if (item->getRules()->getBattleType() == BT_GRENADE)
				{
					if (item->isFuseEnabled() && item->getRules()->getDamageType()->ResistType != DT_NONE)
					{
						int radius = item->getRules()->getExplosionRadius({BA_THROW, dropper, item, item});
						_save->getTileEngine()->setDangerZone(p, radius, dropper);
					}
				}
				dropper->updateEnemyKnowledge(_save->getTileIndex(p), true, false);
			}
			item->moveToOwner(nullptr);
			t->addItem(item, slot);
			if (item->getUnit() && item->getUnit()->getStatus() == STATUS_UNCONSCIOUS)
			{
				item->getUnit()->setPosition(t->getPosition());
			}
		}
		else if (item->getSlot() == 0 || item->getSlot() == _inventorySlotGround)
		{
			item->moveToOwner(unit);
			item->setTurnFlag(false);
			if (item->getUnit() && item->getUnit()->getStatus() == STATUS_UNCONSCIOUS)
			{
				item->getUnit()->setPosition(invalid);
			}
		}
	}
	item->setSlot(slot);
	item->setSlotX(x);
	item->setSlotY(y);
}

/**
 * Add moving unit.
 */
void TileEngine::addMovingUnit(BattleUnit* unit)
{
	if (_movingUnit != nullptr)
	{
		_movingUnitPrev.push_back(_movingUnit);
	}
	_movingUnit = unit;
}

/**
 * Add moving unit.
 */
void TileEngine::removeMovingUnit(BattleUnit* unit)
{
	if (_movingUnit != unit)
	{
		throw Exception("Wrong unit is removed from TileEngine movingUnit");
	}
	if (_movingUnitPrev.empty())
	{
		_movingUnit = nullptr;
	}
	else
	{
		_movingUnit = _movingUnitPrev.back();
		_movingUnitPrev.pop_back();
	}
}

/**
 * Get current moving unit.
 */
BattleUnit* TileEngine::getMovingUnit()
{
	return _movingUnit;
}

/**
 * Validates the melee range between two units.
 * @param attacker The attacking unit.
 * @param target The unit we want to attack.
 * @param dir Direction to check.
 * @return True when the range is valid.
 */
bool TileEngine::validMeleeRange(BattleUnit *attacker, BattleUnit *target, int dir)
{
	return validMeleeRange(attacker->getPosition(), dir, attacker, target, 0);
}

/**
 * Validates the melee range between a tile and a unit.
 * @param pos Position to check from.
 * @param direction Direction to check.
 * @param attacker The attacking unit.
 * @param target The unit we want to attack, 0 for any unit.
 * @param dest Destination position.
 * @return True when the range is valid.
 */
bool TileEngine::validMeleeRange(Position pos, int direction, BattleUnit *attacker, BattleUnit *target, Position *dest, bool preferEnemy)
{
	if (direction < 0 || direction > 7)
	{
		return false;
	}
	std::vector<BattleUnit*> potentialTargets;
	BattleUnit *chosenTarget = 0;
	Position p;
	int size = attacker->getArmor()->getSize() - 1;
	int meleeOriginVoxelVerticalOffset = attacker->getArmor()->getMeleeOriginVoxelVerticalOffset(); // add some sanity checks or trust the modders?
	Pathfinding::directionToVector(direction, &p);
	for (int x = 0; x <= size; ++x)
	{
		for (int y = 0; y <= size; ++y)
		{
			Tile *origin (_save->getTile(Position(pos + Position(x, y, 0))));
			Tile *targetTile (_save->getTile(Position(pos + Position(x, y, 0) + p)));

			if (targetTile && origin)
			{
				Tile *aboveTargetTile = _save->getAboveTile(targetTile);
				Tile *belowTargetTile = _save->getBelowTile(targetTile);

				if (origin->getTerrainLevel() <= -16 && aboveTargetTile && !aboveTargetTile->hasNoFloor(_save))
				{
					targetTile = aboveTargetTile;
				}
				else if (belowTargetTile && targetTile->hasNoFloor(_save) && !targetTile->getUnit() && belowTargetTile->getTerrainLevel() <= -16)
				{
					targetTile = belowTargetTile;
				}
				if (targetTile->getUnit())
				{
					if (target == 0 || targetTile->getUnit() == target)
					{
						Position originVoxel = Position(origin->getPosition().toVoxel())
							+ Position(8,8,attacker->getHeight() + attacker->getFloatHeight() - 4 -origin->getTerrainLevel() + meleeOriginVoxelVerticalOffset);
						Position targetVoxel;
						if (canTargetUnit(&originVoxel, targetTile, &targetVoxel, attacker, false))
						{
							if (dest)
							{
								*dest = targetTile->getPosition();
							}
							if (target != 0)
							{
								return true;
							}
							else
							{
								potentialTargets.push_back(targetTile->getUnit());
							}
						}
					}
				}
			}
		}
	}

	for (auto* bu : potentialTargets)
	{
		// if there's actually something THERE, we'll chalk this up as a success.
		if (!chosenTarget)
		{
			chosenTarget = bu;
		}
		// but if there's a target of a different faction, we'll prioritize them.
		else if ((preferEnemy && bu->getFaction() != attacker->getFaction())
		// or, if we're using a medikit, prioritize whichever friend is wounded the most.
		|| (!preferEnemy && bu->getFaction() == attacker->getFaction() &&
		bu->getFatalWounds() > chosenTarget->getFatalWounds()))
		{
			chosenTarget = bu;
		}
	}

	if (dest && chosenTarget)
	{
		*dest = chosenTarget->getPosition();
	}

	return chosenTarget != 0;
}

/**
 * Validates the terrain melee range.
 */
bool TileEngine::validTerrainMeleeRange(BattleAction* action)
{
	if (Mod::EXTENDED_TERRAIN_MELEE <= 0)
	{
		// turned off
		return false;
	}

	action->terrainMeleeTilePart = 0;

	if (action->weapon)
	{
		auto wRule = action->weapon->getRules();
		if (wRule->getBattleType() == BT_MELEE)
		{
			// check primary damage type
			if (wRule->getDamageType()->ToTile == 0.0) return false;
		}
		else
		{
			// check secondary damage type
			if (wRule->getMeleeType()->ToTile == 0.0) return false;
		}
	}

	Position pos = action->actor->getPosition();
	int direction = action->actor->getDirection();
	BattleUnit* attacker = action->actor;

	if (direction < 0 || direction > 7)
	{
		return false;
	}
	if (direction % 2 != 0)
	{
		// diagonal directions are not supported
		return false;
	}
	Position p;
	Pathfinding::directionToVector(direction, &p);

	Tile* originTile = _save->getTile(pos);
	Tile* originTile2 = originTile;
	if (originTile && originTile->getTerrainLevel() <= -16)
	{
		// if we are on the upper part of stairs, target one tile above
		pos += Position(0, 0, 1);
		originTile = _save->getTile(pos);
	}
	Tile* neighbouringTile = _save->getTile(pos + p);
	Tile* neighbouringTile2 = nullptr;
	int size = attacker->getArmor()->getSize();
	if (size > 1)
	{
		if (direction == 0)
		{
			// North
			originTile2 = _save->getTile(pos + Position(1, 0, 0));
			neighbouringTile2 = _save->getTile(pos + p + Position(1, 0, 0));
		}
		else if (direction == 2)
		{
			// East
			neighbouringTile =  _save->getTile(pos + p + Position(1, 0, 0));
			neighbouringTile2 = _save->getTile(pos + p + Position(1, 1, 0));
		}
		else if (direction == 4)
		{
			// South
			neighbouringTile =  _save->getTile(pos + p + Position(0, 1, 0));
			neighbouringTile2 = _save->getTile(pos + p + Position(1, 1, 0));
		}
		else if (direction == 6)
		{
			// West
			originTile2 = _save->getTile(pos + Position(0, 1, 0));
			neighbouringTile2 = _save->getTile(pos + p + Position(0, 1, 0));
		}
		if (!neighbouringTile2 || !originTile2)
		{
			return false;
		}
	}
	if (originTile && neighbouringTile)
	{
		auto setTarget = [](Tile* tt, TilePart tp, BattleAction* aa, int dir = -1) -> bool
		{
			MapData* obj = tt->getMapData(tp);
			if (obj)
			{
				if (dir > -1 && tp == O_OBJECT)
				{
					auto bigWall = obj->getBigWall();
					if (dir == 0 /*north*/ && bigWall != Pathfinding::BIGWALLNORTH && bigWall != Pathfinding::BIGWALLWESTANDNORTH) return false;
					if (dir == 2 /*east */ && bigWall != Pathfinding::BIGWALLEAST  && bigWall != Pathfinding::BIGWALLEASTANDSOUTH) return false;
					if (dir == 4 /*south*/ && bigWall != Pathfinding::BIGWALLSOUTH && bigWall != Pathfinding::BIGWALLEASTANDSOUTH) return false;
					if (dir == 6 /*west */ && bigWall != Pathfinding::BIGWALLWEST  && bigWall != Pathfinding::BIGWALLWESTANDNORTH) return false;
				}
				if (tp != O_OBJECT && !obj->isDoor() && !obj->isUFODoor() && tt->getTUCost(tp, MT_WALK) != Pathfinding::INVALID_MOVE_COST)
				{
					// it is possible to walk through this (rubble) wall... no need to attack it
					return false;
				}
				bool isHighEnough = false;
				for (int i = Mod::EXTENDED_TERRAIN_MELEE; i < 12; ++i)
				{
					if (obj->getLoftID(i) > 0)
					{
						isHighEnough = true;
						break;
					}
				}
				if (isHighEnough)
				{
					aa->target = tt->getPosition();
					aa->terrainMeleeTilePart = tp;
					return true;
				}
			}
			return false;
		};

		if (setTarget(originTile, O_OBJECT, action, direction))
		{
			// All directions: target the object (marked as big wall) on the current tile
			return true;
		}
		if (size > 1)
		{
			if (setTarget(originTile2, O_OBJECT, action, direction))
			{
				// All directions
				return true;
			}
		}

		if (direction == 0 && setTarget(originTile, O_NORTHWALL, action))
		{
			// North: target the north wall of the same tile
			return true;
		}
		else if (direction == 2 && setTarget(neighbouringTile, O_WESTWALL, action))
		{
			// East: target the west wall of the neighbouring tile
			return true;
		}
		else if (direction == 4 && setTarget(neighbouringTile, O_NORTHWALL, action))
		{
			// South: target the north wall of the neighbouring tile
			return true;
		}
		else if (direction == 6 && setTarget(originTile, O_WESTWALL, action))
		{
			// West: target the west wall of the same tile
			return true;
		}
		if (size > 1)
		{
			if (direction == 0 && setTarget(originTile2, O_NORTHWALL, action))
			{
				// North
				return true;
			}
			else if (direction == 2 && setTarget(neighbouringTile2, O_WESTWALL, action))
			{
				// East
				return true;
			}
			else if (direction == 4 && setTarget(neighbouringTile2, O_NORTHWALL, action))
			{
				// South
				return true;
			}
			else if (direction == 6 && setTarget(originTile2, O_WESTWALL, action))
			{
				// West
				return true;
			}
		}

		if (setTarget(neighbouringTile, O_OBJECT, action))
		{
			// All directions: target the object on the neighbouring tile
			return true;
		}
		if (size > 1)
		{
			if (setTarget(neighbouringTile2, O_OBJECT, action))
			{
				// All directions
				return true;
			}
		}
	}

	return false;
}

/**
 * Gets the AI to look through a window.
 * @param position Current position.
 * @return Direction or -1 when no window found.
 */
int TileEngine::faceWindow(Position position)
{
	static const Position oneTileEast = Position(1, 0, 0);
	static const Position oneTileSouth = Position(0, 1, 0);

	Tile *tile = _save->getTile(position);
	if (tile && tile->getMapData(O_NORTHWALL) && tile->getMapData(O_NORTHWALL)->getBlock(DT_NONE)==0) return 0;
	tile = _save->getTile(position + oneTileEast);
	if (tile && tile->getMapData(O_WESTWALL) && tile->getMapData(O_WESTWALL)->getBlock(DT_NONE)==0) return 2;
	tile = _save->getTile(position + oneTileSouth);
	if (tile && tile->getMapData(O_NORTHWALL) && tile->getMapData(O_NORTHWALL)->getBlock(DT_NONE)==0) return 4;
	tile = _save->getTile(position);
	if (tile && tile->getMapData(O_WESTWALL) && tile->getMapData(O_WESTWALL)->getBlock(DT_NONE)==0) return 6;

	return -1;
}

/**
 * Validates a throw action.
 * @param action The action to validate.
 * @param originVoxel The origin point of the action.
 * @param targetVoxel The target point of the action.
 * @param depth Battlescape depth.
 * @param curve The curvature of the throw.
 * @param voxelType The type of voxel at which this parabola terminates.
 * @return Validity of action.
 */
bool TileEngine::validateThrow(BattleAction &action, Position originVoxel, Position targetVoxel, int depth, double *curve, int *voxelType, bool forced)
{
	if (originVoxel == targetVoxel)
		return false;
	bool foundCurve = false;
	double curvature = 0.5;
	if (action.type == BA_THROW)
	{
		curvature = std::max(0.48, 1.73 / sqrt(sqrt((double)(action.actor->getBaseStats()->strength) / (double)(action.weapon->getTotalWeight()))) + (action.actor->isKneeled()? 0.1 : 0.0));
	}
	else
	{
		// arcing projectile weapons assume a fixed strength and weight.(70 and 10 respectively)
		// curvature should be approximately 1.06358350461 at this point.
		curvature = 1.73 / sqrt(sqrt(70.0 / 10.0)) + (action.actor->isKneeled()? 0.1 : 0.0);
	}

	Tile *targetTile = _save->getTile(action.target);
	Position targetPos = targetVoxel.toTile();
	// object blocking - can't throw here
	if (action.type == BA_THROW
		&& targetTile
		&& targetTile->getMapData(O_OBJECT)
		&& targetTile->getMapData(O_OBJECT)->getTUCost(MT_WALK) == Pathfinding::INVALID_MOVE_COST
		&& !(targetTile->isBigWall()
		&& (targetTile->getMapData(O_OBJECT)->getBigWall()<1
		|| targetTile->getMapData(O_OBJECT)->getBigWall()>3)))
	{
		return false;
	}
	// out of range - can't throw here
	if (ProjectileFlyBState::validThrowRange(&action, originVoxel, targetTile, depth) == false)
	{
		return false;
	}

	std::vector<Position> trajectory;
	// thows should be around 10 tiles far, make one allocation that fit 99% cases with some margin
	trajectory.resize(16*20);
	// we try 8 different curvatures to try and reach our goal.
	int test = V_OUTOFBOUNDS;
	while (!foundCurve && curvature < 5.0)
	{
		trajectory.clear();
		test = calculateParabolaVoxel(originVoxel, targetVoxel, true, &trajectory, action.actor, curvature, Position(0,0,0));
		//position that item hit
		Position hitPos = (trajectory.back() + Position(0,0,1)).toTile();
		//position where item will land
		Position tilePos = Projectile::getPositionFromEnd(trajectory, Projectile::ItemDropVoxelOffset).toTile();
		if (forced || (test != V_OUTOFBOUNDS && tilePos == targetPos))
		{
			if (voxelType)
			{
				*voxelType = test;
			}
			foundCurve = true;
		}
		else
		{
			curvature += 0.5;
			if (test != V_OUTOFBOUNDS && action.actor->getFaction() == FACTION_PLAYER) //obstacle indicator is only for player
			{
				Tile* hitTile = _save->getTile(hitPos);
				if (hitTile)
				{
					hitTile->setObstacle(test);
				}
			}
		}
	}
	if (curvature >= 5.0)
	{
		return false;
	}
	if (curve)
	{
		*curve = curvature;
	}

	return true;
}

/**
 * Recalculates FOV of all units in-game.
 */
void TileEngine::recalculateFOV()
{
	for (auto* bu : *_save->getUnits())
	{
		if (bu->getTile() != 0)
		{
			calculateFOV(bu);
		}
	}
}

/**
 * Returns the direction from origin to target.
 * @param origin The origin point of the action.
 * @param target The target point of the action.
 * @return direction.
 */
int TileEngine::getDirectionTo(Position origin, Position target) const
{
	double ox = target.x - origin.x;
	double oy = target.y - origin.y;
	double angle = atan2(ox, -oy);
	// divide the pie in 4 angles each at 1/8th before each quarter
	double pie[4] = {(M_PI_4 * 4.0) - M_PI_4 / 2.0, (M_PI_4 * 3.0) - M_PI_4 / 2.0, (M_PI_4 * 2.0) - M_PI_4 / 2.0, (M_PI_4 * 1.0) - M_PI_4 / 2.0};
	int dir = 0;

	if (angle > pie[0] || angle < -pie[0])
	{
		dir = 4;
	}
	else if (angle > pie[1])
	{
		dir = 3;
	}
	else if (angle > pie[2])
	{
		dir = 2;
	}
	else if (angle > pie[3])
	{
		dir = 1;
	}
	else if (angle < -pie[1])
	{
		dir = 5;
	}
	else if (angle < -pie[2])
	{
		dir = 6;
	}
	else if (angle < -pie[3])
	{
		dir = 7;
	}
	else if (angle < pie[0])
	{
		dir = 0;
	}
	return dir;
}

/**
 * Calculate arc between two unit directions.
 * e.g. Arc of 7 and 3 is 4. Arc of 0 and 7 is 1.
 * @param directionA Value of 0 to 7
 * @param directionB Value of 0 to 7
 * @return Value in range of 0 - 4
 */
int TileEngine::getArcDirection(int directionA, int directionB) const
{
	return std::abs((((directionA - directionB) + 12) % 8) - 4);
}

/**
 * Gets the origin voxel of a certain action.
 * @param action Battle action.
 * @param tile Pointer to the action tile.
 * @return origin position.
 */
Position TileEngine::getOriginVoxel(BattleAction &action, Tile *tile)
{
	int unitSize = action.actor->getArmor()->getSize();
	int weaponShift = 4; // Original weapon position shift from the top of units head
	bool isArcingTrajectory = action.type == BA_THROW;
	if (action.weapon && action.weapon->getArcingShot(action.type)) isArcingTrajectory = true;

    // If small unit goes either precise aiming or kneeling, or improved LoF enabled
    bool improvedLof = (action.type == BA_AIMEDSHOT || action.actor->isKneeled() || Options::battleRealisticImprovedLof);
    if (Options::battleRealisticAccuracy && unitSize == 1 && improvedLof)
		weaponShift = 1; // ...move weapon to the eyes level

	if (!tile)
	{
		tile = action.actor->getTile();
	}

	Position origin = tile->getPosition();
	Tile *tileAbove = _save->getTile(origin + Position(0,0,1));
	Position originVoxel = Position(origin.x*16, origin.y*16, origin.z*24);

	// take into account soldier height and terrain level if the projectile is launched from a soldier
	if (action.actor->getPosition() == origin || action.type != BA_LAUNCH)
	{
		// calculate offset of the starting point of the projectile
		originVoxel.z += -tile->getTerrainLevel();

		originVoxel.z += action.actor->getHeight() + action.actor->getFloatHeight();

		if (action.type == BA_THROW)
		{
			originVoxel.z -= 3;
		}
		else
		{
			originVoxel.z -= weaponShift;
		}

		if (originVoxel.z >= (origin.z + 1)*24)
		{
			if (tileAbove && tileAbove->hasNoFloor(0))
			{
				origin.z++;
			}
			else
			{
				while (originVoxel.z >= (origin.z + 1)*24)
				{
					originVoxel.z--;
				}
				originVoxel.z -= weaponShift;
			}
		}

		if (Options::battleRealisticAccuracy && !isArcingTrajectory)
		{
			const int dirXshift[8] = {5, 6, 8,10,11,10, 8, 6};
			const int dirYshift[8] = {8, 6, 5, 6, 8,10,11,10};

			// Adjuct target tile to the centre of unit
			Tile *t = _save->getTile( action.target );
			if (t)
			{
				BattleUnit *targetUnit = t->getUnit();
				if (targetUnit)
				{
					int targetSize = targetUnit->getArmor()->getSize();
					Position targetVoxel = targetUnit->getPosition().toVoxel() + Position(8*targetSize, 8*targetSize, 0);
					action.target = targetVoxel.toTile();
				}
			}

			int direction = getDirectionTo(origin, action.target);

			// Offset for different relativeOrigin values
			switch (action.relativeOrigin)
			{
			case BattleActionOrigin::CENTRE:
				originVoxel.x += 8 * unitSize; // Shoot straight from the eye point
				originVoxel.y += 8 * unitSize; // moving barrel in front of unit breaks LOF near walls with existing LOS above them
				break;

			case BattleActionOrigin::LEFT:
				originVoxel.x += dirXshift[ direction ] * unitSize;
				originVoxel.y += dirYshift[ direction ] * unitSize;
				break;

			case BattleActionOrigin::RIGHT:
				direction = (direction + 4) % 8;
				originVoxel.x += dirXshift[ direction ] * unitSize;
				originVoxel.y += dirYshift[ direction ] * unitSize;
				break;
			};
		}
		else
		{
			const int dirXshift[8] = {8, 14,15,15,8, 1, 1, 1};
			const int dirYshift[8] = {1, 1, 8, 15,15,15,8, 1};

			int direction = getDirectionTo(origin, action.target);

			// Offset for different relativeOrigin values
			switch (action.relativeOrigin)
			{
			case BattleActionOrigin::CENTRE:
				// Standard offset.
				originVoxel.x += dirXshift[ direction ] * unitSize;
				originVoxel.y += dirYshift[ direction ] * unitSize;
				break;

				// 2:1 Weighted average of the standard offset and a rotation, either left or right.
			case BattleActionOrigin::LEFT:
				originVoxel.x += ((2 * dirXshift[ direction ] + dirXshift[(direction + 7) % 8]) * unitSize + 1) / 3;
				originVoxel.y += ((2 * dirYshift[ direction ] + dirYshift[(direction + 7) % 8]) * unitSize + 1) / 3;
				break;

			case BattleActionOrigin::RIGHT:
				originVoxel.x += ((2 * dirXshift[ direction ] + dirXshift[(direction + 1) % 8]) * unitSize + 1) / 3;
				originVoxel.y += ((2 * dirYshift[ direction ] + dirYshift[(direction + 1) % 8]) * unitSize + 1) / 3;
				break;
			};
		};
	}
	else
	{
		// don't take into account soldier height and terrain level if the projectile is not launched from a soldier(from a waypoint)
		originVoxel.x += 8;
		originVoxel.y += 8;
		originVoxel.z += 16;
	}
	return originVoxel;
}

/**
 * mark a region of the map as "dangerous" for a turn.
 * @param pos is the epicenter of the explosion.
 * @param radius how far to spread out.
 * @param unit the unit that is triggering this action.
 */
void TileEngine::setDangerZone(Position pos, int radius, BattleUnit *unit)
{
	Tile *tile = _save->getTile(pos);
	if (!tile)
	{
		return;
	}
	// set the epicenter as dangerous
	tile->setDangerous(true);
	Position originVoxel = pos.toVoxel() + Position(8,8,12 + -tile->getTerrainLevel());
	Position targetVoxel;
	for (int x = -radius; x != radius; ++x)
	{
		for (int y = -radius; y != radius; ++y)
		{
			// we can skip the epicenter
			if (x != 0 || y != 0)
			{
				// make sure we're within the radius
				if ((x*x)+(y*y) <= (radius*radius))
				{
					tile = _save->getTile(pos + Position(x,y,0));
					if (tile)
					{
						targetVoxel = ((pos + Position(x,y,0)).toVoxel()) + Position(8,8,12 + -tile->getTerrainLevel());
						std::vector<Position> trajectory;
						// we'll trace a line here, ignoring all units, to check if the explosion will reach this point
						// granted this won't properly account for explosions tearing through walls, but then we can't really
						// know that kind of information before the fact, so let's have the AI assume that the wall (or tree)
						// is enough to protect them.
						if (calculateLineVoxel(originVoxel, targetVoxel, true, &trajectory, unit, unit) == V_EMPTY)
						{
							if (trajectory.size() && (trajectory.back().toTile()) == pos + Position(x,y,0))
							{
								tile->setDangerous(true);
							}
						}
					}
				}
			}
		}
	}
}

/**
 * Checks if a position is a valid place for a unit to be placed
 * @param position Pointer to the position to check
 * @param unit Pointer to the unit
 * @param checkSurrounding Do we need to check the 8 tiles around the selected one? (default false)
 * @param startSurroundingCheckDirection Which direction should we start the check? (default 0 = north)
 * @return true if we found a valid position and stored it in the pointer passed to the function
 */
bool TileEngine::isPositionValidForUnit(Position &position, BattleUnit *unit, bool checkSurrounding, int startSurroundingCheckDirection)
{
	int unitSize = unit->getArmor()->getSize();
	std::vector<Position > positionsToCheck;
	positionsToCheck.push_back(position);
	if (checkSurrounding)
	{
		// Look up the surrounding directions
		int surroundingTilePositions [8][2] = {
			{0, -1}, // north (-y direction)
			{1, -1}, // northeast
			{1, 0}, // east (+ x direction)
			{1, 1}, // southeast
			{0, 1}, // south (+y direction)
			{-1, 1}, // southwest
			{-1, 0}, // west (-x direction)
			{-1, -1}}; // northwest
		for (int i = 0; i < 8; i++)
		{
			positionsToCheck.push_back(position + Position(surroundingTilePositions[(startSurroundingCheckDirection + i) % 8][0] * unitSize, surroundingTilePositions[(startSurroundingCheckDirection + i) % 8][1] * unitSize, 0));
		}
	}

	for (const auto& pos : positionsToCheck)
	{
		bool passedCheck = true;

		for (int x = unitSize - 1; x >= 0; x--)
		{
			for (int y = unitSize - 1; y >= 0; y--)
			{
				// Make sure the location is in bounds and nothing blocks being there
				Position positionToCheck = pos + Position(x, y, 0);
				Tile* tileToCheck = _save->getTile(positionToCheck);
				if (!tileToCheck || (tileToCheck->getUnit() && tileToCheck->getUnit() != unit) ||
					tileToCheck->getTUCost(O_OBJECT, unit->getMovementType()) == Pathfinding::INVALID_MOVE_COST ||
					(tileToCheck->getMapData(O_OBJECT) && tileToCheck->getMapData(O_OBJECT)->getBigWall() && tileToCheck->getMapData(O_OBJECT)->getBigWall() <= 3))
				{
					passedCheck = false;
				}
			}
		}

		// Extra test for large units
		if (passedCheck && unitSize > 1)
		{
			_save->getPathfinding()->setUnit(unit); //TODO: remove as was required by `isBlockedDirection`
			for (int dir = 2; dir <= 4; ++dir)
			{
				if (_save->getPathfinding()->isBlockedDirection(unit, _save->getTile(pos), dir))
				{
					passedCheck = false;
				}
			}
		}

		if (passedCheck)
		{
			position = pos;
			return true;
		}
	}

	return false;
}

/**
 * Update game state after script hook execution. We need check state of units
 * and handle special cases like deaths or unit tranformation.
 * For now we assume that Light and FOV is affected only in small area,
 * this mean we could glitch if multiple units are affected by script logic.
 * @param battleActionAttack Data of action that triggeted script hook.
 * @param pos Postion to update light and Fov, can be invalid if we do not update light
 */
void TileEngine::updateGameStateAfterScript(BattleActionAttack battleActionAttack, Position pos)
{
	_save->getBattleGame()->checkForCasualties(nullptr, battleActionAttack, false, false);

	_save->reviveUnconsciousUnits(true);

	_save->getBattleGame()->convertInfected();

	if (pos != TileEngine::invalid)
	{
		// limit area of the following calls to the Position pos
		calculateLighting(LL_ITEMS, pos, 2, true);
		calculateFOV(pos, 1, false);
	}
}

bool TileEngine::isNextToDoor(Tile* tile, bool flipDoor)
{
	if (tile == NULL)
		return false;
	if (tile->isDoor(O_NORTHWALL) || tile->isDoor(O_WESTWALL) || ((tile->isUfoDoor(O_NORTHWALL) || tile->isUfoDoor(O_WESTWALL)) && !flipDoor) )
		return true;
	Position neighbourSouth = tile->getPosition();
	neighbourSouth += Position(0, 1, 0);
	Tile* tileSouth = _save->getTile(neighbourSouth);
	if (tileSouth != NULL && (tileSouth->isDoor(O_NORTHWALL) || tileSouth->isUfoDoor(O_NORTHWALL) && !flipDoor))
		return true;
	Position neighbourEast = tile->getPosition();
	neighbourEast += Position(1, 0, 0);
	Tile *tileEast = _save->getTile(neighbourEast);
	if (tileEast != NULL && (tileEast->isDoor(O_WESTWALL) || tileEast->isUfoDoor(O_WESTWALL) && !flipDoor))
		return true;
	return false;
}

bool TileEngine::isNearDoor(Tile* tile)
{
	if (isNextToDoor(tile))
		return true;
	Position checkPosition = tile->getPosition();
	checkPosition += Position(0, -1, 0);
	Tile* checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	checkPosition = tile->getPosition();
	checkPosition += Position(1, -1, 0);
	checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	checkPosition = tile->getPosition();
	checkPosition += Position(1, 0, 0);
	checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	checkPosition = tile->getPosition();
	checkPosition += Position(1, 1, 0);
	checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	checkPosition = tile->getPosition();
	checkPosition += Position(0, 1, 0);
	checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	checkPosition = tile->getPosition();
	checkPosition += Position(-1, 1, 0);
	checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	checkPosition = tile->getPosition();
	checkPosition += Position(-1, 0, 0);
	checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	checkPosition = tile->getPosition();
	checkPosition += Position(-1, -1, 0);
	checkTile = _save->getTile(checkPosition);
	if (isNextToDoor(checkTile))
		return true;
	return false;
}

std::set<Tile*> TileEngine::visibleTilesFrom(BattleUnit* unit, Position pos, int direction, bool onlyNew, bool ignoreAirTiles)
{
	std::set<Tile*> visibleFrom;

	std::vector<Position> _trajectory;
	bool swap = (direction == 0 || direction == 4);
	const int signX[8] = {+1, +1, +1, +1, -1, -1, -1, -1};
	const int signY[8] = {-1, -1, -1, +1, +1, +1, -1, -1};
	int y1, y2;
	Position posTest;

	if ((unit->getHeight() + unit->getFloatHeight() + -_save->getTile(pos)->getTerrainLevel()) >= 24 + 4)
	{
		Tile* tileAbove = _save->getTile(pos + Position(0, 0, 1));
		if (tileAbove && tileAbove->hasNoFloor(0))
		{
			++pos.z;
		}
	}

	// Test all tiles within view cone for visibility.
	int maxDist = _save->getMod()->getMaxViewDistance();
	if (Options::aiPerformanceOptimization)
	{
		int myUnits = 0;
		for (BattleUnit* bu : *(_save->getUnits()))
		{
			if (bu->getFaction() == unit->getFaction() && !bu->isOut())
				++myUnits;
		}
		float scaleFactor = (float)60 * 60 * 4 * 30 / (_save->getMapSizeXYZ() * myUnits);
		maxDist = std::min(60, _save->getMod()->getMaxViewDistance());
		if (scaleFactor < 1)
			maxDist *= scaleFactor;
	}
	for (int x = 0; x <= maxDist; ++x) // TODO: Possible improvement: find the intercept points of the arc at max view distance and choose a more intelligent sweep of values when an event arc is defined.
	{
		if (direction & 1)
		{
			y1 = 0;
			y2 = maxDist;
		}
		else
		{
			y1 = -x;
			y2 = x;
		}
		for (int y = y1; y <= y2; ++y) // TODO: Possible improvement: find the intercept points of the arc at max view distance and choose a more intelligent sweep of values when an event arc is defined.
		{
			const int distanceSqr = x * x + y * y;
			if (distanceSqr >= 0)
			{
				posTest.x = pos.x + signX[direction] * (swap ? y : x);
				posTest.y = pos.y + signY[direction] * (swap ? x : y);
				// Only continue if the column of tiles at (x,y) is within the narrow arc of interest (if enabled)
				for (int z = 0; z < _save->getMapSizeZ(); z++)
				{
					posTest.z = z;

					if (_save->getTile(posTest)) // inside map?
					{
						if (ignoreAirTiles)
						{
							// skip air tiles
							if (_save->getTile(posTest)->hasNoFloor())
								continue;
						}
						// this sets tiles to discovered if they are in LOS - tile visibility is not calculated in voxelspace but in tilespace
						// large units have "4 pair of eyes"
						int size = unit->getArmor()->getSize();
						for (int xo = 0; xo < size; xo++)
						{
							for (int yo = 0; yo < size; yo++)
							{
								Position poso = pos + Position(xo, yo, 0);
								_trajectory.clear();
								int tst = calculateLineTile(poso, posTest, _trajectory);
								if (tst > 127)
								{
									// Vision impacted something before reaching posTest. Throw away the impact point.
									_trajectory.pop_back();
								}
								// Reveal all tiles along line of vision. Note: needed due to width of bresenham stroke.
								for (const auto& posVisited : _trajectory)
								{
									// Add tiles to the visible list only once. BUT we still need to calculate the whole trajectory as
									//  this bresenham line's period might be different from the one that originally revealed the tile.
									if (x <= getMaxViewDistance() && y <= getMaxViewDistance() && distanceSqr <= getMaxViewDistanceSq())
									{
										Tile* tile = _save->getTile(posVisited);
										if (tile->getUnit())
											continue;
										if (!onlyNew || tile->getLastExplored(unit->getFaction()) < _save->getTurn())
											visibleFrom.insert(tile);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return visibleFrom;
}

void TileEngine::setVisibilityCache(Position from, Position to, bool visible)
{
	std::pair<int, int> key = std::make_pair(_save->getTileIndex(from), _save->getTileIndex(to));
	_visibilityCache.insert({key, visible});
}

bool TileEngine::getVisibilityCache(Position from, Position to)
{
	std::pair<int, int> key = std::make_pair(_save->getTileIndex(from), _save->getTileIndex(to));
	return _visibilityCache[key];
}

bool TileEngine::hasEntry(Position from, Position to)
{
	std::pair<int, int> key = std::make_pair(_save->getTileIndex(from), _save->getTileIndex(to));
	if (_visibilityCache.find(key) != _visibilityCache.end())
		return true;
	return false;
}

void TileEngine::resetVisibilityCache()
{
	_visibilityCache.clear();
}

}
