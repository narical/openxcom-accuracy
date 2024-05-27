#include "LuaState.h"
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

#include "LuaState.h"
#include "../Engine/Logger.h"

namespace OpenXcom
{

//we have to use globals to store some of the data needed for the C style callbacks
std::filesystem::path CurrentScriptPath;


int LuaPrint(lua_State* luaState)
{
	lua_Debug ar;
	lua_getstack(luaState, 1, &ar);
	lua_getinfo(luaState, "nSl", &ar);

	int num = lua_gettop(luaState);
	for (int i = 1; i <= num; i++)
	{
		size_t l;
		std::string message(luaL_tolstring(luaState, i, &l)); /* convert it to string */
		if (i > 1)                                            /* not the first element? */
			message = "\t" + message;
		Log(LOG_INFO) << "LUA - " << CurrentScriptPath.string() << " - " << message;
	}
	return 0;
}

void LuaWarning(void* ud, const char* msg, int tocont)
{
	(void)tocont;
	lua_State* luaState = (lua_State*)ud;
	lua_Debug ar;
	lua_getstack(luaState, 1, &ar);
	lua_getinfo(luaState, "nSl", &ar);

	Log(LOG_WARNING) << CurrentScriptPath << " " << msg;
}

int LuaPanic(lua_State* luaState)
{
	const char* msg = lua_tostring(luaState, -1);
	if (msg == NULL)
		msg = "panic error object is not a string";
	Log(LOG_ERROR) << CurrentScriptPath << " " << msg;
	return 0; /* return to Lua to abort */
}

LuaState::LuaState(const std::filesystem::path& scriptPath, const ModData* modData)
{
	_state = nullptr;
	_error = false;
	_errorString = "";
}

LuaState::~LuaState()
{
	//cleanly shut down the Lua state
	if (_state)
	{
		lua_close(_state);
	}
}

const std::filesystem::path& LuaState::getScriptPath() const
{
	return _scriptPath;
}

const ModData* LuaState::getModData() const
{
	return _modData;
}

bool LuaState::loadScript(const std::filesystem::path& filename)
{
	// check the file exists
	if (!std::filesystem::exists(filename))
	{
		Log(LOG_ERROR) << "LuaState::loadScript: File " << filename << " does not exist.";
		return false;
	}

	// store the script path
	_scriptPath = filename;

	// create the lua state
	_state = luaL_newstate();
	if (!_state)
	{
		Log(LOG_ERROR) << "LuaState::loadScript: Could not create lua state.";
		return false;
	}

	// open the standard libraries
	luaL_openlibs(_state);
	lua_setwarnf(_state, LuaWarning, _state);
	lua_atpanic(_state, LuaPanic);

	// custom overrides
	static const luaL_Reg overrides[] = {
		{"print", LuaPrint},
		{NULL, NULL}};

	lua_getglobal(_state, "_G");
	luaL_setfuncs(_state, overrides, 0);
	lua_pop(_state, 1);

	// load the script
	CurrentScriptPath = _scriptPath;
	if (luaL_loadfile(_state, filename.string().c_str()) != LUA_OK)
	{
		Log(LOG_ERROR) << "LuaState::loadScript: Could not load script " << filename << ": " << lua_tostring(_state, -1);
		lua_close(_state);
		_state = nullptr;
		return false;
	}

	// run the script
	if (lua_pcall(_state, 0, 0, 0) != LUA_OK)
	{
		Log(LOG_ERROR) << "LuaState::loadScript: Could not run script " << filename << ": " << lua_tostring(_state, -1);
		lua_close(_state);
		_state = nullptr;
		return false;
	}
	CurrentScriptPath.clear();

	return false;
}

}
