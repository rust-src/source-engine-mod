//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ICLASSMAP_H
#define ICLASSMAP_H
#ifdef _WIN32
#pragma once
#endif

class C_BaseEntity;
typedef C_BaseEntity* (*DISPATCHFUNCTION)( void );

abstract_class IClassMap
{
public:
	virtual					~IClassMap() {}

#ifdef LUA_SDK
	virtual void			Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory = 0, bool scripted = false ) = 0;
	virtual void			RemoveAllScripted( void ) = 0;
#else
	virtual void			Add( const char *mapname, const char *classname, int size, DISPATCHFUNCTION factory = 0 ) = 0;
#endif
	virtual char const		*Lookup( const char *classname ) = 0;
#ifdef LUA_SDK
	virtual DISPATCHFUNCTION FindFactory( const char *classname ) = 0;
#endif
	virtual C_BaseEntity	*CreateEntity( const char *mapname ) = 0;
	virtual int				GetClassSize( const char *classname ) = 0;
};

extern IClassMap& GetClassMap();

//-----------------------------------------------------------------------------
// HL2SB: dynamic content enumeration for SMenu (implementation in classmap.cpp).
//
// IClassMap is the one client-side dictionary that carries BOTH the stock
// classes (LINK_ENTITY_TO_CLASS) and the classes registered at RUNTIME from Lua
// (RegisterScriptedEntity / RegisterScriptedWeapon in basescripted.cpp /
// weapon_hl2mpbase_scriptedweapon.cpp), so SMenu enumerates it instead of
// reading addons/menu/entitylist.txt.
//
// The interface itself is deliberately NOT touched: these are plain free
// functions with an ordinal-based accessor, so there is no vtable entry and no
// class-layout change (waf does not track header dependencies, and a changed
// vtable would silently mix ABIs across the already-built objects).
//-----------------------------------------------------------------------------
int			ClassMap_GetEntryCount( void );
const char *ClassMap_GetEntryName( int iEntry );	// entity class name
const char *ClassMap_GetEntryCPPName( int iEntry );	// registered C++ class name
bool		ClassMap_IsEntryScripted( int iEntry );	// true = registered from a Lua script


#endif // ICLASSMAP_H
