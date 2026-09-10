//=============================================================================//
//
// Purpose: The GameEvents library, ported from Experiment: Source
//          (src/public/lgameevents.cpp).
//
//          gameevent.Listen( name ) makes the engine call the Lua hook of the same
//          name whenever that game event fires, with the event's data as a table --
//          which is what GMod's gameevent.Listen does.
//
//          Adapted: LUA_CALL_HOOK_FOR_STATE_BEGIN/END call HL2SB's `hook.call` (not
//          `hook.Call`) and pass _GAMEMODE, matching how every other HL2SB binding
//          invokes a hook; the library is also published as the global `gameevent`,
//          GMod's spelling, next to Experiment's `GameEvents`.
//
//=============================================================================//

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lgameevents.h"
#include "igameevents.h"
#include "lColor.h"
#include "tier1/LKeyValues.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CLuaGameEventListener : public IGameEventListener2
{
    protected:
    lua_State *L;

    public:
    CLuaGameEventListener( lua_State *L )
    {
        this->L = L;
    }

    void AddListener( const char *name )
    {
        gameeventmanager->AddListener( this, name, false );
    }

    // IGameEventListener2 Interface:
    public:
    virtual void FireGameEvent( IGameEvent *event )
    {
        const char *eventName = event->GetName();

        LUA_CALL_HOOK_FOR_STATE_BEGIN( this->L, eventName );
        lua_pushkeyvalues_as_table( this->L, event->m_pDataKeys );
        LUA_CALL_HOOK_FOR_STATE_END( this->L, 1, 0 );
    }
};

static CLuaGameEventListener *luaGameEventListener;

void InitializeLuaGameEventHandler( lua_State *L )
{
    luaGameEventListener = new CLuaGameEventListener( L );
}

void ShutdownLuaGameEventHandler( lua_State *L )
{
    if ( luaGameEventListener == NULL )
        return;

    gameeventmanager->RemoveListener( luaGameEventListener );
    delete luaGameEventListener;
    luaGameEventListener = NULL;
}

LUA_REGISTRATION_INIT( GameEvents )

LUA_BINDING_BEGIN( GameEvents, Listen, "library", "Call a hook for this game event name, when the event occurs." )
{
    luaGameEventListener->AddListener( LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "eventName" ) );
    return 0;
}
LUA_BINDING_END()

/*
** Open GameEvents library
*/
LUALIB_API int luaopen_GameEvents( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( GameEvents );

    // GMod spells this `gameevent` (gameevent.Listen); publish both spellings onto
    // the table luaL_register just left on the stack.
    lua_pushvalue( L, -1 );
    lua_setglobal( L, "gameevent" );

    return 1;
}
