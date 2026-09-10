#ifndef LGAMEEVENTS_H
#define LGAMEEVENTS_H
#ifdef _WIN32
#pragma once
#endif

/*
** Ported from Experiment: Source (src/public/lgameevents.h).
**
** The listener has to outlive the Lua state's script loading, so it is created with
** the state and removed again on shutdown; luamanager.cpp calls these.
*/
void InitializeLuaGameEventHandler( lua_State *L );
void ShutdownLuaGameEventHandler( lua_State *L );

#endif  // LGAMEEVENTS_H
