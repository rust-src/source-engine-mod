//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB "SMenu" - the fork's own GMod-style spawn menu.
//
// HL2SB: the menu used to be driven by a local, pre-dumped text list
// (addons/menu/entitylist.txt, written by the server's `dumpentitytofile`).
// It is now built DYNAMICALLY out of the client's own class dictionary:
//
//   GetClassMap() / IClassMap (game/client/classmap.cpp, declared in
//   game/client/iclassmap.h) is the ONE client-side dictionary that holds BOTH
//     * the stock classes registered by LINK_ENTITY_TO_CLASS
//       (game/shared/predictable_entity.h:146 - on the client this macro adds
//       to the class map instead of to the server factory dictionary), and
//     * everything registered at RUNTIME from Lua: RegisterScriptedEntity()
//       (game/shared/lua/basescripted.cpp:52) and RegisterScriptedWeapon()
//       (game/shared/lua/weapon_hl2mpbase_scriptedweapon.cpp:64).
//   Both Lua loaders run on the client too (cdll_client_int.cpp:1807-1808,
//   luasrc_LoadWeapons/luasrc_LoadEntities), so ported GMod SWEPs and scripted
//   entities are in this dictionary by the time the menu is first opened.
//
// Why the class map and not the other candidates:
//   * CHLClient::GetAllClasses() (cdll_client_int.cpp:1492) walks the
//     ClientClass chain (public/client_class.h:49).  That enumerates the
//     NETWORKED classes only - and a Lua SENT/SWEP reuses DT_BaseScripted /
//     DT_HL2MPScriptedWeapon and carries its real class name in a string, so
//     sent_ball / pist_weagon are NOT in that chain.  The class map has them.
//   * The server's IEntityFactoryDictionary (game/server/util.h:95) is the
//     authoritative "what can `ent_create` build" list, but it lives in
//     server.dll.  A client-side menu cannot read it without the server
//     publishing it (network string table or usermessage), and it does not need
//     to: the stock classes are registered on both realms, and the Lua loaders
//     register scripted classes on both realms as well.  What this deliberately
//     does NOT list is the server-only, non-networked helper soup that made the
//     old text list unusable (ai_*, logic_*, math_*, path_*, filter_*, func_*,
//     ...): those have no client class and no place in a spawn menu.
//
// The list is built once per level (first open, and again when the level
// changes) - never per frame.  Clicking an entry uses the paths this menu has
// always used: `give <class>` for weapons (only when sm_menu_give is 1) and
// `ent_create <class>` otherwise.  No new console command is introduced.
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include <stdio.h>
#include "sm_menu_list.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/IVGui.h>
#include <vgui/MouseCode.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/PanelListPanel.h>
#include <vgui_controls/Frame.h>
#include "filesystem.h"
#include "iclassmap.h"
#include "networkstringtable_clientdll.h"
#include "luamanager.h"

#include "tier0/memdbgon.h"

using namespace vgui;

// HL2SB: the network string table the server publishes its entity factory
// dictionary in.  Created in CServerGameDLL::CreateNetworkStringTables and
// filled by SMenu_PublishEntityList (game/server/gameinterface.cpp); keep the
// two spellings in step.
#define SMENU_ENTITYLIST_TABLE	"SMenuEntityList"

//-----------------------------------------------------------------------------
// HL2SB: category / flag bits for one dynamic entry.
//-----------------------------------------------------------------------------
enum
{
	SMCAT_WEAPON	= 1 << 0,	// weapon / item / ammo, or a Lua SWEP
	SMCAT_LUAENT	= 1 << 1,	// scripted entity (SENT) from lua/entities
	SMCAT_ENTITY	= 1 << 2,	// any other spawnable class
	SMCAT_NPC		= 1 << 3,
	SMCAT_PROP		= 1 << 4,
	SMCAT_VEHICLE	= 1 << 5,
	SMFLAG_LUA		= 1 << 6,	// registered from Lua (scripted_ents / weapons)
	SMFLAG_WEAPON	= 1 << 7,	// sm_menu_give may hand this one to the player
};

// Room for a verbatim spawn line.  The longest one this menu builds is a vehicle:
//   ent_create prop_vehicle_prisoner_pod model models/vehicles/prisoner_pod.mdl
//   vehiclescript scripts/vehicles/prisoner_pod.txt
// which is 124 bytes, so 192 leaves headroom for a longer addon path.
#define SMENU_FIXEDCMD_LEN	192

struct SMenuEntry_t
{
	char			szClass[64];	// class name, or "props_x/model" for model entries
	char			szFixedCmd[SMENU_FIXEDCMD_LEN];	// non-empty: run verbatim
	char			szMaterial[128];// material to draw ("" = no icon, name only)
	unsigned int	uFlags;
};

// Everything the class dictionary yielded, in dictionary (alphabetical) order.
static CUtlVector< SMenuEntry_t > g_SMenuEntries;

// One page of the tree: which entries it shows, and where they come from.
struct SMenuCatDef_t
{
	const char	*pszTitle;
	unsigned int uMask;
	unsigned int uNotMask;
	bool		bModelPage;		// the models/props_* scan instead of class entries
};

static const SMenuCatDef_t s_SMenuCats[] =
{
	{ "Weapons",				SMCAT_WEAPON,							0,				false },
	{ "    Lua SWEPs",			SMCAT_WEAPON | SMFLAG_LUA,				0,				false },
	{ "    Stock weapons",		SMCAT_WEAPON,							SMFLAG_LUA,		false },
	{ "Entities",				SMCAT_LUAENT | SMCAT_ENTITY,			0,				false },
	{ "    Lua entities",		SMCAT_LUAENT,							0,				false },
	{ "    Stock entities",		SMCAT_ENTITY,							0,				false },
	{ "NPCs",					SMCAT_NPC,								0,				false },
	{ "Props",					SMCAT_PROP,								0,				false },
	{ "Vehicles",				SMCAT_VEHICLE,							0,				false },
	{ "Props (models)",			0,										0,				true },
};

#define SMENU_CAT_COUNT		ARRAYSIZE( s_SMenuCats )

// HL2SB: what clicking a Weapons entry does.
//   0 (DEFAULT) - ent_create <weapon>: place the weapon ENTITY in the world,
//                 where you are looking ("point and place").  This is what the
//                 weapons page has always done, and what a spawn menu should do.
//   1           - give it straight to the player instead, the way GMod's
//                 spawnmenu does.
// (Declared before the classes below because CSMIconButton reads it.)
ConVar sm_menu_give( "sm_menu_give", "0", FCVAR_CLIENTDLL, "SMenu weapons page: 0 (default) places the weapon entity where you look, 1 gives it to the player" );

ConVar sm_menu( "sm_menu", "0", FCVAR_CLIENTDLL, "Spawn Menu" );

// GMod-style hold-to-open / release-to-close spawn menu.
// Binding Q to "+smenu" opens it while held; the engine fires "-smenu"
// on release, which closes it again.  (No new command is invented here - these
// two are the menu's existing entry point, and cfg/config_hl2sb.cfg already
// binds Q to +smenu.)
static void SMenuDown( const CCommand &args )
{
	sm_menu.SetValue( 1 );
}
static ConCommand smenu_down_cmd( "+smenu", SMenuDown, "Open SMenu (hold)" );

static void SMenuUp( const CCommand &args )
{
	sm_menu.SetValue( 0 );
}
static ConCommand smenu_up_cmd( "-smenu", SMenuUp, "Close SMenu (release)" );

//-----------------------------------------------------------------------------
// HL2SB: the filter that keeps a raw class dictionary from becoming a wall of
// engine plumbing.
//
// This is a POLICY filter, not a content list: which classes exist is decided
// entirely by the two engine dictionaries at runtime.  It drops the classes a
// spawn menu can do nothing useful with - internal effect entities, pure logic
// controllers, brush entities that cannot be created by `ent_create`, invisible
// map markers - plus the classes a bare `ent_create` cannot give a model to
// (see s_pNeedsAModel below), and nothing else.  Lua-registered classes are
// NEVER filtered (see SMenu_AddClass), so addon content survives even if its
// class name starts with one of these prefixes (trigger_scripted is a SENT).
//-----------------------------------------------------------------------------
static bool SMenu_IsHiddenClass( const char *pszClass )
{
	static const char *s_pExact[] =
	{
		"worldspawn", "player", "predicted_viewmodel", "viewmodel", "soundent",
		"spotlight_end", "localname", "entityname", "reserved_spot", "world_items",
		"sky_camera", "bodyque", "entity_blocker", "water_lod_control",
		"hammer_updateignorelist", "te_tester", "lookdoorthinker", "scene_manager",
		"trigger",
	};
	static const char *s_pPrefix[] =
	{
		"_", "base", "ai_", "logic_", "math_", "path_", "filter_", "func_",
		"trigger_", "info_", "point_", "env_", "phys_", "game_", "team_",
		"vgui_", "move_", "keyframe_", "rope_", "script_", "scripted_",
		"commentary_", "player_", "tanktrain_", "instanced_", "material_",
		"handle_", "test_", "hammer_", "cycler", "sky_", "water_", "event_queue_",
	};
	static const char *s_pSuffix[] =
	{
		"_gamerules",	// spawning a second CGameRules object is never wanted
	};

	//-----------------------------------------------------------------------------
	// HL2SB: classes that CANNOT be built by a bare `ent_create <class>` because
	// their Spawn() needs a `model` keyvalue and the class itself supplies no
	// default.  A bare spawn leaves the model empty, and the engine then either
	// substitutes models/error.mdl or dies:
	//
	//   * CPhysicsProp / CDynamicProp / CPhysSphere / CBasePropDoor all derive
	//     from CBaseProp, whose Precache() reacts to an empty model by
	//     permanently setting models/error.mdl (game/server/props.cpp:258-264).
	//     The result is a purple ERROR prop, never the thing the user clicked.
	//   * CRagdollProp::Spawn() dereferences the studiohdr it got from that empty
	//     model, which is NULL - a hard crash:
	//       dumps/crash_20260914_000100_1_accessviolation.mdmp
	//       CRagdollProp::Spawn+0xB6 [physics_prop_ragdoll.cpp:167], read at 0x0
	//
	// These bare entries have no value: model-carrying props ARE offered, with a
	// model, on the existing "Props (models)" page (`prop_physics_create <model>`).
	// Everything is verified against the classes that actually register in this
	// build (server_base.vpc: props.cpp + physics_prop_ragdoll.cpp;
	// server_hl2mp.vpc: hl2/prop_thumper.cpp + hl2/prop_combine_ball.cpp):
	//
	//   HIDDEN                                        reason
	//   prop_physics / _override / _multiplayer /     CPhysicsProp     -> error.mdl
	//     _respawnable, physics_prop
	//   prop_dynamic / _override / _ornament,          CDynamicProp     -> error.mdl
	//     dynamic_prop
	//   prop_sphere                                    CPhysSphere      -> error.mdl
	//   prop_door_rotating                             CBasePropDoor    -> error.mdl
	//   prop_ragdoll / prop_ragdoll_attached /        CRagdollProp      -> CRASH
	//     physics_prop_ragdoll
	//
	//   KEPT (they DO supply a model of their own, so a bare spawn works):
	//   prop_thumper  - hl2/prop_thumper.cpp:91-99 defaults to
	//                   models/props_combine/CombineThumper002.mdl
	//   prop_combine_ball - prop_combine_ball.cpp:371 SetModel(PROP_COMBINE_BALL_MODEL)
	//
	// Lua-registered classes never reach this list (SMenu_AddClass only filters
	// non-scripted classes), so an addon SENT called prop_physics_* survives.
	//-----------------------------------------------------------------------------
	static const char *s_pNeedsAModel[] =
	{
		"physics_prop",			// physics_prop, physics_prop_ragdoll
		"prop_physics",			// prop_physics, _override, _multiplayer, _respawnable
		"prop_dynamic",			// prop_dynamic, _override, _ornament
		"dynamic_prop",			// legacy alias of prop_dynamic
		"prop_sphere",			// CPhysSphere : CPhysicsProp
		"prop_door_rotating",	// CPropDoorRotating : CBasePropDoor : CDynamicProp
		"prop_ragdoll",			// CRagdollProp, CRagdollPropAttached
	};

	for ( int i = 0; i < ARRAYSIZE( s_pNeedsAModel ); ++i )
	{
		if ( !Q_strnicmp( pszClass, s_pNeedsAModel[i], Q_strlen( s_pNeedsAModel[i] ) ) )
			return true;
	}

	for ( int i = 0; i < ARRAYSIZE( s_pExact ); ++i )
	{
		if ( !Q_stricmp( pszClass, s_pExact[i] ) )
			return true;
	}

	for ( int i = 0; i < ARRAYSIZE( s_pPrefix ); ++i )
	{
		if ( !Q_strnicmp( pszClass, s_pPrefix[i], Q_strlen( s_pPrefix[i] ) ) )
			return true;
	}

	for ( int i = 0; i < ARRAYSIZE( s_pSuffix ); ++i )
	{
		const int nNameLen = Q_strlen( pszClass );
		const int nSuffixLen = Q_strlen( s_pSuffix[i] );

		if ( nNameLen > nSuffixLen && !Q_stricmp( pszClass + nNameLen - nSuffixLen, s_pSuffix[i] ) )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// HL2SB: which page does this class belong to?
//-----------------------------------------------------------------------------
static unsigned int SMenu_Classify( const char *pszClass, const char *pszCPP, bool bScripted )
{
	const bool bScriptedWeapon = bScripted && pszCPP && Q_stristr( pszCPP, "Weapon" ) != NULL;
	const bool bPrefixWeapon = !Q_strnicmp( pszClass, "weapon_", 7 ) ||
							   !Q_strnicmp( pszClass, "item_", 5 ) ||
							   !Q_strnicmp( pszClass, "ammo_", 5 ) ||
							   !Q_strnicmp( pszClass, "gmod_", 5 );

	if ( bScriptedWeapon || bPrefixWeapon || ( pszCPP && Q_stristr( pszCPP, "Weapon" ) ) )
	{
		return SMCAT_WEAPON | SMFLAG_WEAPON | ( bScripted ? SMFLAG_LUA : 0 );
	}

	// A Lua scripted entity (SENT) - the GMod content this menu exists for.
	if ( bScripted )
		return SMCAT_LUAENT | SMFLAG_LUA;

	if ( !Q_strnicmp( pszClass, "npc_", 4 ) || !Q_strnicmp( pszClass, "monster_", 8 ) ||
		 ( pszCPP && Q_stristr( pszCPP, "NPC" ) ) )
		return SMCAT_NPC;

	if ( !Q_strnicmp( pszClass, "prop_vehicle_", 13 ) || !Q_strnicmp( pszClass, "vehicle_", 8 ) ||
		 ( pszCPP && Q_stristr( pszCPP, "Vehicle" ) ) )
		return SMCAT_VEHICLE;

	if ( !Q_strnicmp( pszClass, "prop_", 5 ) || !Q_strnicmp( pszClass, "physbox", 7 ) ||
		 ( pszCPP && ( Q_stristr( pszCPP, "Prop" ) || Q_stristr( pszCPP, "PhysBox" ) ) ) )
		return SMCAT_PROP;

	return SMCAT_ENTITY;
}

//-----------------------------------------------------------------------------
// HL2SB: pick the thumbnail for one entry.
//   1. the fork's own icon set  - materials/vgui/smenu/<class>.vmt
//   2. GMod's spawnmenu thumbs   - materials/entities/<class>.png
//   3. one generic icon per category, and then - unconditionally, for every
//      category - GMod's own guaranteed floor, icon16/plugin.png
//
// The two per-class tests are the only file system work here and they are
// CACHED, because the list is refreshed on every open (an addon may register
// content at any time) and re-probing ~700 class names would turn that into a
// visible hitch.
//-----------------------------------------------------------------------------
struct SMenuIconProbe_t
{
	unsigned char nSmenuIcon;	// materials/vgui/smenu/<class>.vmt + its texture
	unsigned char nEntityThumb;	// materials/entities/<class>.png
};

static CUtlDict< SMenuIconProbe_t, unsigned short > g_SMenuIconProbes;

static bool SMenu_FileExists( const char *pszFile )
{
	return filesystem->FileExists( pszFile );
}

//-----------------------------------------------------------------------------
// HL2SB: can the engine really bind this material NAME?
//
// "the .vmt exists" is NOT the same question, and that gap is what drew the
// purple/black cells:
//   * 73 of the 208 materials/vgui/smenu/*.vmt have no texture behind them.
//     Every one of those .vmt files uses "$basetexture vgui/smenu/<own name>"
//     (checked all 208, zero exceptions), so the texture a .vmt needs is always
//     the file sitting next to it.  Binding one of the broken ones draws the
//     ERROR material...
//   * ...and it also SHADOWED the perfectly good GMod thumbnail in
//     materials/entities/<class>.png, because the .vmt probe used to win
//     outright.  That is why so many NPC cells were broken even though the very
//     same class had a working thumbnail on disk.
//
// A name is bindable when a file with that name and an extension this build can
// load is on disk: the .vtf a sibling .vmt references, or a raw image
// (materialsystem/hl2sb_pngtexture.cpp:45-48 - .png/.jpg/.jpeg/.tga, which is
// also the only reason weapon_fists.png / weapon_default.png resolve at all).
// Verified against the complete icon set: this rule agrees exactly with
// resolving each .vmt's $basetexture - 135 usable, 73 broken.
//-----------------------------------------------------------------------------
static bool SMenu_MaterialExists( const char *pszMaterial )
{
	static const char *s_pExts[] = { ".vtf", ".png", ".jpg", ".jpeg", ".tga" };

	char szPath[MAX_PATH];
	for ( int i = 0; i < ARRAYSIZE( s_pExts ); ++i )
	{
		Q_snprintf( szPath, sizeof( szPath ), "materials/%s%s", pszMaterial, s_pExts[i] );

		if ( SMenu_FileExists( szPath ) )
			return true;
	}

	return false;
}

static void SMenu_ResolveIcon( SMenuEntry_t &entry )
{
	entry.szMaterial[0] = 0;

	unsigned short i = g_SMenuIconProbes.Find( entry.szClass );
	if ( i == g_SMenuIconProbes.InvalidIndex() )
	{
		char szPath[MAX_PATH];
		char szMaterial[MAX_PATH];

		SMenuIconProbe_t probe;

		// 1. the fork's own icon set - but only when the texture it points at
		//    is on disk too (see SMenu_MaterialExists).
		Q_snprintf( szMaterial, sizeof( szMaterial ), "vgui/smenu/%s", entry.szClass );
		probe.nSmenuIcon = SMenu_MaterialExists( szMaterial ) ? 1 : 0;

		// 2. GMod's spawnmenu thumbnail - a raw .png, so the file IS the
		//    material and the name has to keep its extension.
		Q_snprintf( szPath, sizeof( szPath ), "materials/entities/%s.png", entry.szClass );
		probe.nEntityThumb = SMenu_FileExists( szPath ) ? 1 : 0;

		i = g_SMenuIconProbes.Insert( entry.szClass, probe );
	}

	if ( g_SMenuIconProbes[i].nSmenuIcon )
	{
		Q_snprintf( entry.szMaterial, sizeof( entry.szMaterial ), "vgui/smenu/%s", entry.szClass );
		return;
	}

	if ( g_SMenuIconProbes[i].nEntityThumb )
	{
		Q_snprintf( entry.szMaterial, sizeof( entry.szMaterial ), "entities/%s.png", entry.szClass );
		return;
	}

	// 3. one generic icon per category, then - unconditionally, for EVERY
	//    category - icon16/plugin.png.  That is exactly the floor GMod's own
	//    spawnmenu uses (the log shows it loading: `image texture
	//    "icon16/plugin.png" (16x16)`), so a cell can never be left holding a
	//    material name that resolves to nothing.  Neither candidate is taken on
	//    faith: SMenu_MaterialExists is the file system check.
	const char *pszGeneric = "vgui/smenu/weapon_default";

	if ( entry.uFlags & SMCAT_NPC )
		pszGeneric = "icon16/monkey";
	else if ( entry.uFlags & SMCAT_VEHICLE )
		pszGeneric = "icon16/car";
	else if ( entry.uFlags & SMCAT_PROP )
		pszGeneric = "icon16/box";
	else if ( entry.uFlags & ( SMCAT_LUAENT | SMFLAG_LUA ) )
		pszGeneric = "icon16/plugin";	// addon/plugin content - GMod's icon

	if ( SMenu_MaterialExists( pszGeneric ) )
	{
		Q_strncpy( entry.szMaterial, pszGeneric, sizeof( entry.szMaterial ) );
		return;
	}

	if ( SMenu_MaterialExists( "icon16/plugin" ) )
		Q_strncpy( entry.szMaterial, "icon16/plugin", sizeof( entry.szMaterial ) );
}

//-----------------------------------------------------------------------------
// HL2SB: VEHICLES - why this is not just `ent_create <class>`.
//
// A vehicle needs TWO keyvalues, `model` and `vehiclescript`, and neither can be
// derived from the other nor from the class name.  Sending the bare class (what
// this menu used to do) produces `Vehicle () unable to properly initialize due
// to script error in ()!` and then the NULL-physics crash analysed in
// dumps/crash_20260913_232522.  The reasons, all checked against the real
// content and the engine source:
//
//   * scripts/vehicles/*.txt contain NO model reference.  Every file checked
//     (`Select-String '"model"|\.mdl' D:\srceng\hl2\scripts\vehicles\*.txt`):
//     zero hits.  So "the model comes from the script" is simply not true.
//   * no CPropVehicle* class defaults its own model either: the model is only
//     ever the entity's `model` keyvalue, and CBaseProp::Precache()
//     (game/server/props.cpp:258-264) substitutes models/error.mdl when it is
//     empty - which is why the broken spawns reported "prop_vehicle_apc has a
//     health specified in model 'models/error.mdl'".
//   * the model does not carry the script either: the string "vehiclescript"
//     occurs nowhere in the hl2 content VPKs (0 hits outside map BSPs).
//
// So the pair has to come from real content.  Two sources, both probed at
// menu-build time:
//
//   1. THE CONTENT PROBE - the general rule, no table, and the reason an addon
//      vehicle needs no code change here:  the script is
//      scripts/vehicles/<stem>.txt, or the single script in scripts/vehicles/
//      whose basename EXTENDS the stem (prop_vehicle_jeep -> jeep_test.txt);
//      the model is the first of models/<stem>.mdl, models/vehicles/<stem>.mdl,
//      models/props_vehicles/<stem>.mdl, models/cranes/<stem>.mdl that exists.
//      This alone resolves prop_vehicle_airboat and prop_vehicle_prisoner_pod.
//   2. A THREE-ENTRY OVERRIDE for the classes whose model does not follow the
//      stem.  Every value is copied from the engine or from real HL2 map entity
//      data, never invented:
//        prop_vehicle_jeep  -> models/buggy.mdl
//            game/server/player.cpp:6088 (CreateJeep) uses exactly that pair, and
//            7 of the maps under D:\srceng\hl2\maps place prop_vehicle_jeep with
//            models/buggy.mdl.
//        prop_vehicle_apc   -> models/combine_apc.mdl
//            16 of those maps place prop_vehicle_apc with models/combine_apc.mdl
//            (and scripts/vehicles/apc_npc.txt; the script probe picks apc.txt,
//            which is the player-driven tuning of the same vehicle and parses
//            fine).
//        prop_vehicle_crane -> models/cranes/crane_docks.mdl
//            the maps place prop_vehicle_crane with models/cranes/crane_docks.mdl
//            + scripts/vehicles/crane.txt.
//
// A class is listed ONLY when both files are really on the mounted content.
// That is what makes every base/abstract vehicle disappear instead of spawning
// an uninitialised one that crashes the server: prop_vehicle itself,
// prop_vehicle_driveable, prop_vehicle_choreo_generic, vehicle_viewcontroller,
// prop_vehicle_cannon (no map uses a model for it) and prop_vehicle_jetski (its
// Episode 2 model is not mounted, even though scripts/vehicles/jetski.txt is).
//-----------------------------------------------------------------------------
// A {class, model} pair: the model to name in the spawn command for a class.
struct SMenuClassModel_t
{
	const char	*pszClass;
	const char	*pszModel;
};

static const SMenuClassModel_t s_SMenuVehicleModels[] =
{
	{ "prop_vehicle_jeep",	"models/buggy.mdl" },
	{ "prop_vehicle_apc",	"models/combine_apc.mdl" },
	{ "prop_vehicle_crane",	"models/cranes/crane_docks.mdl" },
};

//-----------------------------------------------------------------------------
// HL2SB: classes SMenu KEEPS listed because their Spawn() does supply a default
// model - but only INSIDE Spawn, after the engine has already precached an empty
// model (CC_Ent_Create -> Precache, then DispatchSpawn -> Spawn -> Precache
// again).  The user therefore saw
//     Attempting to precache model, but model name is NULL
// for an entry the menu offers.  Naming the model in the command fixes that and
// changes nothing else: it is the very value the class would have chosen.
//
//   prop_thumper      -> models/props_combine/CombineThumper002.mdl
//                        THUMPER_MODEL_NAME, hl2/prop_thumper.cpp:26, used at :94
//   prop_combine_ball -> models/effects/combineball.mdl
//                        PROP_COMBINE_BALL_MODEL, hl2/prop_combine_ball.cpp:33,
//                        SetModel at :371
//
// Both models are present in the mounted content (checked in the HL2 VPK trees),
// so giving them here cannot turn a working entry into a broken one.
//-----------------------------------------------------------------------------
static const SMenuClassModel_t s_SMenuSpawnModels[] =
{
	{ "prop_thumper",		"models/props_combine/CombineThumper002.mdl" },
	{ "prop_combine_ball",	"models/effects/combineball.mdl" },
};

static const char *SMenu_FindSpawnModel( const char *pszClass )
{
	for ( int i = 0; i < ARRAYSIZE( s_SMenuSpawnModels ); ++i )
	{
		if ( !Q_stricmp( s_SMenuSpawnModels[i].pszClass, pszClass ) )
			return s_SMenuSpawnModels[i].pszModel;
	}

	return NULL;
}

// models/<stem>.mdl plus the three subdirectories HL2 keeps vehicle models in
static const char *s_pSMenuVehicleModelPaths[] =
{
	"models/%s.mdl",
	"models/vehicles/%s.mdl",
	"models/props_vehicles/%s.mdl",
	"models/cranes/%s.mdl",
};

static bool SMenu_IsVehicleClass( const char *pszClass )
{
	return !Q_strnicmp( pszClass, "prop_vehicle_", 13 ) ||
		   !Q_strnicmp( pszClass, "vehicle_", 8 );
}

// "prop_vehicle_jeep" -> "jeep";  "vehicle_viewcontroller" -> "viewcontroller";
// "prop_vehicle" -> "" (abstract, there is nothing to probe for)
static const char *SMenu_VehicleStem( const char *pszClass )
{
	if ( !Q_strnicmp( pszClass, "prop_vehicle_", 13 ) )
		return pszClass + 13;

	if ( !Q_strnicmp( pszClass, "vehicle_", 8 ) )
		return pszClass + 8;

	return NULL;
}

// Every script stem in scripts/vehicles/, read once.  Same caching contract as
// the icon probes: the menu list is rebuilt on every open, and this is the only
// directory scan in it.
static CUtlVector< CUtlString > g_SMenuVehicleScriptStems;
static bool g_bSMenuVehicleScriptsScanned = false;

static void SMenu_ScanVehicleScripts( void )
{
	if ( g_bSMenuVehicleScriptsScanned )
		return;

	g_bSMenuVehicleScriptsScanned = true;

	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *pszFile = filesystem->FindFirstEx( "scripts/vehicles/*.txt", "GAME", &hFind );

	for ( ; pszFile && pszFile[0]; pszFile = filesystem->FindNext( hFind ) )
	{
		if ( pszFile[0] == '.' )
			continue;

		char szStem[MAX_PATH];
		Q_strncpy( szStem, pszFile, sizeof( szStem ) );

		// strip any path the find may report, then the extension
		char *pSlash = strrchr( szStem, '/' );
		if ( pSlash )
			Q_memmove( szStem, pSlash + 1, Q_strlen( pSlash + 1 ) + 1 );

		pSlash = strrchr( szStem, '\\' );
		if ( pSlash )
			Q_memmove( szStem, pSlash + 1, Q_strlen( pSlash + 1 ) + 1 );

		char *pExt = strrchr( szStem, '.' );
		if ( pExt )
			*pExt = 0;

		if ( szStem[0] )
			g_SMenuVehicleScriptStems.AddToTail( CUtlString( szStem ) );
	}

	if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( hFind );
}

static bool SMenu_FindVehicleScript( const char *pszStem, char *pOut, int nOutLen )
{
	char szScript[MAX_PATH];

	// the plain convention first: scripts/vehicles/<stem>.txt
	Q_snprintf( szScript, sizeof( szScript ), "scripts/vehicles/%s.txt", pszStem );

	if ( SMenu_FileExists( szScript ) )
	{
		Q_strncpy( pOut, szScript, nOutLen );
		return true;
	}

	// Otherwise accept the script whose stem EXTENDS the class stem, but only
	// when exactly ONE does - prop_vehicle_jeep -> scripts/vehicles/jeep_test.txt,
	// the pair player.cpp:6091 and the real maps both use.  Two candidates is
	// ambiguous, and guessing is exactly what this function exists to avoid.
	SMenu_ScanVehicleScripts();

	const int nStemLen = Q_strlen( pszStem );
	const CUtlString *pMatch = NULL;
	int nMatching = 0;

	for ( int i = 0; i < g_SMenuVehicleScriptStems.Count(); ++i )
	{
		const char *pszCandidate = g_SMenuVehicleScriptStems[i].Get();

		if ( Q_strnicmp( pszCandidate, pszStem, nStemLen ) != 0 )
			continue;

		if ( pszCandidate[nStemLen] == 0 )
			continue;		// the exact name, already tried above

		pMatch = &g_SMenuVehicleScriptStems[i];
		nMatching++;
	}

	if ( nMatching != 1 )
		return false;

	Q_snprintf( pOut, nOutLen, "scripts/vehicles/%s.txt", pMatch->Get() );
	return true;
}

static bool SMenu_FindVehicleModel( const char *pszClass, const char *pszStem, char *pOut, int nOutLen )
{
	for ( int i = 0; i < ARRAYSIZE( s_SMenuVehicleModels ); ++i )
	{
		if ( !Q_stricmp( s_SMenuVehicleModels[i].pszClass, pszClass ) )
		{
			Q_strncpy( pOut, s_SMenuVehicleModels[i].pszModel, nOutLen );
			return true;
		}
	}

	for ( int i = 0; i < ARRAYSIZE( s_pSMenuVehicleModelPaths ); ++i )
	{
		char szModel[MAX_PATH];
		Q_snprintf( szModel, sizeof( szModel ), s_pSMenuVehicleModelPaths[i], pszStem );

		if ( SMenu_FileExists( szModel ) )
		{
			Q_strncpy( pOut, szModel, nOutLen );
			return true;
		}
	}

	return false;
}

// Fills pOut with the complete spawn line for a vehicle class, or returns false
// when this build cannot produce a real vehicle of that class - in which case
// the class must not be listed at all.
static bool SMenu_BuildVehicleCommand( const char *pszClass, char *pOut, int nOutLen )
{
	const char *pszStem = SMenu_VehicleStem( pszClass );

	if ( !pszStem || !pszStem[0] )
		return false;		// prop_vehicle / vehicle_ themselves: abstract

	char szModel[MAX_PATH];
	char szScript[MAX_PATH];

	if ( !SMenu_FindVehicleModel( pszClass, pszStem, szModel, sizeof( szModel ) ) )
		return false;

	if ( !SMenu_FindVehicleScript( pszStem, szScript, sizeof( szScript ) ) )
		return false;

	// Belt and braces (both probes already asked): the entity needs BOTH files,
	// or it spawns as models/error.mdl with a NULL physics controller and then
	// dereferences it from Think().
	if ( !SMenu_FileExists( szModel ) || !SMenu_FileExists( szScript ) )
		return false;

	Q_snprintf( pOut, nOutLen, "ent_create %s model %s vehiclescript %s",
				pszClass, szModel, szScript );
	return true;
}

//-----------------------------------------------------------------------------
// HL2SB: add one class to the list (deduplicated, filtered, classified).
//-----------------------------------------------------------------------------
static int SMenu_FindEntry( const char *pszClass )
{
	for ( int i = 0; i < g_SMenuEntries.Count(); ++i )
	{
		if ( !Q_stricmp( g_SMenuEntries[i].szClass, pszClass ) )
			return i;
	}

	return -1;
}

static void SMenu_AddClass( const char *pszClass, const char *pszCPP, bool bScripted )
{
	if ( !pszClass || !pszClass[0] )
		return;

	// Lua content is never filtered: an addon's class name may start with a
	// prefix that is plumbing for a stock class (trigger_scripted is a SENT).
	if ( !bScripted && SMenu_IsHiddenClass( pszClass ) )
		return;

	// HL2SB: vehicles get their spawn line (model + vehiclescript) here, and a
	// vehicle class this build cannot build properly is NOT listed at all - see
	// the long note above s_SMenuVehicleModels.  Only stock HL2 classes are
	// touched: a Lua SENT that happens to be called prop_vehicle_* is left
	// exactly as it was, because Lua content is never filtered.
	char szFixedCmd[SMENU_FIXEDCMD_LEN];
	szFixedCmd[0] = 0;

	if ( !bScripted && SMenu_IsVehicleClass( pszClass ) )
	{
		if ( !SMenu_BuildVehicleCommand( pszClass, szFixedCmd, sizeof( szFixedCmd ) ) )
			return;
	}

	// HL2SB: a kept class that only picks its own default model inside Spawn()
	// gets that model named in the command, so the engine precaches a real model
	// instead of logging "model name is NULL" for an entry the menu offers
	// (see s_SMenuSpawnModels).
	if ( !bScripted && !szFixedCmd[0] )
	{
		const char *pszDefaultModel = SMenu_FindSpawnModel( pszClass );

		if ( pszDefaultModel )
			Q_snprintf( szFixedCmd, sizeof( szFixedCmd ), "ent_create %s model %s", pszClass, pszDefaultModel );
	}

	const unsigned int uFlags = SMenu_Classify( pszClass, pszCPP, bScripted );

	const int iExisting = SMenu_FindEntry( pszClass );
	if ( iExisting >= 0 )
	{
		g_SMenuEntries[iExisting].uFlags |= uFlags;
		return;
	}

	SMenuEntry_t entry;
	Q_memset( &entry, 0, sizeof( entry ) );
	entry.uFlags = uFlags;
	Q_strncpy( entry.szClass, pszClass, sizeof( entry.szClass ) );
	Q_strncpy( entry.szFixedCmd, szFixedCmd, sizeof( entry.szFixedCmd ) );
	SMenu_ResolveIcon( entry );

	g_SMenuEntries.AddToTail( entry );
}

//-----------------------------------------------------------------------------
// HL2SB: GMod-style runtime registrations (the Lua <-> SMenu hookup).
//
// Direction chosen: (a) - C++ SMenu PULLS the Lua registries with the Lua C API
// every time the list is (re)built.  It is the least invasive option: nothing
// in the Lua layer has to know SMenu exists, so it cannot desync, and it also
// picks up registrations that happen long after level init.  The existing
// engine-side push path is used as well (the Lua loaders call
// RegisterScriptedWeapon / RegisterScriptedEntity, which land in the client's
// class map - source 1 of SMenu_BuildEntries), so the result is a hybrid: the
// engine registration decides WHAT IS SPAWNABLE, the Lua lists decide how it is
// CATEGORISED.
//
// Which keys this fork actually fills (checked across lua/ gamemodes/ addons/):
//   list.Set( "Weapon", ... )            - lua/includes/modules/weapons.lua:58
//   list.Set( "SpawnableEntities", ... ) - lua/includes/modules/scripted_ents.lua:123
// Nothing registers "Vehicles" yet (GMod's vehicle tab is txt-driven; this fork
// has no such list), but it is read anyway so an addon that uses GMod's
// convention lands in the Vehicles page.  "Weapons"/"Entities" are accepted as
// spelling variants.
//
// A Lua list is only a table of STRINGS - nothing guarantees the class can be
// built.  Every name read here is therefore required to already be in the
// engine enumeration (client class map or the server's published dictionary);
// otherwise it is dropped and counted, so the menu's acceptance test holds:
// every entry can really be spawned by the command the entry runs.
//
// list.Get() is a Lua call, so it goes through luasrc_pcall, which logs the
// error AND pops it, leaving the stack clean on failure
// (game/shared/lua/luamanager.cpp:1051) - the engine enumeration survives.
//-----------------------------------------------------------------------------
struct SMenuLuaList_t
{
	const char	*pszListId;
	unsigned int uForceCat;
};

static const SMenuLuaList_t s_SMenuLuaLists[] =
{
	{ "SpawnableEntities",	0 },
	{ "Entities",			0 },
	{ "Vehicles",			SMCAT_VEHICLE },
	{ "Weapon",				SMCAT_WEAPON | SMFLAG_WEAPON },
	{ "Weapons",			SMCAT_WEAPON | SMFLAG_WEAPON },
};

static void SMenu_MergeLuaList( const char *pszListId, unsigned int uForceCat )
{
	if ( !L )
		return;

	lua_getglobal( L, "list" );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}

	lua_getfield( L, -1, "Get" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return;
	}

	lua_remove( L, -2 );				// [Get]
	lua_pushstring( L, pszListId );		// [Get][listid]

	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
		return;							// the error object is already popped

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}

	int nKnown = 0, nRejected = 0;

	lua_pushnil( L );
	while ( lua_next( L, -2 ) != 0 )
	{
		// [table][key][value] - keyed lists map a class name to an info table,
		// list.Add()-style lists hold the name as the value.
		const char *pszName = NULL;
		if ( lua_type( L, -2 ) == LUA_TSTRING )
			pszName = lua_tostring( L, -2 );
		else if ( lua_type( L, -1 ) == LUA_TSTRING )
			pszName = lua_tostring( L, -1 );

		if ( pszName && pszName[0] )
		{
			// No Lua call may happen inside lua_next(): SMenu_FindEntry only
			// touches g_SMenuEntries.
			const int iExisting = SMenu_FindEntry( pszName );

			if ( iExisting < 0 )
			{
				nRejected++;
			}
			else if ( uForceCat )
			{
				// Known-spawnable: let the list re-categorise it.  GMod's
				// Vehicles list is a list OF VEHICLES, so a name registered
				// there belongs in the Vehicles page even when its class name
				// looks like nothing in particular.
				unsigned int &uFlags = g_SMenuEntries[iExisting].uFlags;
				uFlags = ( uFlags & ~( SMCAT_WEAPON | SMCAT_LUAENT | SMCAT_ENTITY | SMCAT_NPC | SMCAT_PROP | SMCAT_VEHICLE ) ) | uForceCat;
				nKnown++;
			}
		}

		lua_pop( L, 1 );	// pop the value, keep the key for the next step
	}

	lua_pop( L, 1 );		// the result table

	// Only worth a line when a registered name has no spawnable class behind it
	// (the aggregate counts are logged by SMenu_BuildEntries).
	if ( nRejected > 0 )
		Msg( "[HL2SB] SMenu: list.Get(\"%s\") - %d known class(es) recategorised, %d name(s) without a spawnable class dropped\n",
			 pszListId, nKnown, nRejected );
}

static int __cdecl SMenu_SortEntries( const SMenuEntry_t *pLeft, const SMenuEntry_t *pRight )
{
	return Q_stricmp( pLeft->szClass, pRight->szClass );
}

// Cheap change detector: the panels are only torn down and rebuilt when the
// entry set really changed (this is what makes a refresh on every open safe).
static unsigned int SMenu_HashEntries( void )
{
	unsigned int uHash = 2166136261u;

	for ( int i = 0; i < g_SMenuEntries.Count(); ++i )
	{
		for ( const char *p = g_SMenuEntries[i].szClass; *p; ++p )
			uHash = ( uHash ^ (unsigned char)*p ) * 16777619u;

		uHash = ( uHash ^ g_SMenuEntries[i].uFlags ) * 16777619u;
	}

	return uHash;
}

//-----------------------------------------------------------------------------
// HL2SB: build the dynamic list.  Never reads addons/menu/entitylist.txt.
//
// Sources:
//   1. the client's own class dictionary (game/client/classmap.cpp) - it is the
//      only place that knows which classes came from LUA (scripted flag), so it
//      runs first and the Lua SWEP/SENT pages stay correct;
//   2. the "SMenuEntityList" network string table published by the server
//      (game/server/gameinterface.cpp: SMenu_PublishEntityList) - this is the
//      server's entity factory dictionary, and it is where the NPCs, props and
//      VEHICLES come from, because those are server-only classes in HL2MP;
//   3. GMod-style runtime registries, pulled from the Lua state (see above).
//
// Cheap enough to repeat on every menu open: with the icon probe cache it is
// string work only.
//-----------------------------------------------------------------------------
static void SMenu_BuildEntries( void )
{
	g_SMenuEntries.RemoveAll();

	// 1. client class dictionary
	const int nClasses = ClassMap_GetEntryCount();
	for ( int i = 0; i < nClasses; ++i )
	{
		SMenu_AddClass( ClassMap_GetEntryName( i ), ClassMap_GetEntryCPPName( i ), ClassMap_IsEntryScripted( i ) );
	}

	// 2. the server's entity factory dictionary
	INetworkStringTable *pTable = networkstringtable ? networkstringtable->FindTable( SMENU_ENTITYLIST_TABLE ) : NULL;
	if ( pTable )
	{
		const int nStrings = pTable->GetNumStrings();
		for ( int i = 0; i < nStrings; ++i )
		{
			const char *pszClass = pTable->GetString( i );
			if ( pszClass && pszClass[0] )
				SMenu_AddClass( pszClass, NULL, false );
		}
	}

	// 3. GMod runtime registries (validated against 1+2 inside)
	for ( int i = 0; i < ARRAYSIZE( s_SMenuLuaLists ); ++i )
		SMenu_MergeLuaList( s_SMenuLuaLists[i].pszListId, s_SMenuLuaLists[i].uForceCat );

	g_SMenuEntries.Sort( SMenu_SortEntries );

	int nWeapons = 0, nLua = 0, nNpc = 0, nVeh = 0;
	for ( int i = 0; i < g_SMenuEntries.Count(); ++i )
	{
		if ( g_SMenuEntries[i].uFlags & SMCAT_WEAPON ) nWeapons++;
		if ( g_SMenuEntries[i].uFlags & SMFLAG_LUA ) nLua++;
		if ( g_SMenuEntries[i].uFlags & SMCAT_NPC ) nNpc++;
		if ( g_SMenuEntries[i].uFlags & SMCAT_VEHICLE ) nVeh++;
	}

	// Regression tripwire: this line says exactly which source produced what.
	Msg( "[HL2SB] SMenu: %d entries (%d weapons, %d NPCs, %d vehicles, %d Lua) - %d from the client class map, %d published by the server\n",
		 g_SMenuEntries.Count(), nWeapons, nNpc, nVeh, nLua, nClasses,
		 pTable ? pTable->GetNumStrings() : 0 );
}

//-----------------------------------------------------------------------------
// HL2SB: one icon-grid cell - thumbnail + class name, click = spawn.
//
// Not vgui::ImageButton/ImagePanel: scheme()->GetImage() prepends "vgui/" to
// every name (vgui2/src/Scheme.cpp:1260), so it can only reach
// materials/vgui/... while GMod's thumbnails live in materials/entities/.
// Drawing through ISurface::DrawSetTextureFile addresses materials/ directly
// and lets the entry show its class name as well.
//-----------------------------------------------------------------------------
class CSMIconButton : public vgui::Panel
{
	typedef vgui::Panel BaseClass;
public:
	CSMIconButton( vgui::Panel *pParent, const SMenuEntry_t &entry ) : BaseClass( pParent, "SMenuIcon" )
	{
		Q_strncpy( m_szClass, entry.szClass, sizeof( m_szClass ) );
		Q_strncpy( m_szFixedCmd, entry.szFixedCmd, sizeof( m_szFixedCmd ) );
		Q_strncpy( m_szMaterial, entry.szMaterial, sizeof( m_szMaterial ) );
		m_bWeapon = ( entry.uFlags & SMFLAG_WEAPON ) != 0;
		m_bHover = false;
		m_bBound = false;
		m_nTexture = surface()->CreateNewTextureID();

		SetSize( 72, 76 );
		SetPaintBackgroundEnabled( false );
		SetMouseInputEnabled( true );

		m_pLabel = new vgui::Label( this, "Name", m_szClass );
		m_pLabel->SetContentAlignment( vgui::Label::a_center );
		m_pLabel->SetMouseInputEnabled( false );
		m_pLabel->SetBounds( 0, 58, 72, 18 );
	}

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		if ( m_pLabel )
			m_pLabel->SetFont( pScheme->GetFont( "DefaultVerySmall", true ) );
	}

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		int w, h;
		GetSize( w, h );

		if ( m_pLabel )
			m_pLabel->SetBounds( 0, h - 18, w, 18 );
	}

	virtual void Paint()
	{
		int w, h;
		GetSize( w, h );

		if ( m_bHover )
		{
			surface()->DrawSetColor( 255, 255, 255, 28 );
			surface()->DrawFilledRect( 0, 0, w, h );
		}

		if ( m_szMaterial[0] && m_nTexture != -1 )
		{
			// HL2SB: bind lazily, on first paint.  Drawing hundreds of
			// thumbnails eagerly would decode every one of them the moment the
			// menu is built, including the pages nobody opened.
			if ( !m_bBound )
			{
				surface()->DrawSetTextureFile( m_nTexture, m_szMaterial, true, false );
				m_bBound = true;
			}

			const int nIcon = 56;
			int x = ( w - nIcon ) / 2;
			surface()->DrawSetColor( 255, 255, 255, 255 );
			surface()->DrawSetTexture( m_nTexture );
			surface()->DrawTexturedRect( x, 2, x + nIcon, 2 + nIcon );
		}

		BaseClass::Paint();
	}

	virtual void OnCursorEntered()
	{
		m_bHover = true;
		Repaint();
	}

	virtual void OnCursorExited()
	{
		m_bHover = false;
		Repaint();
	}

	virtual void OnMouseReleased( vgui::MouseCode code )
	{
		if ( code != MOUSE_LEFT )
			return;

		char szCommand[256];

		if ( m_szFixedCmd[0] )
		{
			Q_strncpy( szCommand, m_szFixedCmd, sizeof( szCommand ) );
		}
		else
		{
			const bool bGive = m_bWeapon && sm_menu_give.GetBool();
			Q_snprintf( szCommand, sizeof( szCommand ), "%s %s", bGive ? "give" : "ent_create", m_szClass );
		}

		engine->ClientCmd( szCommand );
	}

private:
	vgui::Label	*m_pLabel;
	char		m_szClass[64];
	char		m_szFixedCmd[SMENU_FIXEDCMD_LEN];
	char		m_szMaterial[128];
	int			m_nTexture;
	bool		m_bBound;
	bool		m_bHover;
	bool		m_bWeapon;
};

//-----------------------------------------------------------------------------
// HL2SB: one page of icon cells.  PanelListPanel already lays its children out
// as a wrapping grid and owns the scrollbar, so the only thing this class does
// is tell it how many columns fit in the current width.
//-----------------------------------------------------------------------------
#define SMENU_CELL_W	76
#define SMENU_CELL_H	80

class CSMList : public vgui::PanelListPanel
{
	typedef vgui::PanelListPanel BaseClass;
public:
	CSMList( vgui::Panel *pParent, const char *pName ) : BaseClass( pParent, pName )
	{
		m_nColumns = 0;
		SetFirstColumnWidth( 0 );
		SetNumColumns( 1 );
		SetVerticalBufferPixels( 2 );
	}

	void AddEntry( const SMenuEntry_t &entry )
	{
		CSMIconButton *pButton = new CSMIconButton( this, entry );
		pButton->SetSize( SMENU_CELL_W, SMENU_CELL_H );
		AddItem( NULL, pButton );
	}

	virtual void PerformLayout()
	{
		int w = 0, h = 0;
		GetSize( w, h );
		NOTE_UNUSED( h );

		int nColumns = ( w - 30 ) / SMENU_CELL_W;
		if ( nColumns < 1 )
			nColumns = 1;

		if ( nColumns != m_nColumns )
		{
			m_nColumns = nColumns;
			SetNumColumns( nColumns );
		}

		BaseClass::PerformLayout();
	}

private:
	int m_nColumns;
};

//-----------------------------------------------------------------------------
// HL2SB: the left-hand category list.  A PanelListPanel of buttons sends its
// "Command" message to the list itself (Panel::OnCommand is part of the base
// message map, so it does NOT bubble to us), hence the forwarder.
//-----------------------------------------------------------------------------
class CSMenu;

class CSMCatList : public vgui::PanelListPanel
{
	typedef vgui::PanelListPanel BaseClass;
public:
	CSMCatList( vgui::Panel *pParent, const char *pName ) : BaseClass( pParent, pName )
	{
		m_pOwner = NULL;
		SetFirstColumnWidth( 0 );
		SetNumColumns( 1 );
		SetVerticalBufferPixels( 2 );
	}

	void SetOwner( CSMenu *pOwner ) { m_pOwner = pOwner; }

	virtual void OnCommand( const char *command );

private:
	CSMenu	*m_pOwner;
};

//-----------------------------------------------------------------------------
// Purpose: the menu itself - category tree on the left, icon grid on the right
//-----------------------------------------------------------------------------
class CSMenu : public vgui::Frame
{
	typedef vgui::Frame BaseClass;
public:
	CSMenu( vgui::VPANEL *parent, const char *panelName ) : BaseClass( NULL, "SMenu" )
	{
		SetTitle( "SMenu", true );
		SetSize( 820, 620 );
		SetMinimizeButtonVisible( false );
		SetMaximizeButtonVisible( false );

		m_szBuiltLevel[0] = 0;
		m_uBuiltHash = 0;
		m_bBuiltOnce = false;
		m_nCurrentCat = 0;

		m_pTree = new CSMCatList( this, "CategoryList" );
		m_pTree->SetOwner( this );

		m_pGridHost = new vgui::Panel( this, "GridHost" );

		// One page per tree node; the tree buttons themselves are (re)built by
		// RebuildTree(), which skips every category that came up empty - an
		// empty category must not reserve a dead tab (GMod hides those too).
		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			m_bCatVisible[i] = false;

			char szName[32];
			Q_snprintf( szName, sizeof( szName ), "Page%d", i );
			m_pPages[i] = new CSMList( m_pGridHost, szName );
			m_pPages[i]->SetVisible( false );
		}

		vgui::ivgui()->AddTickSignal( GetVPanel(), 100 );

		SetMoveable( true );
		SetVisible( false );
		SetSizeable( true );

		// GMod-style: the spawn menu must NOT capture keyboard input, otherwise
		// vgui treats the popup as the key focus and swallows WASD before the
		// engine can fire +forward/+back/+moveleft/+moveright, so the player
		// cannot walk while the menu is open.  Mouse input stays enabled so the
		// tree and the icons still respond to clicks.
		SetKeyBoardInputEnabled( false );
		SetMouseInputEnabled( true );
	}

	~CSMenu()
	{
	}

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		int x = 0, y = 0, w = 0, h = 0;
		GetClientArea( x, y, w, h );

		if ( w <= 0 || h <= 0 )
			return;

		int nTreeW = w / 5;
		if ( nTreeW < 150 )
			nTreeW = 150;
		if ( nTreeW > 260 )
			nTreeW = 260;

		m_pTree->SetBounds( x + 4, y + 4, nTreeW, h - 8 );
		m_pGridHost->SetBounds( x + nTreeW + 10, y + 4, w - nTreeW - 14, h - 8 );

		// The visible page fills the host; the others keep their stale bounds
		// and are simply not drawn.
		if ( m_pPages[m_nCurrentCat] )
			m_pPages[m_nCurrentCat]->SetBounds( 0, 0, m_pGridHost->GetWide(), m_pGridHost->GetTall() );
	}

	virtual void OnCommand( const char *command )
	{
		BaseClass::OnCommand( command );

		if ( !Q_stricmp( command, "Close" ) )
		{
			sm_menu.SetValue( 0 );
			return;
		}

		int nCat = -1;
		if ( !Q_strnicmp( command, "smcat ", 6 ) )
			nCat = atoi( command + 6 );

		if ( nCat >= 0 && nCat < (int)SMENU_CAT_COUNT )
		{
			ShowCategory( nCat );
		}
	}

	virtual void OnTick()
	{
		BaseClass::OnTick();

		const bool bVisible = sm_menu.GetBool() != 0;
		const bool bWasVisible = IsVisible();

		if ( bVisible )
		{
			// REFRESH TIMING: on every open (the hidden -> visible transition)
			// and on a level change - never per frame.  Opening is the cheap
			// place to re-read everything, because an addon can register content
			// long after level init; RebuildIfNeeded() re-reads the sources each
			// time but only rebuilds the panels when the entry set really
			// changed, so the live panels are never mutated in place while the
			// menu is open.
			const char *pszLevel = engine->GetLevelName();
			const bool bLevelChanged = pszLevel && Q_stricmp( m_szBuiltLevel, pszLevel ) != 0;

			if ( !bWasVisible || bLevelChanged )
			{
				RebuildIfNeeded();
				ShowCategory( m_nCurrentCat );
			}
		}

		SetVisible( bVisible );
	}

private:
	// Re-reads every source and, only when the resulting entry set changed,
	// rebuilds the pages and the category tree.  Returns true when it rebuilt.
	bool RebuildIfNeeded( void )
	{
		SMenu_BuildEntries();

		const char *pszLevel = engine->GetLevelName();
		if ( !pszLevel )
			pszLevel = "";

		const unsigned int uHash = SMenu_HashEntries();
		const bool bLevelChanged = Q_stricmp( m_szBuiltLevel, pszLevel ) != 0;

		if ( m_bBuiltOnce && uHash == m_uBuiltHash && !bLevelChanged )
			return false;		// nothing changed: leave the live panels alone

		Q_strncpy( m_szBuiltLevel, pszLevel, sizeof( m_szBuiltLevel ) );
		m_uBuiltHash = uHash;

		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			m_pPages[i]->DeleteAllItems();

			if ( s_SMenuCats[i].bModelPage )
				continue;

			for ( int j = 0; j < g_SMenuEntries.Count(); ++j )
			{
				const SMenuEntry_t &entry = g_SMenuEntries[j];

				if ( ( entry.uFlags & s_SMenuCats[i].uMask ) == 0 )
					continue;

				if ( s_SMenuCats[i].uNotMask && ( entry.uFlags & s_SMenuCats[i].uNotMask ) )
					continue;

				m_pPages[i]->AddEntry( entry );
			}
		}

		// models/props_* - the only page whose content is files, not classes, so
		// it is rescanned on a level change (and the first time) only: walking
		// every props_*.mdl is far too expensive for a per-open refresh.
		if ( !m_bBuiltOnce || bLevelChanged )
		{
			m_pPages[SMENU_CAT_COUNT - 1]->DeleteAllItems();
			SMenu_BuildModelPage( m_pPages[SMENU_CAT_COUNT - 1] );
		}

		m_bBuiltOnce = true;

		RebuildTree();

		return true;
	}

	//-----------------------------------------------------------------------------
	// HL2SB: one button per non-empty category.  A category with nothing in it
	// (Vehicles on a server that does not publish its entity list, for instance)
	// is left out of the tree instead of showing a dead tab.
	//-----------------------------------------------------------------------------
	void RebuildTree( void )
	{
		m_pTree->DeleteAllItems();

		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
			m_bCatVisible[i] = false;

		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			if ( m_pPages[i]->GetItemCount() <= 0 )
				continue;

			m_bCatVisible[i] = true;

			char szName[32];
			Q_snprintf( szName, sizeof( szName ), "Cat%d", i );

			char szCmd[32];
			Q_snprintf( szCmd, sizeof( szCmd ), "smcat %d", i );

			vgui::Button *pButton = new vgui::Button( m_pTree, szName, s_SMenuCats[i].pszTitle, m_pTree, szCmd );
			pButton->SetContentAlignment( vgui::Label::a_west );
			pButton->SetSize( 180, 22 );
			m_pTree->AddItem( NULL, pButton );
		}
	}

	//-----------------------------------------------------------------------------
	// HL2SB: props from models/props_* - unchanged from the original menu, and
	// still icon-gated: this page lists model FILES, so relaxing the icon rule
	// the way the class pages do would turn it into thousands of generic boxes.
	//-----------------------------------------------------------------------------
	void SMenu_BuildModelPage( CSMList *pPage )
	{
		FileFindHandle_t fh;
		for ( const char *pDir = filesystem->FindFirstEx( "models/*", "GAME", &fh ); pDir && *pDir; pDir = filesystem->FindNext( fh ) )
		{
			if ( Q_strncmp( pDir, "props_", Q_strlen( "props_" ) ) != 0 )
				continue;

			if ( !filesystem->FindIsDirectory( fh ) )
				continue;

			char szFolder[MAX_PATH];
			Q_FileBase( pDir, szFolder, sizeof( szFolder ) );

			char szSearch[MAX_PATH];
			Q_snprintf( szSearch, sizeof( szSearch ), "models/%s/*.mdl", szFolder );

			FileFindHandle_t mh;
			for ( const char *pModel = g_pFullFileSystem->FindFirst( szSearch, &mh ); pModel; pModel = g_pFullFileSystem->FindNext( mh ) )
			{
				if ( pModel[0] == '.' )
					continue;

				char szExt[10];
				Q_ExtractFileExtension( pModel, szExt, sizeof( szExt ) );
				if ( Q_stricmp( szExt, "mdl" ) )
					continue;

				char szBase[MAX_PATH];
				Q_FileBase( pModel, szBase, sizeof( szBase ) );

				char szModel[MAX_PATH];
				Q_snprintf( szModel, sizeof( szModel ), "%s/%s", szFolder, szBase );

				// HL2SB: same rule as the class pages - the .vmt alone is not
				// enough, its texture has to be on disk too, or the cell draws
				// the ERROR material (see SMenu_MaterialExists).
				char szMaterial[MAX_PATH];
				Q_snprintf( szMaterial, sizeof( szMaterial ), "vgui/smenu/models/%s", szModel );
				if ( !SMenu_MaterialExists( szMaterial ) )
					continue;

				SMenuEntry_t entry;
				Q_memset( &entry, 0, sizeof( entry ) );
				entry.uFlags = SMCAT_PROP;
				Q_strncpy( entry.szClass, szModel, sizeof( entry.szClass ) );
				Q_snprintf( entry.szFixedCmd, sizeof( entry.szFixedCmd ), "prop_physics_create %s", szModel );
				Q_snprintf( entry.szMaterial, sizeof( entry.szMaterial ), "vgui/smenu/models/%s", szModel );

				pPage->AddEntry( entry );
			}
			g_pFullFileSystem->FindClose( mh );
		}
		g_pFullFileSystem->FindClose( fh );
	}

	void ShowCategory( int nCat )
	{
		if ( nCat < 0 || nCat >= (int)SMENU_CAT_COUNT || !m_bCatVisible[nCat] )
		{
			// The requested page is empty or gone (the tree hides those), so
			// land on the first page that does have entries.
			nCat = -1;
			for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
			{
				if ( m_bCatVisible[i] )
				{
					nCat = i;
					break;
				}
			}

			if ( nCat < 0 )
				return;
		}

		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
			m_pPages[i]->SetVisible( i == nCat );

		m_nCurrentCat = nCat;

		if ( m_pPages[nCat] )
		{
			m_pPages[nCat]->SetBounds( 0, 0, m_pGridHost->GetWide(), m_pGridHost->GetTall() );
			m_pPages[nCat]->InvalidateLayout( true );
		}
	}

	CSMCatList	*m_pTree;
	vgui::Panel	*m_pGridHost;
	CSMList		*m_pPages[SMENU_CAT_COUNT];
	bool		m_bCatVisible[SMENU_CAT_COUNT];
	int			m_nCurrentCat;
	char		m_szBuiltLevel[256];
	unsigned int m_uBuiltHash;
	bool		m_bBuiltOnce;
};

void CSMCatList::OnCommand( const char *command )
{
	if ( m_pOwner )
		m_pOwner->OnCommand( command );
}

class CSMPanelInterface : public SMPanel
{
private:
	CSMenu *SMPanel;
public:
	CSMPanelInterface()
	{
		SMPanel = NULL;
	}
	void Create(vgui::VPANEL parent)
	{
		SMPanel = new CSMenu(&parent, "SMenu");
	}
	void Destroy()
	{
		if (SMPanel)
		{
			SMPanel->SetParent((vgui::Panel *)NULL);
			delete SMPanel;
		}
	}
	void Activate(void)
	{
		if (SMPanel)
		{
			SMPanel->Activate();
		}
	}
};
static CSMPanelInterface g_SMPanel;
SMPanel* smenu = (SMPanel*)&g_SMPanel;
