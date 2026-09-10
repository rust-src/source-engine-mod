//=============================================================================//
//
// Purpose: The Sounds library, ported from Experiment: Source
//          (src/public/lsounds.cpp).
//
//          Kept verbatim from upstream except for the adaptations listed below;
//          GMod's sound.Add / sound.Play are built on this (its `sound` table is
//          aliased onto Sounds by the Lua content).
//
//          Dropped from the port:
//            * the AudioChannel userdata and every Sounds.PlayUrl / Sounds.PlayFile
//              binding -- all of them go through Experiment's util/bassmanager,
//              which wraps the BASS audio library for URL/file streaming.  HL2SB
//              does not ship BASS, and nothing else in the library depends on it.
//            * _E.PLAY_SOUND_FLAG, which only existed to describe BASS flags.
//
//          Adapted:
//            * EmitSound() is CBaseEntity::EmitSound() here; upstream has a free
//              function of that name in SoundEmitterSystem.cpp.
//            * the recipient-filter optional argument is tested with a local
//              helper instead of lua_isrecipientfilter() (not ported).
//            * Sounds.Add read Volume twice (the first read's value was popped
//              again); only the second, which also handles the table form, is kept.
//            * Sounds.Play returned 0 after pushing the duration, so callers never
//              saw it; it returns the duration now.
//
//=============================================================================//

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lsounds.h"
#include "engine/IEngineSound.h"
#include "mathlib/lvector.h"
#ifdef CLIENT_DLL
#include "c_recipientfilter.h"
#include "lc_recipientfilter.h"
#else
#include "recipientfilter.h"
#include "lrecipientfilter.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ISoundEmitterSystemBase *soundemitterbase;

// The recipient-filter argument of Sounds.Play is optional: accept either filter
// type, and anything else means "build a PAS attenuation filter around origin".
static bool lua_issoundsrecipientfilter (lua_State *L, int idx) {
  return luaL_testudata(L, idx, "CRecipientFilter") != NULL ||
         luaL_testudata(L, idx, "CPASFilter") != NULL;
}

LUA_REGISTRATION_INIT( Sounds )

LUA_BINDING_BEGIN( Sounds, Add, "library", "Creates a sound script." )
{
    if ( !LUA_BINDING_ARGUMENT( lua_istable, 1, "soundData" ) )
    {
        luaL_argerror( L, 1, "expected table" );
        return 0;
    }

    CSoundParametersInternal parameters;

    // For gmod compat we check both lowecase and UpperCamelCase
    GET_FIELD_WITH_COMPATIBILITY_OR_ERROR( L, 1, "Name", "name", lua_isstring );
    const char *name = luaL_checkstring( L, -1 );
    lua_pop( L, 1 );  // pop the name value

    GET_FIELD_WITH_COMPATIBILITY_OR_ERROR( L, 1, "Channel", "channel", lua_isnumber );
    parameters.SetChannel( luaL_checknumber( L, -1 ) );
    lua_pop( L, 1 );  // pop the channel value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Level", "level" );
    if ( lua_isnumber( L, -1 ) )
        parameters.SetSoundLevel( luaL_checknumber( L, -1 ) );
    else
        parameters.SetSoundLevel( SNDLVL_NORM );
    lua_pop( L, 1 );  // pop the level value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Volume", "volume" );
    if ( lua_istable( L, -1 ) )
    {
        lua_rawgeti( L, -1, 1 );
        lua_rawgeti( L, -2, 2 );
        parameters.SetVolume( luaL_checknumber( L, -2 ), luaL_checknumber( L, -1 ) );
        lua_pop( L, 2 );  // pop the volume values
    }
    else if ( lua_isnumber( L, -1 ) )
    {
        parameters.SetVolume( luaL_checknumber( L, -1 ), 0 );
    }
    else
    {
        parameters.SetVolume( 1.0f, 0 );
    }
    lua_pop( L, 1 );  // pop the volume value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Pitch", "pitch" );
    if ( lua_istable( L, -1 ) )
    {
        lua_rawgeti( L, -1, 1 );
        lua_rawgeti( L, -2, 2 );
        parameters.SetPitch( luaL_checknumber( L, -2 ), luaL_checknumber( L, -1 ) );
        lua_pop( L, 2 );  // pop the pitch values
    }
    else if ( lua_isnumber( L, -1 ) )
    {
        parameters.SetPitch( luaL_checknumber( L, -1 ), 0 );
    }
    else
    {
        parameters.SetPitch( 100, 0 );
    }
    lua_pop( L, 1 );  // pop the pitch value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Sound", "sound" );
    if ( lua_istable( L, -1 ) )
    {
        // Loop through the table and add each sound file
        lua_pushnil( L );

        while ( lua_next( L, -2 ) != 0 )
        {
            if ( lua_isstring( L, -1 ) )
            {
                CUtlSymbol soundSymbol = soundemitterbase->AddWaveName( luaL_checkstring( L, -1 ) );
                SoundFile soundFile;
                soundFile.symbol = soundSymbol;
                soundFile.gender = GENDER_NONE;
                parameters.AddSoundName( soundFile );
            }
            lua_pop( L, 1 );
        }
    }
    else if ( lua_isstring( L, -1 ) )
    {
        CUtlSymbol soundSymbol = soundemitterbase->AddWaveName( luaL_checkstring( L, -1 ) );
        SoundFile soundFile;
        soundFile.symbol = soundSymbol;
        soundFile.gender = GENDER_NONE;
        parameters.AddSoundName( soundFile );
    }
    else
    {
        luaL_argerror( L, 1, "expected field 'sound' to be a string or table of strings" );
        return 0;
    }
    lua_pop( L, 1 );  // pop the sound value

    // TODO: Check if the file needs to exist, or if we can just create a sound script without a file
    soundemitterbase->AddSound( name, "scripts/sounds/lua_procedural.txt", parameters );

    return 0;
}
LUA_BINDING_END()

// lua_run_cl Sounds.Play("ambient/levels/labs/teleport_alarm_loop1.wav", Vectors.Create(0, 0, 0))
LUA_BINDING_BEGIN( Sounds, Play, "library", "Plays a sound emitting from a place in the world. Not properly tested for sound script names (didn't work when I tried it)" )
{
    const char *pszSoundName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "soundName" );  // doc: sound script name or sound file name relative to sound/ folder
    const Vector vecOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "origin" );       // doc: position of the sound
    int entityIndex = ( int )LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 3, SOUND_FROM_WORLD, "entity" );
    SOUND_CHANNEL channel = ( SOUND_CHANNEL )LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, CHAN_AUTO, "channel" );
    float flVolume = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 5, 1, "volume" );
    soundlevel_t soundLevel = LUA_BINDING_ARGUMENT_ENUM_WITH_DEFAULT( soundlevel_t, 6, SNDLVL_NORM, "soundLevel" );
    int soundFlags = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 7, 0, "soundFlags" );
    float flPitchPercent = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 8, 100, "pitchPercent" );
    int nDSP = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 9, 0, "dsp" );
    lua_CRecipientFilter filter;

    if ( lua_issoundsrecipientfilter( L, 10 ) )
    {
        filter = LUA_BINDING_ARGUMENT_NILLABLE( luaL_checkrecipientfilter, 10, "filter" );
    }
    else
    {
        filter = CPASAttenuationFilter( vecOrigin, soundLevel );
    }

    float duration = 0;

    EmitSound_t params;
    params.m_pSoundName = pszSoundName;
    params.m_pOrigin = &vecOrigin;
    params.m_flVolume = flVolume;
    params.m_SoundLevel = soundLevel;
    params.m_nPitch = flPitchPercent;
    params.m_nSpecialDSP = nDSP;
    params.m_flSoundTime = 0;
    params.m_pflSoundDuration = &duration;
    params.m_bWarnOnDirectWaveReference = false;
    params.m_nChannel = channel;
    params.m_nFlags = soundFlags;

    CBaseEntity::EmitSound( filter, entityIndex, params );

    // Upstream pushes the duration and then returns 0, which throws the value away
    // (the returned count is what Lua sees).  Return it instead.
    lua_pushnumber( L, duration );

    return 1;
}
LUA_BINDING_END()

/*
** Open Sounds library
*/
LUALIB_API int luaopen_Sounds( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Sounds );

    return 1;
}
