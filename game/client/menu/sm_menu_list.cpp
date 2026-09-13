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
// HL2SB layout (GMod's spawnmenu structure - see SMenu_TODO.md item 7):
//
//   +-----------------------------------------------------------------+
//   | [Weapons][Entities][NPCs][Props][Vehicles][Models]              | top tabs
//   +----------------+------------------------------------------------+
//   | nyangun        |                                                |
//   | HL2SB Lua      |   icon grid of the selected source             |
//   | Engine         |                                                |
//   +----------------+------------------------------------------------+
//
// The top tabs answer WHAT a thing is: the same category classification this
// menu always had (the old "Lua SWEPs" / "Stock weapons" split is gone from the
// tabs because that IS the source axis now).  The LEFT sidebar answers WHICH
// SOURCE an entry came from - the addon that shipped the Lua script, the tree's
// own lua/, or the honest "Engine" bucket for the classes server.dll registers
// with no game to attribute them to.  That is the same two-axis split as GMod's
// menu (its sidebar IS the source list: contentsidebar.lua:10-17 selects the
// source, gameprops.lua:126-150 fills it with mounted games and their
// "games/16/<name>.png" icons, addonprops.lua:95-138 fills it with addons and
// icon16/bricks.png, and both SKIP a source with nothing in it), with the
// derivation this fork can actually support - see SMenu_AssignSource.
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
#include "vgui/ILocalize.h"
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
	char			szName[128];	// HL2SB: display name - never empty, see SMenu_ResolveDisplayName
	char			szFixedCmd[SMENU_FIXEDCMD_LEN];	// non-empty: run verbatim
	char			szMaterial[128];// material to draw ("" = no icon, name only)
	char			szSource[128];	// which source it came from - see SMenu_AssignSource
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

// HL2SB: TOP TABS = CATEGORIES, and nothing else.  GMod's spawnmenu tabs answer
// the same question ("which kind of thing is this?"); the Lua/stock split that
// used to live here as indented sub-tabs is now the LEFT sidebar's source
// dimension (SMenu_AssignSource / g_SMenuCatSources), so it is deliberately not
// duplicated as a tab any more.
static const SMenuCatDef_t s_SMenuCats[] =
{
	{ "Weapons",				SMCAT_WEAPON,							0,				false },
	{ "Entities",				SMCAT_LUAENT | SMCAT_ENTITY,			0,				false },
	{ "NPCs",					SMCAT_NPC,								0,				false },
	{ "Props",					SMCAT_PROP,								0,				false },
	{ "Vehicles",				SMCAT_VEHICLE,							0,				false },
	// HL2SB: "Models", not "Props (models)".  GMod's own tab for a list of
	// spawnable models is `#spawnmenu.content_tab`, whose English text is
	// "Spawnlists" (resource/localization/en/spawnmenu.properties:2), and which
	// exists because GMod's model content is grouped into user-editable
	// spawnlists.  This page is not that: it is a flat scan of models/props_*,
	// so it gets the plain, readable name of what it actually lists.  The
	// parenthetical was the one tab title a user cannot read at a glance.
	{ "Models",					0,										0,				true },
};

#define SMENU_CAT_COUNT		ARRAYSIZE( s_SMenuCats )

//-----------------------------------------------------------------------------
// HL2SB: THE SECOND AXIS - which SOURCE an entry comes from.
//
// This is the left-hand sidebar.  One source list PER CATEGORY, filled by
// SMenu_BuildSources(); a source that has no entry in that category simply does
// not appear, which is how GMod hides an addon with nothing in it
// (addonprops.lua:101-102).
//-----------------------------------------------------------------------------
#define SMENU_SRC_OWN		"HL2SB Lua"		// the tree's own lua/ (and gamemode content)
#define SMENU_SRC_ENGINE	"Engine"		// server.dll-registered, no derivable source
#define SMENU_SRC_HL2		"Half-Life 2"	// the models page - provably HL2 content
#define SMENU_SRC_ADDONS	"addons/"		// prefix of an addon source id

struct SMenuSource_t
{
	char	szId[128];		// "addons/<folder>" | "HL2SB Lua" | "Half-Life 2" | "Engine"
	char	szLabel[128];	// what the sidebar row prints
	char	szMaterial[128];// 16x16 icon, existence-gated ("" = name only, never purple)
	int		nCount;			// entries of the current category in this source
};

static CUtlVector< SMenuSource_t > g_SMenuCatSources[SMENU_CAT_COUNT];

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
//   1. the fork's own icon set  - materials/vgui/smenu/<class>
//   2. GMod's spawnmenu thumbs   - materials/entities/<class>.png
//   3. one generic icon per category, and then - unconditionally, for every
//      category - GMod's own guaranteed floor, icon16/plugin.png
//
// The per-class probe is the only file system work here and it is CACHED,
// because the list is refreshed on every open (an addon may register content at
// any time) and re-probing ~700 class names would turn that into a visible
// hitch.  What the cache stores is the RESOLVED, BINDABLE name - not a guess
// that still has to be turned into one.
//-----------------------------------------------------------------------------
struct SMenuIconProbe_t
{
	char szSpecific[128];	// the resolved material for this class ("" = nothing on disk)
};

static CUtlDict< SMenuIconProbe_t, unsigned short > g_SMenuIconProbes;

static bool SMenu_FileExists( const char *pszFile )
{
	return filesystem->FileExists( pszFile );
}

// Q_strncpy can leave the destination UNTERMINATED when the source is longer
// than the buffer (V_strncpy copies exactly maxLen characters), so every
// variable-length source string goes through this.
static void SMenu_CopyString( char *pDest, const char *pSrc, int nDestLen )
{
	if ( !pDest || nDestLen <= 0 )
		return;

	Q_strncpy( pDest, pSrc ? pSrc : "", nDestLen );
	pDest[nDestLen - 1] = 0;
}

//-----------------------------------------------------------------------------
// HL2SB: THE MENU'S TEXT METRICS.
//
// Every label this menu draws is measured instead of guessed at, and every
// label is shortened ONLY by an explicit "...", never by the panel rectangle
// chopping a glyph in half (which is what the user saw).  The font is the one
// font clientscheme.res is guaranteed to define - see AGENTS.md 5.1, where
// GetFont("DefaultBold") returning INVALID_FONT is the recorded trap.
//-----------------------------------------------------------------------------
static HFont SMenu_LabelFont( vgui::Panel *pPanel = NULL )
{
	ISchemeManager *pSchemes = scheme();
	if ( !pSchemes )
		return INVALID_FONT;

	// The panel's own scheme is the one its ApplySchemeSettings got, so it is
	// asked first; the client's first loaded scheme is the fallback for the
	// measurements that happen before a panel has been through its scheme.
	if ( pPanel )
	{
		IScheme *pPanelScheme = pSchemes->GetIScheme( pPanel->GetScheme() );
		if ( pPanelScheme )
		{
			HFont hPanelFont = pPanelScheme->GetFont( "DefaultVerySmall", true );
			if ( hPanelFont != INVALID_FONT )
				return hPanelFont;
		}
	}

	IScheme *pScheme = pSchemes->GetIScheme( pSchemes->GetDefaultScheme() );
	if ( !pScheme )
		return INVALID_FONT;

	return pScheme->GetFont( "DefaultVerySmall", true );
}

//-----------------------------------------------------------------------------
// HL2SB: THE MENU'S OWN LABEL FONTS.
//
// This menu is laid out in UNSCALED pixels (76x80 cells, 20 px sidebar rows,
// an 820x620 frame), but a font that comes from resource/clientscheme.res is
// NOT: the engine scales a scheme font by the screen height, and every scheme
// font here declares "yres 480 599".  Measured in-game, `DefaultVerySmall`
// (Verdana tall 12 as written) reports GetFontTall() == 32 - about 2.7x its
// written size, i.e. the client was running a ~1280 px tall window.  A 32 px
// label in a 20 px row cannot fit, which is precisely what the user saw and
// reported twice: clipped cell names ("nron...") and a sidebar whose text was
// "still too big".
//
// So the two label areas get their own fonts, built the way this fork's own Lua
// UI fonts are built (lua/includes/modules/gmod_vgui.lua:362-363: an empty
// CreateFont() handle filled in by SetFontGlyphSet with 0x010 =
// FONTFLAG_ANTIALIAS), at sizes chosen for THIS layout's pixels:
//
//   * grid cells  - Verdana 13, in the 24 px band under the 56 px thumbnail
//   * sidebar     - Verdana 11, in a 20 px row (one step smaller still, which is
//                   what the user asked for on the sidebar specifically)
//
// The range arguments are deliberately left at their defaults (0, 0): per
// AGENTS.md 5.4 the engine's amalgam logic then lets the foreign fallback font
// cover 0x0100-0xFFFF, so a CJK addon title still draws.  Passing a full range
// instead would claim the whole BMP and render CJK as boxes.
//-----------------------------------------------------------------------------
#define SMENU_CELL_FONT_TALL	13
#define SMENU_SOURCE_FONT_TALL	11

static HFont SMenu_MakeFont( const char *pszFace, int nTall )
{
	ISurface *pSurface = surface();
	if ( !pSurface )
		return INVALID_FONT;

	const HFont hFont = pSurface->CreateFont();
	if ( hFont == INVALID_FONT )
		return INVALID_FONT;

	if ( !pSurface->SetFontGlyphSet( hFont, pszFace, nTall, 0, 0, 0, ISurface::FONTFLAG_ANTIALIAS ) )
		return INVALID_FONT;

	return hFont;
}

static HFont SMenu_CellFont( void )
{
	static HFont s_hFont = INVALID_FONT;
	static bool s_bTried = false;

	if ( !s_bTried )
	{
		s_bTried = true;
		s_hFont = SMenu_MakeFont( "Verdana", SMENU_CELL_FONT_TALL );
	}

	return s_hFont;
}

static HFont SMenu_SourceFont( void )
{
	static HFont s_hFont = INVALID_FONT;
	static bool s_bTried = false;

	if ( !s_bTried )
	{
		s_bTried = true;
		s_hFont = SMenu_MakeFont( "Verdana", SMENU_SOURCE_FONT_TALL );
	}

	return s_hFont;
}

static int SMenu_TextWidth( HFont hFont, const char *pszText, int nMaxChars = -1 )
{
	if ( !pszText || !pszText[0] || hFont == INVALID_FONT || !g_pVGuiLocalize )
		return 0;

	wchar_t wszText[192];
	g_pVGuiLocalize->ConvertANSIToUnicode( pszText, wszText, sizeof( wszText ) );

	int nChars = ( nMaxChars >= 0 ) ? nMaxChars : V_wcslen( wszText );
	if ( nChars > (int)ARRAYSIZE( wszText ) - 1 )
		nChars = (int)ARRAYSIZE( wszText ) - 1;

	if ( nChars <= 0 )
		return 0;

	wszText[nChars] = 0;

	int nWide = 0, nTall = 0;
	surface()->GetTextSize( hFont, wszText, nWide, nTall );

	return nWide;
}

// Shorten a label to fit nMaxWidth.  Returns true when it had to.
static bool SMenu_ShortenText( HFont hFont, const char *pszText, int nMaxWidth, char *pOut, int nOutLen )
{
	SMenu_CopyString( pOut, pszText, nOutLen );

	if ( !pszText || !pszText[0] || nMaxWidth <= 0 )
		return false;

	if ( SMenu_TextWidth( hFont, pszText ) <= nMaxWidth )
		return false;

	const int nEllipsis = SMenu_TextWidth( hFont, "..." );
	const int nLen = Q_strlen( pszText );

	int nKeep = nLen;
	while ( nKeep > 0 && SMenu_TextWidth( hFont, pszText, nKeep ) + nEllipsis > nMaxWidth )
		--nKeep;

	if ( nKeep <= 0 )
	{
		SMenu_CopyString( pOut, "...", nOutLen );
		return true;
	}

	char szHead[192];
	const int nCopy = MIN( nKeep, (int)sizeof( szHead ) - 1 );
	Q_memcpy( szHead, pszText, nCopy );
	szHead[nCopy] = 0;

	Q_snprintf( pOut, nOutLen, "%s...", szHead );
	return true;
}

//-----------------------------------------------------------------------------
// HL2SB: can the engine really BIND this material name?
//
// "a file with this name is on disk" is not the question, and getting it wrong
// is what drew the purple/black cells and the "--- Missing Vgui material" lines
// the user found in the log:
//
//   * A LONE .vtf IS NOT BINDABLE.  DrawSetTextureFile goes through
//     CMatSystemTexture::SetMaterial (vguimatsurface/TextureDictionary.cpp:718-
//     732), i.e. g_pMaterialSystem->FindMaterial(name) - a MATERIAL, not a raw
//     texture, and a material is a .vmt.  With no .vmt the engine synthesises an
//     UnlitGeneric material ONLY when a raw image sits next to it
//     (materialsystem/hl2sb_pngtexture.cpp loads .png/.jpg/.jpeg/.tga), so
//     `vgui/smenu/sent_ball` (a .vtf with no .vmt and no image) came back as the
//     ERROR material.  Those are exactly the classes in the user's log:
//     sent_ball, combine_mine, grenade_helicopter, prop_thumper - and there are
//     36 such names in materials/vgui/smenu/.  Every one of the 36 ALSO has a
//     real GMod spawnmenu thumbnail at materials/entities/<class>.png, which the
//     old ".vtf counts as loadable" probe was shadowing.
//
//   * A .vmt IS BINDABLE ONLY WHEN THE TEXTURE IT NAMES IS ON DISK TOO.  All 208
//     .vmt here use "$basetexture vgui/smenu/<their own name>", so the texture is
//     the sibling .vtf (73 of them do not have one) - or a sibling raw image,
//     which the same fallback covers: weapon_default.vmt resolves through
//     weapon_default.png, and the log proves it loads
//     (`[HL2SB] image texture "vgui/smenu/weapon_default" (256x128)`).
//
// So: bindable  ==  (a raw image exists)  ||  (.vmt exists AND .vtf exists).
// Re-checked against the whole icon set after the change (244 base names, 380
// files under materials/vgui/smenu): 131 .vmt+.vtf pairs and 4 .vmt+sibling
// image names stay bindable, 73 .vmt-only and 36 .vtf-only names now correctly
// fall through to GMod's thumbnail (or to the generic floor) instead of binding
// the ERROR material.
//-----------------------------------------------------------------------------
static bool SMenu_MaterialExists( const char *pszMaterial )
{
	static const char *s_pImages[] = { ".png", ".jpg", ".jpeg", ".tga" };

	char szPath[MAX_PATH];
	for ( int i = 0; i < ARRAYSIZE( s_pImages ); ++i )
	{
		Q_snprintf( szPath, sizeof( szPath ), "materials/%s%s", pszMaterial, s_pImages[i] );

		if ( SMenu_FileExists( szPath ) )
			return true;
	}

	Q_snprintf( szPath, sizeof( szPath ), "materials/%s.vmt", pszMaterial );
	if ( !SMenu_FileExists( szPath ) )
		return false;

	Q_snprintf( szPath, sizeof( szPath ), "materials/%s.vtf", pszMaterial );
	return SMenu_FileExists( szPath );
}

//-----------------------------------------------------------------------------
// HL2SB: the bindable name of a RAW IMAGE, extension included - or false when
// there is no image behind it at all.
//
// "the .vmt exists" is not the question (see SMenu_MaterialExists), and neither
// is "the name looks right": the icon16 / games/16 sets GMod's spawnmenu uses
// are raw .png files, and a raw image is bound by its NAME INCLUDING the
// extension (materialsystem/hl2sb_pngtexture.cpp).  So the candidate's extension
// is discovered here instead of assumed, and a candidate with nothing on disk
// leaves pOut empty - which is what keeps an icon-less source from drawing a
// purple cell.
//-----------------------------------------------------------------------------
static bool SMenu_ResolveMaterialName( const char *pszCandidate, char *pOut, int nOutLen )
{
	// Raw images only.  A LONE .vtf must NOT be returned here even when it is on
	// disk: binding "entities/x.vtf" would ask FindMaterial for
	// "entities/x.vtf.vmt", miss, and draw the ERROR material - which is the bug
	// this whole block exists to kill.  A .vtf is reachable ONLY through a .vmt
	// (SMenu_ResolveIconName / SMenu_MaterialExists handle that pair).
	static const char *s_pExts[] = { ".png", ".jpg", ".jpeg", ".tga" };

	if ( pOut && nOutLen > 0 )
		pOut[0] = 0;

	if ( !pszCandidate || !pszCandidate[0] || !pOut || nOutLen <= 0 )
		return false;

	char szPath[MAX_PATH];
	for ( int i = 0; i < ARRAYSIZE( s_pExts ); ++i )
	{
		Q_snprintf( szPath, sizeof( szPath ), "materials/%s%s", pszCandidate, s_pExts[i] );

		if ( SMenu_FileExists( szPath ) )
		{
			Q_snprintf( pOut, nOutLen, "%s%s", pszCandidate, s_pExts[i] );
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// HL2SB: the name to BIND for a material base.
//
// A .vmt wins when it can be bound: it carries the shader settings, and the
// engine's own "texture missing -> sibling raw image" fallback keeps it out of
// the ERROR material.  Otherwise the raw image's name is used, extension and
// all.  Returns false when neither exists, which is what makes the caller walk
// on to the next candidate instead of drawing a checkerboard.
//-----------------------------------------------------------------------------
static bool SMenu_ResolveIconName( const char *pszBase, char *pOut, int nOutLen )
{
	char szVmt[MAX_PATH];
	Q_snprintf( szVmt, sizeof( szVmt ), "materials/%s.vmt", pszBase );

	if ( SMenu_FileExists( szVmt ) && SMenu_MaterialExists( pszBase ) )
	{
		SMenu_CopyString( pOut, pszBase, nOutLen );
		return true;
	}

	return SMenu_ResolveMaterialName( pszBase, pOut, nOutLen );
}

// The generic floor is the same handful of names for every entry of a category,
// so resolve each one once instead of once per cell.
static const char *SMenu_GenericIcon( int nSlot )
{
	static char s_szGeneric[6][128];
	static bool s_bResolved[6] = { false, false, false, false, false, false };

	Assert( nSlot >= 0 && nSlot < 6 );

	if ( !s_bResolved[nSlot] )
	{
		s_bResolved[nSlot] = true;

		static const char *s_pNames[] =
		{
			"vgui/smenu/weapon_default",	// 0: weapons / everything else
			"icon16/monkey",				// 1: NPCs
			"icon16/car",					// 2: vehicles
			"icon16/box",					// 3: props
			"icon16/plugin",				// 4: addon / Lua content
			"icon16/plugin",				// 5: the unconditional floor
		};

		if ( !SMenu_ResolveIconName( s_pNames[nSlot], s_szGeneric[nSlot], sizeof( s_szGeneric[nSlot] ) ) )
			SMenu_ResolveIconName( "icon16/plugin", s_szGeneric[nSlot], sizeof( s_szGeneric[nSlot] ) );
	}

	return s_szGeneric[nSlot];
}

static void SMenu_ResolveIcon( SMenuEntry_t &entry )
{
	entry.szMaterial[0] = 0;

	unsigned short i = g_SMenuIconProbes.Find( entry.szClass );
	if ( i == g_SMenuIconProbes.InvalidIndex() )
	{
		SMenuIconProbe_t probe;
		probe.szSpecific[0] = 0;

		char szBase[MAX_PATH];

		// 1. the fork's own icon set - bound only when the engine can really
		//    load it (see SMenu_MaterialExists).
		Q_snprintf( szBase, sizeof( szBase ), "vgui/smenu/%s", entry.szClass );
		if ( !SMenu_ResolveIconName( szBase, probe.szSpecific, sizeof( probe.szSpecific ) ) )
		{
			// 2. GMod's spawnmenu thumbnail - a raw .png, so the file IS the
			//    material and the name has to keep its extension.
			Q_snprintf( szBase, sizeof( szBase ), "entities/%s", entry.szClass );
			SMenu_ResolveMaterialName( szBase, probe.szSpecific, sizeof( probe.szSpecific ) );
		}

		i = g_SMenuIconProbes.Insert( entry.szClass, probe );
	}

	if ( i != g_SMenuIconProbes.InvalidIndex() && g_SMenuIconProbes[i].szSpecific[0] )
	{
		SMenu_CopyString( entry.szMaterial, g_SMenuIconProbes[i].szSpecific, sizeof( entry.szMaterial ) );
		return;
	}

	// 3. one generic icon per category, then - unconditionally, for EVERY
	//    category - icon16/plugin.png.  That is exactly the floor GMod's own
	//    spawnmenu uses (the log shows it loading: `image texture
	//    "icon16/plugin.png" (16x16)`), so a cell can never be left holding a
	//    material name that resolves to nothing.  Neither candidate is taken on
	//    faith: SMenu_ResolveIconName is the file system check.
	int nSlot = 0;

	if ( entry.uFlags & SMCAT_NPC )
		nSlot = 1;
	else if ( entry.uFlags & SMCAT_VEHICLE )
		nSlot = 2;
	else if ( entry.uFlags & SMCAT_PROP )
		nSlot = 3;
	else if ( entry.uFlags & ( SMCAT_LUAENT | SMFLAG_LUA ) )
		nSlot = 4;	// addon/plugin content - GMod's icon

	SMenu_CopyString( entry.szMaterial, SMenu_GenericIcon( nSlot ), sizeof( entry.szMaterial ) );

	if ( !entry.szMaterial[0] )
		SMenu_CopyString( entry.szMaterial, SMenu_GenericIcon( 5 ), sizeof( entry.szMaterial ) );
}

//-----------------------------------------------------------------------------
// HL2SB: THE DISPLAY NAME.
//
// GMod labels a spawnmenu icon with the entry's NICE NAME, never with its spawn
// name: contenticon.lua:61-66 (`SetName(obj.nicename)`), and the content types
// build that name from the content's own data -
//   weapons.lua:4     nicename = ent.PrintName or ent.ClassName
//   entities.lua:10   nicename = ent.PrintName or ent.SpawnName
//   npcs.lua:12       nicename = ent.Name      or ent.SpawnName
//   vehicles.lua:9    nicename = ent.PrintName or ent.SpawnName
// - so a spawnmenu cell reads "SMG", "Nyan Gun", "Sexyness", never
// "weapon_smg1".  This menu printed the raw class name instead, which is exactly
// what the user complained about.  Resolution order, best first:
//
//   1. the Lua registry for the class - `list.Set("Weapon", ...)` stores
//      SWEP.PrintName (weapons.lua:58-67) and `list.Set("SpawnableEntities",
//      ...)` the SENT's PrintName (scripted_ents.lua:123-125);
//   2. the ENGINE weapon script, scripts/<class>.txt `printname` - how every
//      stock HL2 weapon names itself, and the very string
//      CBaseCombatWeapon::GetPrintName() returns (basecombatweapon_shared.cpp:347,
//      which reads GetWpnData().szPrintName out of the same cached database);
//   3. a language token keyed by the class name ("#<class>");
//   4. the class name - and, for the weapon-ish classes this menu files under
//      Weapons (weapon_/item_/ammo_), the class name with its prefix dropped and
//      its underscores turned into spaces, so that page can never show
//      "weapon_fists".  That is the user's rule, and it is the honest floor:
//      this fork's own SWEPs spell their PrintName as a language token that no
//      mounted language file defines ("#GMOD_Fists", "#weapon_medkit",
//      "#sent_ball" - none of them are in hl2/hl2sb resource/*.txt), so without
//      this step they would fall all the way back to the class name.
//
// A token is resolved the way the HUD resolves it (hud_weaponselection.cpp:1216-
// 1244): the fork's own "<token>_Menu" entry wins - it is the compact menu name,
// and it is what resource/hl2sb_english.txt adds for the stock weapons
// ("HL2_SMG1_Menu" = "SMG") - then the plain token, and only its FIRST LINE is
// used because Valve's plain tokens carry the description on a second line
// ("SMG\n(SUBMACHINE GUN)").  A '#' token that resolves to NOTHING is not shown
// at all: the chain falls through instead, which is what turns "#GMOD_Fists"
// into "Fists" rather than into a raw token.
//
// Both the Lua probe and the script probe are CACHED per class for the life of
// the process (g_SMenuNameCache): the entry set is rebuilt on every menu open,
// but a class's display name cannot change without a level change.
//-----------------------------------------------------------------------------
static CUtlDict< CUtlString, unsigned short > g_SMenuNameCache;

// The number of Lua-registered entries the last build saw.  When it changes the
// Lua loaders have run (or run again), so every cached display name is re-asked
// - see the long note in SMenu_BuildEntries.
static int s_nSMenuLastNameLuaCount = -1;

// One `list.GetEntry(listid, class).<field>` lookup.  Never leaves a value on
// the Lua stack (luasrc_pcall pops its own error object).
static bool SMenu_LuaListField( const char *pszListId, const char *pszClass, const char *pszField, char *pOut, int nOutLen )
{
	if ( pOut && nOutLen > 0 )
		pOut[0] = 0;

	if ( !L || !pszListId || !pszClass || !pszField || !pOut || nOutLen <= 0 )
		return false;

	lua_getglobal( L, "list" );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_getfield( L, -1, "GetEntry" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return false;
	}

	lua_remove( L, -2 );					// [GetEntry]
	lua_pushstring( L, pszListId );			// [GetEntry][listid]
	lua_pushstring( L, pszClass );			// [GetEntry][listid][class]

	if ( luasrc_pcall( L, 2, 1, 0 ) != 0 )
		return false;						// the error object is already popped

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_getfield( L, -1, pszField );

	bool bFound = false;
	if ( lua_type( L, -1 ) == LUA_TSTRING )
	{
		const char *pszValue = lua_tostring( L, -1 );
		if ( pszValue && pszValue[0] )
		{
			SMenu_CopyString( pOut, pszValue, nOutLen );
			bFound = true;
		}
	}

	lua_pop( L, 2 );
	return bFound;
}

// One `<module>.<method>(class).<field>` lookup, never leaving a value on the
// Lua stack.  Used for the SWEP/SENT tables themselves (weapons.GetStored /
// scripted_ents.GetStored), which is where a script's own PrintName lives.
static bool SMenu_LuaModuleField( const char *pszModule, const char *pszMethod, const char *pszClass, const char *pszField, char *pOut, int nOutLen )
{
	if ( pOut && nOutLen > 0 )
		pOut[0] = 0;

	if ( !L || !pszModule || !pszMethod || !pszClass || !pszField || !pOut || nOutLen <= 0 )
		return false;

	lua_getglobal( L, pszModule );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_getfield( L, -1, pszMethod );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return false;
	}

	lua_remove( L, -2 );					// [method]
	lua_pushstring( L, pszClass );			// [method][class]

	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
		return false;						// the error object is already popped

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_getfield( L, -1, pszField );

	bool bFound = false;
	if ( lua_type( L, -1 ) == LUA_TSTRING )
	{
		const char *pszValue = lua_tostring( L, -1 );
		if ( pszValue && pszValue[0] )
		{
			SMenu_CopyString( pOut, pszValue, nOutLen );
			bFound = true;
		}
	}

	lua_pop( L, 2 );
	return bFound;
}

// scripts/<class>.txt `printname`, straight out of the engine's own cached
// weapon-info database.  Silent and cheap when the class has no weapon script:
// the file is probed on the file system first, so the database is never
// polluted with a slot for a class that is not a weapon at all.
static bool SMenu_WeaponScriptName( const char *pszClass, char *pOut, int nOutLen )
{
	if ( pOut && nOutLen > 0 )
		pOut[0] = 0;

	if ( !pszClass || !pszClass[0] || !pOut || nOutLen <= 0 )
		return false;

	char szScript[MAX_PATH];
	Q_snprintf( szScript, sizeof( szScript ), "scripts/%s.txt", pszClass );

	if ( !SMenu_FileExists( szScript ) )
		return false;

	WEAPON_FILE_INFO_HANDLE hWeapon = LookupWeaponInfoSlot( pszClass );
	if ( hWeapon == GetInvalidWeaponInfoHandle() )
	{
		if ( !ReadWeaponDataFromFileForSlot( filesystem, pszClass, &hWeapon, NULL ) )
			return false;
	}

	FileWeaponInfo_t *pInfo = GetFileWeaponInfoFromHandle( hWeapon );
	if ( !pInfo || !pInfo->szPrintName[0] )
		return false;

	SMenu_CopyString( pOut, pInfo->szPrintName, nOutLen );
	return true;
}

// Resolve one candidate string to something a cell can print.  Returns true when
// the candidate was usable: a plain string always is, a "#token" only when a
// language file knows it.
static bool SMenu_LocalizeCandidate( const char *pszRaw, char *pOut, int nOutLen )
{
	if ( pOut && nOutLen > 0 )
		pOut[0] = 0;

	if ( !pszRaw || !pszRaw[0] || !pOut || nOutLen <= 0 )
		return false;

	const bool bToken = ( pszRaw[0] == '#' );
	const char *pszKey = bToken ? pszRaw + 1 : pszRaw;

	if ( g_pVGuiLocalize && pszKey[0] )
	{
		char szMenuKey[160];
		Q_snprintf( szMenuKey, sizeof( szMenuKey ), "%s_Menu", pszKey );

		wchar_t *pWide = g_pVGuiLocalize->Find( szMenuKey );
		if ( !pWide )
			pWide = g_pVGuiLocalize->Find( pszKey );

		if ( pWide && pWide[0] )
		{
			// Valve's own tokens keep the description on the second line
			// ("SMG\n(SUBMACHINE GUN)"); a grid cell wants the name.
			wchar_t wszLine[192];
			int i = 0;
			for ( ; pWide[i] && pWide[i] != L'\n' && i < (int)ARRAYSIZE( wszLine ) - 1; ++i )
				wszLine[i] = pWide[i];
			wszLine[i] = 0;

			char szAnsi[256];
			if ( wszLine[0] && g_pVGuiLocalize->ConvertUnicodeToANSI( wszLine, szAnsi, sizeof( szAnsi ) ) )
				SMenu_CopyString( pOut, szAnsi, nOutLen );
		}
	}

	// Not a token, and the language files had nothing to say about it: it is
	// already a name (a SWEP's "Nyan Gun", an addon's "Sexyness").
	if ( !pOut[0] && !bToken )
		SMenu_CopyString( pOut, pszRaw, nOutLen );

	return pOut[0] != 0;
}

// The last resort for a class with no name anywhere - see the note above.
static void SMenu_HumanizeClassName( const char *pszClass, char *pOut, int nOutLen )
{
	static const char *s_pPrefixes[] = { "weapon_", "item_", "ammo_" };

	const char *pszName = pszClass;

	for ( int i = 0; i < ARRAYSIZE( s_pPrefixes ); ++i )
	{
		const int nPrefix = Q_strlen( s_pPrefixes[i] );

		if ( !Q_strnicmp( pszClass, s_pPrefixes[i], nPrefix ) && pszClass[nPrefix] )
		{
			pszName = pszClass + nPrefix;
			break;
		}
	}

	if ( pszName == pszClass )
	{
		SMenu_CopyString( pOut, pszClass, nOutLen );
		return;
	}

	int j = 0;
	for ( int i = 0; pszName[i] && j < nOutLen - 1; ++i )
	{
		char c = pszName[i];

		if ( c == '_' )
			c = ' ';
		else if ( j == 0 && c >= 'a' && c <= 'z' )
			c = (char)( c - 'a' + 'A' );

		pOut[j++] = c;
	}

	pOut[j] = 0;
}

static void SMenu_ResolveDisplayName( SMenuEntry_t &entry )
{
	unsigned short i = g_SMenuNameCache.Find( entry.szClass );

	if ( i == g_SMenuNameCache.InvalidIndex() )
	{
		char szCandidate[128];
		char szName[128];

		szName[0] = 0;

		// 1. the Lua registries.  THE FORK'S OWN SWEP REGISTRY COMES FIRST:
		//    luasrc_LoadOneWeapon registers every SWEP through Team Sandbox's
		//    `weapon.register` (luamanager.cpp:1506-1513 ->
		//    lua/includes/modules/weapon.lua:75-80), which keeps its OWN table -
		//    it does not touch GMod's `weapons` module - so `weapon.get` is the
		//    registry that actually holds this fork's and its addons' SWEPs
		//    (proved by the log: weapons.GetStored("weapon_nyangun") is nil while
		//    weapon.get returns the SWEP with its PrintName).  GMod's module and
		//    the list entries are still asked, for a GMod-style addon that
		//    registers itself.
		if ( SMenu_LuaModuleField( "weapon", "get", entry.szClass, "PrintName", szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		if ( !szName[0] && SMenu_LuaModuleField( "weapons", "GetStored", entry.szClass, "PrintName", szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		if ( !szName[0] && SMenu_LuaModuleField( "scripted_ents", "GetStored", entry.szClass, "PrintName", szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		if ( !szName[0] && SMenu_LuaListField( "Weapon", entry.szClass, "PrintName", szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		if ( !szName[0] && SMenu_LuaListField( "SpawnableEntities", entry.szClass, "PrintName", szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		if ( !szName[0] && SMenu_LuaListField( "Vehicles", entry.szClass, "PrintName", szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		if ( !szName[0] && SMenu_LuaListField( "NPC", entry.szClass, "Name", szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		// 2. the engine weapon script - stock HL2 weapons name themselves there.
		if ( !szName[0] && SMenu_WeaponScriptName( entry.szClass, szCandidate, sizeof( szCandidate ) ) )
			SMenu_LocalizeCandidate( szCandidate, szName, sizeof( szName ) );

		// 3. a language token keyed by the class name.
		if ( !szName[0] )
		{
			char szKey[80];
			Q_snprintf( szKey, sizeof( szKey ), "#%s", entry.szClass );
			SMenu_LocalizeCandidate( szKey, szName, sizeof( szName ) );
		}

		// 4. the class name - humanized for the weapon-ish entries.
		if ( !szName[0] )
			SMenu_HumanizeClassName( entry.szClass, szName, sizeof( szName ) );

		unsigned short nNew = g_SMenuNameCache.Insert( entry.szClass, CUtlString( szName ) );
		if ( nNew == g_SMenuNameCache.InvalidIndex() )
		{
			SMenu_CopyString( entry.szName, szName, sizeof( entry.szName ) );
			return;
		}

		i = nNew;
	}

	SMenu_CopyString( entry.szName, (const char *)g_SMenuNameCache[i], sizeof( entry.szName ) );
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
// HL2SB: WHERE an entry comes from - the left-hand source sidebar.
//
// GMod's spawnmenu separates WHAT a thing is (its top tabs) from WHICH SOURCE it
// comes from (its sidebar), and that is the split this file now mirrors:
//
//   "addons/<folder>"   a Lua class whose script lives in an addon   (bricks.png)
//   "HL2SB Lua"         a Lua class whose script lives in the tree's own lua/,
//                       the unpacked lua_cache/ or a gamemode's content/
//   "Half-Life 2"       the models page, which only ever lists HL2's props_*
//   "Engine"            everything server.dll registered with no derivable source
//
// The derivation does not guess.  Two mechanisms, in order:
//
//   1. THE LOADER, authoritatively.  luasrc_LoadOneWeapon / luasrc_LoadOneEntity
//      know the exact file a class was loaded from (they already log it with
//      their "[Lua] weapon/entity '<class>' <- <path>" line), and that file is
//      read back through luasrc_GetClassScriptFile() (luamanager.h).  This is
//      the reliable route: a mounted addon's script has the SAME MOD-relative
//      name as the tree's own ("lua/weapons/<class>.lua") because
//      mountaddons.cpp mounts every addon folder as its own search path, so the
//      loader stores it with the addon folder folded back in:
//      "addons/nyangun/lua/weapons/weapon_nyangun.lua".
//   2. THE FILE SYSTEM, as a fallback for a Lua class the loaders never saw (a
//      class the server published, a SWEP registered from an addon's
//      lua/autorun/): probe addons/<x>/lua/{weapons,entities}/<class>.lua and
//      then lua/{weapons,entities}/<class>.lua through the engine file system,
//      plus GMod's folder layout <class>/shared.lua.  Every probe is CACHED
//      (g_SMenuSourceProbes); one pass per rebuild.
//
// Either way an entry always lands in a bucket - "Engine" is the honest one for
// anything that cannot be attributed - and Lua content is never filtered out or
// hidden.
//-----------------------------------------------------------------------------
// HL2SB: what a sidebar row PRINTS.
//
// GMod labels a source node with the addon's real title, not with its folder
// path: addonprops.lua:104 `MyNode:AddNode(addon.title .. " (" ..
// addon.models .. ")")`, where the title comes from the addon's own metadata.
// `addons/<folder>` is technically correct and user-hostile - it is a PATH, it
// is long enough that the sidebar clipped it, and the user asked for it to go -
// so the folder is looked up:
//
//   * addons/<x>/addon.json - the "title" member (GMod's own addon metadata);
//   * addons/<x>/addon.txt  - "name" inside the AddonInfo block (the older
//                             workshop layout; this tree's "The Ultimate Admin
//                             Gun Fixed" ships exactly that);
//   * otherwise the folder name itself, prefixes chopped and underscores turned
//     into spaces so the row still reads like a title.
//
// The ID keeps its `addons/<folder>` spelling: that is the filter key the pages
// match on (SMenu_AssignSource produces it), and only the label is cosmetic.
//-----------------------------------------------------------------------------
static bool SMenu_ReadAddonMetadataTitle( const char *pszFolder, char *pOut, int nOutLen )
{
	if ( !pOut || nOutLen <= 0 )
		return false;

	pOut[0] = 0;

	static const char *s_pFiles[] = { "addon.json", "addon.txt" };
	static const char *s_pKeys[] = { "\"title\"", "\"name\"" };

	for ( int i = 0; i < ARRAYSIZE( s_pFiles ); ++i )
	{
		char szPath[MAX_PATH];
		Q_snprintf( szPath, sizeof( szPath ), "addons/%s/%s", pszFolder, s_pFiles[i] );

		FileHandle_t hFile = filesystem->Open( szPath, "rb", "MOD" );
		if ( !hFile )
			continue;

		const int nSize = filesystem->Size( hFile );
		char szBuffer[4096];
		int nRead = 0;

		if ( nSize > 0 )
			nRead = filesystem->Read( szBuffer, MIN( nSize, (int)sizeof( szBuffer ) - 1 ), hFile );

		filesystem->Close( hFile );

		szBuffer[ ( nRead > 0 ) ? nRead : 0 ] = 0;

		const char *pKey = Q_stristr( szBuffer, s_pKeys[i] );
		if ( !pKey )
			continue;

		// The value is the next quoted string after the key.
		const char *pValue = strchr( pKey + Q_strlen( s_pKeys[i] ), '"' );
		if ( !pValue )
			continue;

		++pValue;

		const char *pEnd = strchr( pValue, '"' );
		if ( !pEnd || pEnd <= pValue )
			continue;

		const int nLen = MIN( (int)( pEnd - pValue ), nOutLen - 1 );

		Q_memcpy( pOut, pValue, nLen );
		pOut[nLen] = 0;
		return true;
	}

	return false;
}

static void SMenu_SourceLabel( const char *pszSourceId, char *pOut, int nOutLen )
{
	SMenu_CopyString( pOut, pszSourceId, nOutLen );

	// "HL2SB Lua" / "Half-Life 2" / "Engine" already read fine - they are names,
	// not paths.
	if ( Q_strnicmp( pszSourceId, SMENU_SRC_ADDONS, 7 ) != 0 )
		return;

	const char *pszFolder = pszSourceId + 7;

	char szTitle[128];
	if ( SMenu_ReadAddonMetadataTitle( pszFolder, szTitle, sizeof( szTitle ) ) )
	{
		SMenu_CopyString( pOut, szTitle, nOutLen );
		return;
	}

	int j = 0;
	for ( int i = 0; pszFolder[i] && j < nOutLen - 1; ++i )
		pOut[j++] = ( pszFolder[i] == '_' ) ? ' ' : pszFolder[i];

	pOut[j] = 0;
}

//-----------------------------------------------------------------------------
static CUtlDict< CUtlString, unsigned short > g_SMenuSourceProbes;

static CUtlVector< CUtlString > g_SMenuAddonDirs;
static bool g_bSMenuAddonDirsScanned = false;

// addons/* read once.  Every addon folder is also its own MOD search path
// (mountaddons.cpp:64), so this list exists only to build probe paths.
static void SMenu_ScanAddonDirs( void )
{
	if ( g_bSMenuAddonDirsScanned )
		return;

	g_bSMenuAddonDirsScanned = true;

	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *pszFound = filesystem->FindFirstEx( "addons/*", "MOD", &hFind );

	for ( ; pszFound && pszFound[0]; pszFound = filesystem->FindNext( hFind ) )
	{
		if ( pszFound[0] == '.' || !filesystem->FindIsDirectory( hFind ) )
			continue;

		bool bSeen = false;
		for ( int i = 0; i < g_SMenuAddonDirs.Count(); ++i )
		{
			if ( !Q_stricmp( g_SMenuAddonDirs[i], pszFound ) )
			{
				bSeen = true;
				break;
			}
		}

		if ( !bSeen )
			g_SMenuAddonDirs.AddToTail( CUtlString( pszFound ) );
	}

	if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( hFind );
}

// The fallback probe.  Returns the source-qualified script found, or "".
static const char *SMenu_ProbeClassScript( const char *pszClass )
{
	unsigned short i = g_SMenuSourceProbes.Find( pszClass );

	if ( i != g_SMenuSourceProbes.InvalidIndex() )
		return (const char *)g_SMenuSourceProbes[i];

	SMenu_ScanAddonDirs();

	// Both GMod layouts: <class>.lua and <class>/shared.lua (the loaders accept
	// cl_init.lua / init.lua as well - see luasrc_LoadOneWeapon).
	static const char *s_pScriptNames[] = { "%s.lua", "%s/shared.lua", "%s/cl_init.lua", "%s/init.lua" };
	static const char *s_pKinds[] = { "weapons", "entities" };

	char szFound[MAX_PATH];
	szFound[0] = 0;

	// addons first (the user's own content), then the tree's own lua/.
	for ( int nKind = 0; nKind < ARRAYSIZE( s_pKinds ) && !szFound[0]; ++nKind )
	{
		for ( int nRoot = -1; nRoot < g_SMenuAddonDirs.Count() && !szFound[0]; ++nRoot )
		{
			const bool bAddon = ( nRoot >= 0 );

			for ( int nName = 0; nName < ARRAYSIZE( s_pScriptNames ) && !szFound[0]; ++nName )
			{
				char szBase[MAX_PATH];

				if ( bAddon )
					Q_snprintf( szBase, sizeof( szBase ), "addons/%s/lua/%s/%s", g_SMenuAddonDirs[nRoot], s_pKinds[nKind], pszClass );
				else
					Q_snprintf( szBase, sizeof( szBase ), "lua/%s/%s", s_pKinds[nKind], pszClass );

				char szScript[MAX_PATH];
				Q_snprintf( szScript, sizeof( szScript ), s_pScriptNames[nName], szBase );

				if ( SMenu_FileExists( szScript ) )
					SMenu_CopyString( szFound, szScript, sizeof( szFound ) );
			}
		}
	}

	unsigned short nNew = g_SMenuSourceProbes.Insert( pszClass, CUtlString( szFound ) );

	if ( nNew == g_SMenuSourceProbes.InvalidIndex() )
		return "";

	return (const char *)g_SMenuSourceProbes[nNew];
}

// "addons/nyangun/lua/weapons/weapon_nyangun.lua" -> "addons/nyangun";
// "lua/entities/sent_ball.lua" -> "HL2SB Lua"; "" -> "" (caller picks the floor).
static void SMenu_SourceIdFromScript( const char *pszScript, char *pOut, int nOutLen )
{
	if ( pOut && nOutLen > 0 )
		pOut[0] = 0;

	if ( !pszScript || !pszScript[0] || !pOut || nOutLen <= 0 )
		return;

	if ( !Q_strnicmp( pszScript, SMENU_SRC_ADDONS, 7 ) )
	{
		const char *pFolder = pszScript + 7;
		const char *pSlash = strchr( pFolder, '/' );
		const int nLen = pSlash ? (int)( pSlash - pFolder ) : Q_strlen( pFolder );

		if ( nLen <= 0 )
			return;

		char szFolder[128];
		int nCopy = MIN( nLen, (int)sizeof( szFolder ) - 1 );
		Q_memcpy( szFolder, pFolder, nCopy );
		szFolder[nCopy] = 0;

		Q_snprintf( pOut, nOutLen, SMENU_SRC_ADDONS "%s", szFolder );
		return;
	}

	// The fork's own tree.  lua_cache/ is the same lua/ tree under a prefix, and
	// a gamemode's content/ folder is first-party content too.
	if ( !Q_strnicmp( pszScript, "lua/", 4 ) || !Q_strnicmp( pszScript, "lua_cache/", 10 ) ||
		 !Q_strnicmp( pszScript, "gamemodes/", 10 ) )
	{
		SMenu_CopyString( pOut, SMENU_SRC_OWN, nOutLen );
	}
}

//-----------------------------------------------------------------------------
// HL2SB: place one entry in a source.  Runs ONCE per rebuild, after every flag
// has settled (the Lua lists may recategorise a class, and only a Lua class has
// a script to attribute), so the sidebar can never contradict its entries.
//-----------------------------------------------------------------------------
static void SMenu_AssignSource( SMenuEntry_t &entry )
{
	char szSource[sizeof( entry.szSource )];
	szSource[0] = 0;

	if ( entry.uFlags & SMFLAG_LUA )
	{
		// 1. what the loader actually read - authoritative.
		SMenu_SourceIdFromScript( luasrc_GetClassScriptFile( entry.szClass ), szSource, sizeof( szSource ) );

		// 2. the file system fallback, cached.
		if ( !szSource[0] )
			SMenu_SourceIdFromScript( SMenu_ProbeClassScript( entry.szClass ), szSource, sizeof( szSource ) );
	}

	// 3. The honest bucket.  Everything server.dll registers lands here: this
	//    fork has no per-game registration data to attribute a stock class to
	//    (SMenu_TODO.md item 7.1 - do NOT invent a game for a class we cannot
	//    attribute).  The models page is the one exception and names itself.
	if ( !szSource[0] )
		SMenu_CopyString( szSource, SMENU_SRC_ENGINE, sizeof( szSource ) );

	SMenu_CopyString( entry.szSource, szSource, sizeof( entry.szSource ) );
}

//-----------------------------------------------------------------------------
// HL2SB: a source's 16x16 icon, by GMod's own convention:
//   * the provable Half-Life 2 page -> "games/16/hl2.png"
//     (gameprops.lua:146 builds exactly this name from the game entry)
//   * an addon -> "icon16/bricks.png" (addonprops.lua:104)
//   * EVERY source -> icon16/plugin.png as the existence-gated last resort
//     (the same floor SMenu_ResolveIcon uses for a cell)
// Nothing is taken on faith: a candidate with no file on disk is skipped, and a
// source that ends up with no icon draws its NAME only - never a purple cell.
//-----------------------------------------------------------------------------
static void SMenu_ResolveSourceIcon( SMenuSource_t &src )
{
	src.szMaterial[0] = 0;

	if ( !Q_stricmp( src.szId, SMENU_SRC_HL2 ) )
		SMenu_ResolveMaterialName( "games/16/hl2", src.szMaterial, sizeof( src.szMaterial ) );
	else if ( !Q_strnicmp( src.szId, SMENU_SRC_ADDONS, 7 ) )
		SMenu_ResolveMaterialName( "icon16/bricks", src.szMaterial, sizeof( src.szMaterial ) );

	if ( !src.szMaterial[0] )
		SMenu_ResolveMaterialName( "icon16/plugin", src.szMaterial, sizeof( src.szMaterial ) );
}

// The user's own content first, then the tree's own Lua, then the built-in
// pages; alphabetical inside a group (GMod lists games before addons and sorts
// each group by title).
static int SMenu_SourceRank( const SMenuSource_t &src )
{
	if ( !Q_strnicmp( src.szId, SMENU_SRC_ADDONS, 7 ) )
		return 0;
	if ( !Q_stricmp( src.szId, SMENU_SRC_OWN ) )
		return 1;
	if ( !Q_stricmp( src.szId, SMENU_SRC_HL2 ) )
		return 2;

	return 3;			// Engine
}

static int __cdecl SMenu_SortSources( const SMenuSource_t *pLeft, const SMenuSource_t *pRight )
{
	const int nLeft = SMenu_SourceRank( *pLeft );
	const int nRight = SMenu_SourceRank( *pRight );

	if ( nLeft != nRight )
		return nLeft - nRight;

	return Q_stricmp( pLeft->szId, pRight->szId );
}

static SMenuSource_t *SMenu_FindOrAddSource( int nCat, const char *pszSource )
{
	for ( int i = 0; i < g_SMenuCatSources[nCat].Count(); ++i )
	{
		if ( !Q_stricmp( g_SMenuCatSources[nCat][i].szId, pszSource ) )
			return &g_SMenuCatSources[nCat][i];
	}

	SMenuSource_t &src = g_SMenuCatSources[nCat][g_SMenuCatSources[nCat].AddToTail()];
	Q_memset( &src, 0, sizeof( src ) );
	SMenu_CopyString( src.szId, pszSource, sizeof( src.szId ) );
	SMenu_SourceLabel( pszSource, src.szLabel, sizeof( src.szLabel ) );
	SMenu_ResolveSourceIcon( src );

	return &src;
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

		// HL2SB: the SOURCE is part of an entry's identity now - if an addon
		// mounts a script for a class that used to sit in another bucket, the
		// sidebar has to follow, so it is hashed with the class and the flags.
		for ( const char *p = g_SMenuEntries[i].szSource; *p; ++p )
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
//-----------------------------------------------------------------------------
// HL2SB: DOES GMod CONTENT REALLY RESOLVE?
//
// The user's fourth complaint was that GMod assets looked unimported and the VPK
// possibly unmounted.  A directory listing cannot answer that: the DATA of a
// multi-chunk VPK is not in the _dir file at all - the engine strips "_dir" and
// rebuilds the names by convention, opening the directory file as
// "<base>_dir.vpk" and every chunk as "<base>_%03d.vpk" in the same directory
// (vpklib/packedstore.cpp:285-287 and :450).  The only meaningful test is the
// engine's own file system, so this reads one asset that exists ONLY inside a
// numbered GMod chunk and one loose control:
//
//   * materials/widgets/scale2.png - archive chunk 1 of garrysmod_dir.vpk
//     (i.e. inside garrysmod_001.vpk, 1239 bytes, verified against the VPK tree,
//     and not present loose anywhere on this game's search paths).  Finding and
//     READING it proves the numbered chunks are being followed.
//   * materials/entities/npc_zombie.png - the loose control, so a failure of the
//     first probe cannot be blamed on the search path itself.
//
// Printed once per process, from the first list build, so it lands in the log
// next to the other "[HL2SB] SMenu:" lines - the panel's constructor runs before
// cfg/autoexec.cfg (and therefore con_logfile) has been executed, so a Msg from
// there never reaches ds_debug.log.
//-----------------------------------------------------------------------------
static void SMenu_ProbeGModContent( void )
{
	static bool s_bProbed = false;
	if ( s_bProbed )
		return;

	s_bProbed = true;

	struct SMenuProbe_t
	{
		const char	*pszPath;
		const char	*pszWhat;
	};

	static const SMenuProbe_t s_pProbes[] =
	{
		{ "materials/widgets/scale2.png",		"garrysmod_001.vpk chunk" },
		{ "materials/entities/npc_zombie.png",	"loose control" },
	};

	for ( int i = 0; i < ARRAYSIZE( s_pProbes ); ++i )
	{
		const char *pszPath = s_pProbes[i].pszPath;
		const bool bExists = filesystem->FileExists( pszPath );

		FileHandle_t hFile = filesystem->Open( pszPath, "rb", "GAME" );
		int nSize = hFile ? filesystem->Size( hFile ) : -1;
		int nRead = 0;
		unsigned char pHeader[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

		if ( hFile )
		{
			nRead = filesystem->Read( pHeader, sizeof( pHeader ), hFile );
			filesystem->Close( hFile );
		}

		Msg( "[HL2SB] SMenu: mount probe [%s] \"%s\" - FileExists=%d, size=%d, read=%d, magic=%02x%02x\n",
			 s_pProbes[i].pszWhat, pszPath, bExists ? 1 : 0, nSize, nRead, pHeader[0], pHeader[1] );
	}
}

//-----------------------------------------------------------------------------
// HL2SB: what the display names actually resolve to, in the log.
//
// SMenu_ResolveDisplayName is a chain (Lua registry -> engine weapon script ->
// language token -> class name), so the only way to see which link answered for
// a given class is to ask it.  This prints one line for a few representative
// classes - the stock weapons the user complained about, the fork's own SWEPs
// whose "#token" print name no mounted language file defines, and a SENT - so
// any launch documents that a weapon is no longer labelled `weapon_xxx`.
//
// It waits for a build that has Lua-registered entries (nLuaNow > 0): the very
// first build after a map load happens before luasrc_LoadWeapons has run, and
// reporting THAT would report the fallback names, not the real ones.
//-----------------------------------------------------------------------------
// One-off: dump what each link of the chain actually sees for one class, so a
// mis-resolved name can be diagnosed from the log instead of guessed at.  Runs
// once, together with the name probe.
static void SMenu_DumpLuaChain( const char *pszClass )
{
	if ( !L )
	{
		Msg( "[HL2SB] SMenu: lua chain \"%s\" - the Lua state is NULL\n", pszClass );
		return;
	}

	struct SMenuChainStep_t
	{
		const char	*pszModule;
		const char	*pszMethod;
		const char	*pszField;
	};

	static const SMenuChainStep_t s_pSteps[] =
	{
		{ "weapon",			"get",			"PrintName" },
		{ "weapons",		"GetStored",	"PrintName" },
		{ "scripted_ents",	"GetStored",	"PrintName" },
		{ "list",			"GetEntry",		"PrintName" },
	};

	for ( int i = 0; i < ARRAYSIZE( s_pSteps ); ++i )
	{
		const char *pszValue = "(unavailable)";

		lua_getglobal( L, s_pSteps[i].pszModule );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, s_pSteps[i].pszMethod );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_remove( L, -2 );			// [method]

				// weapons.GetStored / scripted_ents.GetStored take the class name;
				// list.GetEntry takes (listid, class).
				int nArgs = 1;
				if ( !Q_stricmp( s_pSteps[i].pszModule, "list" ) )
				{
					lua_pushstring( L, "Weapon" );	// [method][listid]
					lua_pushstring( L, pszClass );	// [method][listid][class]
					nArgs = 2;
				}
				else
				{
					lua_pushstring( L, pszClass );	// [method][class]
				}

				if ( luasrc_pcall( L, nArgs, 1, 0 ) == 0 )
				{
					if ( lua_istable( L, -1 ) )
					{
						lua_getfield( L, -1, s_pSteps[i].pszField );
						if ( lua_type( L, -1 ) == LUA_TSTRING )
							pszValue = lua_tostring( L, -1 );
						else
							pszValue = lua_typename( L, lua_type( L, -1 ) );
						lua_pop( L, 1 );
					}
					else
					{
						pszValue = lua_typename( L, lua_type( L, -1 ) );
					}

					lua_pop( L, 1 );
				}
				else
				{
					pszValue = "(pcall failed)";
				}
			}
			else
			{
				lua_pop( L, 2 );
			}
		}
		else
		{
			lua_pop( L, 1 );
		}

		Msg( "[HL2SB] SMenu: lua chain %s.%s(\"%s\").%s = \"%s\"\n",
			 s_pSteps[i].pszModule, s_pSteps[i].pszMethod, pszClass, s_pSteps[i].pszField, pszValue );
	}
}

static void SMenu_ProbeNames( int nLuaNow )
{
	static bool s_bProbed = false;
	if ( s_bProbed || nLuaNow <= 0 )
		return;

	s_bProbed = true;

	SMenu_DumpLuaChain( "weapon_nyangun" );
	SMenu_DumpLuaChain( "pist_weagon" );

	static const char *s_pClasses[] =
	{
		"weapon_smg1",			// stock HL2: scripts/weapon_smg1.txt printname
		"weapon_357",			// stock HL2
		"weapon_physcannon",	// stock HL2
		"weapon_fists",			// fork SWEP, PrintName is an undefined "#GMOD_Fists"
		"weapon_medkit",		// fork SWEP, "#weapon_medkit"
		"weapon_nyangun",		// addon SWEP, a real PrintName ("Nyan Gun")
		"pist_weagon",			// addon SWEP, a real PrintName ("Sexyness")
		"sent_ball",			// fork SENT, "#sent_ball"
		"npc_zombie",			// stock NPC: nothing better than the class name
	};

	for ( int i = 0; i < ARRAYSIZE( s_pClasses ); ++i )
	{
		SMenuEntry_t entry;
		Q_memset( &entry, 0, sizeof( entry ) );
		SMenu_CopyString( entry.szClass, s_pClasses[i], sizeof( entry.szClass ) );
		SMenu_ResolveDisplayName( entry );

		Msg( "[HL2SB] SMenu: name probe \"%s\" -> \"%s\"\n", s_pClasses[i], entry.szName );
	}
}

//-----------------------------------------------------------------------------
// HL2SB: THE DEFAULT SIZE IS GMOD'S.
//
// GMod's spawn menu is not a small window: `PANEL:Init` opens with
// `self:Dock( FILL )` (spawnmenu.lua:19) and the content is then inset on every
// side by
//
//     MarginX = clamp( (ScrW - 1024) * spawnmenu_border, 25, 256 )
//     MarginY = clamp( (ScrH -  768) * spawnmenu_border, 25, 256 )   // :138-139
//     ... both set to 0 when ScrW < 1024 or ScrH < 768                // :141-145
//
// with spawnmenu_border defaulting to 0.1 (:2).  That is ~80-92% of the screen,
// which is what a spawn menu looks like in GMod.  This frame opened at a fixed
// 820x620 instead, so the user had to drag it bigger by hand every session.
//
// The size is now computed from the same formula, and the frame is placed at the
// margins - which is exactly where GMod's inset leaves it.  The border factor is
// a constant on purpose: this fork's standing rule is "no new console commands
// or cvars", and 0.1 is GMod's own default for it.
//-----------------------------------------------------------------------------
#define SMENU_BORDER_FRACTION	0.1f

static int s_nSMenuFrameW = 0;
static int s_nSMenuFrameH = 0;

//-----------------------------------------------------------------------------
// HL2SB: the two label fonts, in numbers, so the user's "the sidebar text is
// still too big" is checkable from the log rather than by eye.  The sidebar has
// its own (smaller) font - see SMenu_SourceFont - and the grid gets its own too.
//-----------------------------------------------------------------------------
static void SMenu_ProbeFonts( void )
{
	static bool s_bProbed = false;
	if ( s_bProbed )
		return;

	s_bProbed = true;

	ISurface *pSurface = surface();
	if ( !pSurface )
		return;

	const HFont hSource = SMenu_SourceFont();
	const HFont hGrid = SMenu_CellFont();

	int nScreenW = 0, nScreenH = 0;
	pSurface->GetScreenSize( nScreenW, nScreenH );

	Msg( "[HL2SB] SMenu: frame %dx%d, GMod-default size on a %dx%d screen; label fonts - sidebar \"Verdana %d\" tall=%d, grid \"Verdana %d\" tall=%d (scheme \"DefaultVerySmall\" tall=%d)\n",
		 s_nSMenuFrameW, s_nSMenuFrameH, nScreenW, nScreenH,
		 SMENU_SOURCE_FONT_TALL, pSurface->GetFontTall( hSource ),
		 SMENU_CELL_FONT_TALL, pSurface->GetFontTall( hGrid ),
		 pSurface->GetFontTall( SMenu_LabelFont() ) );
}

static void SMenu_BuildEntries( void )
{
	g_SMenuEntries.RemoveAll();

	// HL2SB: one-shot proof that GMod's multi-chunk VPKs really resolve, so the
	// log of any launch answers "is the VPK mounted?" - see
	// SMenu_ProbeGModContent.  Done here rather than in the constructor because
	// this runs after cfg/autoexec.cfg has set con_logfile.
	SMenu_ProbeGModContent();

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

	// HL2SB: the source dimension, resolved AFTER every flag has settled (the
	// Lua lists above may recategorise a class) - one pass per rebuild, and the
	// probes behind it are cached.
	for ( int i = 0; i < g_SMenuEntries.Count(); ++i )
		SMenu_AssignSource( g_SMenuEntries[i] );

	//-----------------------------------------------------------------------------
	// HL2SB: THE DISPLAY NAMES - resolved HERE, after the Lua merge, and not
	// while the class dictionary is being read.
	//
	// Two things make the position matter:
	//
	//   * the Lua registries are what answer for a SWEP/SENT, and they are not
	//     loaded yet when the menu is opened on the very first frame (the log's
	//     "0 Lua" build, which `+smenu` produces every time).  Resolving earlier
	//     cached the class-name fallback and kept it forever: the probe showed
	//     "weapon_nyangun -> Nyangun" instead of the SWEP's own "Nyan Gun".
	//   * so the cache is dropped whenever the number of Lua-registered entries
	//     changes.  The second, proper build then asks Lua again and replaces
	//     every fallback name with the real one.
	//-----------------------------------------------------------------------------
	int nLuaNow = 0;
	for ( int i = 0; i < g_SMenuEntries.Count(); ++i )
	{
		if ( g_SMenuEntries[i].uFlags & SMFLAG_LUA )
			++nLuaNow;
	}

	if ( nLuaNow != s_nSMenuLastNameLuaCount )
	{
		s_nSMenuLastNameLuaCount = nLuaNow;
		g_SMenuNameCache.RemoveAll();
	}

	for ( int i = 0; i < g_SMenuEntries.Count(); ++i )
		SMenu_ResolveDisplayName( g_SMenuEntries[i] );

	SMenu_ProbeNames( nLuaNow );
	SMenu_ProbeFonts();

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
// HL2SB: how many entries each category holds, and the debug/timing switch.
//
// This count is what lets the category tabs be built WITHOUT a single icon panel
// existing - the pages themselves are created and filled lazily, the way GMod's
// spawnmenu does it (gamemodes/sandbox/gamemode/spawnmenu/creationmenu/content/
// content.lua:122-174: a node is populated on first click, once; and
// creationmenu.lua:46-53: the content panel is created in a later frame with
// timer.Simple(0, ...)).  Building every page eagerly in the frame the menu
// opens is what made SMenu hitch.
//-----------------------------------------------------------------------------
static int s_nSMenuCatCounts[SMENU_CAT_COUNT];

// Time we are willing to spend creating cell panels before yielding to the next
// frame.  Cells are cheap individually (the icon probes are cached), so a few
// milliseconds is a whole slice of a page.
#define SMENU_BUILD_BUDGET_MS	4.0f

static bool		g_bSMenuDebug = false;

static double SMenu_Now( void )
{
	return Plat_FloatTime() * 1000.0;
}

static void SMenu_Debug( const char *pszFormat, ... )
{
	if ( !g_bSMenuDebug )
		return;

	char szBuffer[512];
	va_list args;
	va_start( args, pszFormat );
	V_vsnprintf( szBuffer, sizeof( szBuffer ), pszFormat, args );
	va_end( args );

	Msg( "[HL2SB] SMenu: %s\n", szBuffer );
}

//-----------------------------------------------------------------------------
// HL2SB: is the fork's debug switch on?
//
// The switch is `hl2sb_hud_debug`, and it is created from LUA
// (lua/game/client/hl2sb_cl_hudpickup.lua:165 CreateClientConVar).  A
// Lua-created convar is never RegisterConCommand-ed in this fork, so
// cvar->FindVar() cannot see it (AGENTS.md 5.4.3(4)) - which is also why
// game/server/hl2sb_undo.cpp:126's debug check never fires.  Fast path first,
// then ask the Lua state.  Evaluated once per menu open, never per frame.
//-----------------------------------------------------------------------------
static bool SMenu_QueryDebugCvar( void )
{
	ConVar *pVar = cvar ? cvar->FindVar( "hl2sb_hud_debug" ) : NULL;
	if ( pVar )
		return pVar->GetInt() != 0;

	if ( !L )
		return false;

	lua_getglobal( L, "GetConVarNumber" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_pushstring( L, "hl2sb_hud_debug" );

	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
		return false;			// the error object is already popped

	const int nValue = (int)lua_tonumber( L, -1 );
	lua_pop( L, 1 );			// the result

	return nValue != 0;
}

static bool SMenu_EntryMatchesCat( int iCat, const SMenuEntry_t &entry )
{
	if ( ( entry.uFlags & s_SMenuCats[iCat].uMask ) == 0 )
		return false;

	if ( s_SMenuCats[iCat].uNotMask && ( entry.uFlags & s_SMenuCats[iCat].uNotMask ) )
		return false;

	return true;
}

static void SMenu_CountCategories( void )
{
	for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
	{
		if ( s_SMenuCats[i].bModelPage )
		{
			s_nSMenuCatCounts[i] = -1;	// unknown: the page is a files scan
			continue;
		}

		int n = 0;
		for ( int j = 0; j < g_SMenuEntries.Count(); ++j )
		{
			if ( SMenu_EntryMatchesCat( i, g_SMenuEntries[j] ) )
				++n;
		}

		s_nSMenuCatCounts[i] = n;
	}
}

static void SMenu_BuildSources( bool bModelPagePossible )
{
	for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
	{
		g_SMenuCatSources[i].RemoveAll();

		if ( s_SMenuCats[i].bModelPage )
		{
			// The models page can only ever list HL2's models/props_* files, so
			// it has exactly one source and that source IS provably Half-Life 2
			// (SMenu_TODO.md 1.1: never invent "Half-Life 2" for a class we
			// cannot attribute - this page is the one place where it is
			// provable, and it is also where GMod shows the game's own icon).
			if ( bModelPagePossible )
			{
				SMenuSource_t &src = g_SMenuCatSources[i][g_SMenuCatSources[i].AddToTail()];
				Q_memset( &src, 0, sizeof( src ) );
				SMenu_CopyString( src.szId, SMENU_SRC_HL2, sizeof( src.szId ) );
				SMenu_CopyString( src.szLabel, SMENU_SRC_HL2, sizeof( src.szLabel ) );
				SMenu_ResolveSourceIcon( src );
			}
			continue;
		}

		// Only the sources that really have entries in THIS category appear -
		// which is also what makes an empty source impossible (GMod skips an
		// addon with nothing in it: addonprops.lua:101-102).
		for ( int j = 0; j < g_SMenuEntries.Count(); ++j )
		{
			if ( !SMenu_EntryMatchesCat( i, g_SMenuEntries[j] ) )
				continue;

			SMenuSource_t *pSrc = SMenu_FindOrAddSource( i, g_SMenuEntries[j].szSource );

			if ( pSrc )
				pSrc->nCount++;
		}

		g_SMenuCatSources[i].Sort( SMenu_SortSources );
	}
}

//-----------------------------------------------------------------------------
// HL2SB: one icon-grid cell - thumbnail + display name, click = spawn.
//
// Not vgui::ImageButton/ImagePanel: scheme()->GetImage() prepends "vgui/" to
// every name (vgui2/src/Scheme.cpp:1260), so it can only reach
// materials/vgui/... while GMod's thumbnails live in materials/entities/.
// Drawing through ISurface::DrawSetTextureFile addresses materials/ directly.
//-----------------------------------------------------------------------------

// The cell's default size, and the widest it may grow while chasing a long
// name.  Beyond the cap a name is shortened with an explicit "..." rather than
// stealing the whole row - the genuine last resort the user asked for, not the
// default (see CSMList::SetWantedCellWidth).
#define SMENU_CELL_W		76
#define SMENU_CELL_H		80
#define SMENU_CELL_MAX_W	180

// The widest the sidebar may get while chasing a long source label.
#define SMENU_SIDE_MAX_W	320

class CSMIconButton : public vgui::Panel
{
	typedef vgui::Panel BaseClass;
public:
	CSMIconButton( vgui::Panel *pParent, const SMenuEntry_t &entry ) : BaseClass( pParent, "SMenuIcon" )
	{
		SMenu_CopyString( m_szClass, entry.szClass, sizeof( m_szClass ) );
		SMenu_CopyString( m_szFixedCmd, entry.szFixedCmd, sizeof( m_szFixedCmd ) );
		SMenu_CopyString( m_szMaterial, entry.szMaterial, sizeof( m_szMaterial ) );
		SMenu_CopyString( m_szName, entry.szName[0] ? entry.szName : entry.szClass, sizeof( m_szName ) );
		m_bWeapon = ( entry.uFlags & SMFLAG_WEAPON ) != 0;
		m_bHover = false;
		m_bBound = false;
		m_hFont = INVALID_FONT;
		m_szDrawn[0] = 0;
		m_nTexture = surface()->CreateNewTextureID();

		SetSize( SMENU_CELL_W, SMENU_CELL_H );
		SetPaintBackgroundEnabled( false );
		SetMouseInputEnabled( true );
	}

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		// The cell uses its own, unscaled font sized for this cell (see
		// SMenu_CellFont); the scheme font is only the fallback if that one could
		// not be built.
		m_hFont = SMenu_CellFont();
		if ( m_hFont == INVALID_FONT )
			m_hFont = pScheme->GetFont( "DefaultVerySmall", true );

		UpdateDrawnLabel();
	}

	virtual void OnSizeChanged( int nNewWide, int nNewTall )
	{
		BaseClass::OnSizeChanged( nNewWide, nNewTall );

		// The grid decides how wide this cell is (CSMList::SetWantedCellWidth),
		// so the label is (re)shortened whenever that changes - never per frame.
		UpdateDrawnLabel();
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

		// HL2SB: the label is DRAWN HERE rather than handed to a vgui::Label,
		// because a Label clips at its own bounds - that is what cut the user's
		// names in half.  The text was already shortened to this cell's width
		// (UpdateDrawnLabel), it is measured only to centre it, and a name that
		// had to be shortened ends in an explicit "..." rather than mid-glyph.
		if ( m_szName[0] && m_hFont != INVALID_FONT && g_pVGuiLocalize )
		{
			if ( !m_szDrawn[0] )
				UpdateDrawnLabel();

			if ( m_szDrawn[0] )
			{
				wchar_t wszLabel[192];
				g_pVGuiLocalize->ConvertANSIToUnicode( m_szDrawn, wszLabel, sizeof( wszLabel ) );

				int nTextW = 0, nTextH = 0;
				surface()->GetTextSize( m_hFont, wszLabel, nTextW, nTextH );

				int x = ( w - nTextW ) / 2;
				if ( x < 2 )
					x = 2;

				int y = h - nTextH - 3;
				if ( y < 2 )
					y = 2;

				surface()->DrawSetTextFont( m_hFont );
				surface()->DrawSetTextColor( 255, 255, 255, 255 );
				surface()->DrawSetTextPos( x, y );
				surface()->DrawPrintText( wszLabel, V_wcslen( wszLabel ) );
			}
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
	// Shorten the display name to this cell's current width, once per size or
	// font change.  The full name is never lost: the shortening is a deliberate
	// "..." on a name that genuinely does not fit the cell the grid handed us.
	void UpdateDrawnLabel( void )
	{
		SMenu_ShortenText( m_hFont, m_szName, GetWide() - 4, m_szDrawn, sizeof( m_szDrawn ) );
	}

	HFont		m_hFont;
	char		m_szClass[64];
	char		m_szName[128];
	char		m_szDrawn[160];
	char		m_szFixedCmd[SMENU_FIXEDCMD_LEN];
	char		m_szMaterial[128];
	int			m_nTexture;
	bool		m_bBound;
	bool		m_bHover;
	bool		m_bWeapon;
};


//-----------------------------------------------------------------------------
// HL2SB: one page of icon cells - created empty, filled in slices.
//
// This mirrors GMod: a category node is populated the first time it is clicked
// and never again unless its content changed (content.lua:122-174), and the
// expensive part is deferred out of the frame that builds the menu
// (creationmenu.lua:46-53).  Here the work is additionally cut into
// SMENU_BUILD_BUDGET_MS slices, so even a page with hundreds of cells cannot
// stall a frame.
//
// PanelListPanel already lays its children out as a wrapping grid and owns the
// scrollbar, so this class only tells it how many columns fit.
//-----------------------------------------------------------------------------
class CSMList : public vgui::PanelListPanel
{
	typedef vgui::PanelListPanel BaseClass;
public:
	CSMList( vgui::Panel *pParent, const char *pName ) : BaseClass( pParent, pName )
	{
		m_nColumns = 0;
		m_nBuildCursor = 0;
		m_nCellsAdded = 0;
		m_bPageDirty = true;
		m_bPageBuilt = false;
		m_bHasItems = false;
		m_flBuildStart = 0.0;
		m_nCellWanted = SMENU_CELL_W;
		m_szSource[0] = 0;
		SetFirstColumnWidth( 0 );
		SetNumColumns( 1 );
		SetVerticalBufferPixels( 2 );
	}

	//-----------------------------------------------------------------------------
	// HL2SB: THE SOURCE DIMENSION.  A page is the (category, source) pair the
	// left sidebar selected; the machinery around it is unchanged - the page is
	// still created empty, still filled in 4 ms slices the first time it is
	// shown, and still never mutated while the menu is opening.
	//
	// Selecting a different source is a user action (a click on the sidebar), so
	// it is the one place a built page is refilled; that is what GMod does when a
	// source node is selected (it fills that node's ViewPanel:
	// gameprops.lua:87-120, addonprops.lua:105-131).
	//-----------------------------------------------------------------------------
	void SetSourceFilter( const char *pszSource )
	{
		if ( !Q_stricmp( m_szSource, pszSource ) )
			return;

		SMenu_CopyString( m_szSource, pszSource, sizeof( m_szSource ) );
		InvalidatePage();
	}

	const char *GetSourceFilter( void ) const	{ return m_szSource; }

	void AddEntry( const SMenuEntry_t &entry )
	{
		CSMIconButton *pButton = new CSMIconButton( this, entry );
		pButton->SetSize( SMENU_CELL_W, SMENU_CELL_H );

		// The cell's own label decides the column width - so it is measured here
		// as well, which is what gets the MODELS page (filled in one go from a
		// file scan, without the class-page pre-pass) a readable width too.
		if ( m_nCellWanted < SMENU_CELL_MAX_W )
		{
			const int nWidth = SMenu_TextWidth( SMenu_CellFont(), entry.szName ) + 8;
			if ( nWidth > m_nCellWanted )
				SetWantedCellWidth( nWidth );
		}

		AddItem( NULL, pButton );
		m_bHasItems = true;
	}

	// HL2SB: SIZE THE CELLS TO THE LABELS.
	//
	// PanelListPanel gives every child the same width, computed from
	// SetNumColumns (vgui2/vgui_controls/PanelListPanel.cpp:321,341), so a page
	// can only be laid out as uniformly sized cells - which means the column
	// count IS the lever for "does the longest name on this page fit".  The
	// widest label on the page therefore decides the column count, and every
	// name fits in full instead of being clipped (the old code used a fixed
	// 76 px cell regardless of the text).
	void SetWantedCellWidth( int nWidth )
	{
		if ( nWidth < SMENU_CELL_W )
			nWidth = SMENU_CELL_W;
		if ( nWidth > SMENU_CELL_MAX_W )
			nWidth = SMENU_CELL_MAX_W;

		if ( nWidth == m_nCellWanted )
			return;

		m_nCellWanted = nWidth;
		m_nColumns = 0;			// force the column count to be recomputed
		InvalidateLayout( true );
	}

	// Mark the page as needing (re)population.  Nothing is destroyed or created
	// here on purpose - that is what keeps the menu-open frame cheap.
	void InvalidatePage( void )
	{
		m_bPageDirty = true;
		m_bPageBuilt = false;
		m_nBuildCursor = 0;
		m_nCellWanted = SMENU_CELL_W;	// re-measure for the new source
		m_nColumns = 0;
	}

	bool IsPageBuilt( void ) const		{ return m_bPageBuilt; }

	// Mark the page as finished without touching its items (used by the models
	// page, whose content comes from a files scan rather than from
	// g_SMenuEntries).
	void FinishPage( void )
	{
		m_bPageBuilt = true;
		m_bPageDirty = false;
		m_nBuildCursor = g_SMenuEntries.Count();
	}

	// Fill at most flBudgetMs worth of cells for category nCat.  Returns true
	// when the page is complete.  The caller drives this once per frame.
	bool BuildSomeCells( float flBudgetMs, int nCat )
	{
		if ( m_bPageBuilt )
			return true;

		if ( m_bPageDirty )
		{
			if ( m_bHasItems )
				DeleteAllItems();

			m_bHasItems = false;
			m_nBuildCursor = 0;
			m_nCellsAdded = 0;
			m_bPageDirty = false;
			m_flBuildStart = SMenu_Now();

			// HL2SB: how wide does THIS page need to be?  One pass over the
			// entries this page will show measures every name, and the widest
			// one sets the column count (see SetWantedCellWidth).  It runs once
			// per page build, never per cell.
			HFont hFont = SMenu_CellFont();
			if ( hFont != INVALID_FONT )
			{
				for ( int i = 0; i < g_SMenuEntries.Count(); ++i )
				{
					const SMenuEntry_t &entry = g_SMenuEntries[i];
					if ( !SMenu_EntryMatchesCat( nCat, entry ) || !MatchesSource( entry ) )
						continue;

					SetWantedCellWidth( SMenu_TextWidth( hFont, entry.szName ) + 8 );
				}
			}
		}

		const int nTotal = g_SMenuEntries.Count();
		const double flSliceStart = SMenu_Now();
		int nThisSlice = 0;

		while ( m_nBuildCursor < nTotal )
		{
			const SMenuEntry_t &entry = g_SMenuEntries[m_nBuildCursor];

			if ( SMenu_EntryMatchesCat( nCat, entry ) && MatchesSource( entry ) )
			{
				AddEntry( entry );
				++m_nCellsAdded;
				++nThisSlice;

				// Always add at least one cell, then stop once the budget is gone.
				if ( flBudgetMs > 0.0f && ( SMenu_Now() - flSliceStart ) >= flBudgetMs )
				{
					++m_nBuildCursor;
					break;
				}
			}

			++m_nBuildCursor;
		}

		if ( m_nBuildCursor >= nTotal )
		{
			m_bPageBuilt = true;
			SMenu_Debug( "page \"%s\" / \"%s\" built: %d cells from %d entries, %.2f ms",
						 s_SMenuCats[nCat].pszTitle, m_szSource, m_nCellsAdded, nTotal, SMenu_Now() - m_flBuildStart );
			return true;
		}

		SMenu_Debug( "page \"%s\" / \"%s\": +%d cells (%d/%d entries scanned, %.2f ms)",
					 s_SMenuCats[nCat].pszTitle, m_szSource, nThisSlice, m_nBuildCursor, nTotal, SMenu_Now() - flSliceStart );
		return false;
	}

	virtual void PerformLayout()
	{
		int w = 0, h = 0;
		GetSize( w, h );
		NOTE_UNUSED( h );

		// HL2SB: the column count comes from the width this page's labels
		// actually need (SetWantedCellWidth), not from a fixed cell size, so a
		// long name buys itself a wider column instead of being cut off.
		int nColumns = ( w - 30 ) / m_nCellWanted;
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
	// Empty source = "no source dimension yet", which matches everything; the
	// caller always sets a real one before the page is first built.
	bool MatchesSource( const SMenuEntry_t &entry ) const
	{
		if ( !m_szSource[0] )
			return true;

		return !Q_stricmp( m_szSource, entry.szSource );
	}

	int		m_nColumns;
	int		m_nCellWanted;	// width this page's labels need (see FitColumnsToLabels)
	int		m_nBuildCursor;
	int		m_nCellsAdded;
	bool	m_bPageDirty;
	bool	m_bPageBuilt;
	bool	m_bHasItems;
	double	m_flBuildStart;
	char	m_szSource[128];
};

//-----------------------------------------------------------------------------
// HL2SB: the widgets of the two-axis layout.
//
//   * CSMTabButton    - one TOP tab (a category).  Highlights while active.
//   * CSMTabBar       - the tab strip: lays its buttons out left to right and
//                       forwards their "Command" messages to the menu, because
//                       Panel::OnCommand is part of the base message map and
//                       does NOT bubble up to the frame.
//   * CSMSourceButton - one LEFT sidebar row: the source's 16x16 icon plus its
//                       name, the way GMod's sidebar DTree shows a game's
//                       "games/16/<name>.png" and an addon's icon16/bricks.png
//                       (gameprops.lua:146, addonprops.lua:104).
//   * CSMSourceList   - the sidebar itself, one row per source of the current
//                       category.
//
// Both rows are hand-drawn panels rather than vgui::Button for the reason
// CSMIconButton already is: scheme()->GetImage() prepends "vgui/" to every name
// (vgui2/src/Scheme.cpp:1260) and can therefore only reach materials/vgui/*,
// while these icons live in materials/icon16/ and materials/games/16/.  Drawing
// through ISurface::DrawSetTextureFile addresses materials/ directly.
//-----------------------------------------------------------------------------
class CSMenu;

class CSMTabButton : public vgui::Button
{
	typedef vgui::Button BaseClass;
public:
	CSMTabButton( vgui::Panel *pParent, const char *pName, const char *pText, const char *pCmd )
		: BaseClass( pParent, pName, pText, pParent, pCmd )
	{
		m_bActive = false;
	}

	void SetActiveTab( bool bActive )
	{
		if ( m_bActive == bActive )
			return;

		m_bActive = bActive;
		Repaint();
	}

	virtual void Paint()
	{
		// A flat wash behind the active tab: the strip has to say which category
		// the grid belongs to, and a plain vgui::Button has no selected look.
		if ( m_bActive )
		{
			int w = 0, h = 0;
			GetSize( w, h );
			surface()->DrawSetColor( 255, 255, 255, 40 );
			surface()->DrawFilledRect( 0, 0, w, h );
		}

		BaseClass::Paint();
	}

private:
	bool	m_bActive;
};

#define SMENU_SRCROW_H	20

class CSMSourceButton : public vgui::Panel
{
	typedef vgui::Panel BaseClass;
public:
	CSMSourceButton( CSMenu *pOwner, vgui::Panel *pParent, const SMenuSource_t &src, int nIndex, bool bSelected )
		: BaseClass( pParent, "SMenuSource" )
	{
		m_pOwner = pOwner;
		m_nIndex = nIndex;
		m_bSelected = bSelected;
		m_bHover = false;
		m_bBound = false;
		m_hFont = INVALID_FONT;
		m_nTexture = surface()->CreateNewTextureID();

		SMenu_CopyString( m_szMaterial, src.szMaterial, sizeof( m_szMaterial ) );
		SMenu_CopyString( m_szLabel, src.szLabel, sizeof( m_szLabel ) );
		m_hFont = INVALID_FONT;
		m_szDrawn[0] = 0;

		SetSize( 160, SMENU_SRCROW_H );
		SetPaintBackgroundEnabled( false );
		SetMouseInputEnabled( true );
	}

	// The source's display name, so the sidebar can be widened to hold it.
	const char *GetLabel( void ) const			{ return m_szLabel; }

	void SetSelected( bool bSelected )
	{
		if ( m_bSelected == bSelected )
			return;

		m_bSelected = bSelected;
		Repaint();
	}

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		// The sidebar uses its own smaller font (SMenu_SourceFont); the scheme
		// font is only the fallback if that one could not be built.
		m_hFont = SMenu_SourceFont();
		if ( m_hFont == INVALID_FONT )
			m_hFont = pScheme->GetFont( "DefaultVerySmall", true );

		UpdateDrawnLabel();
	}

	virtual void OnSizeChanged( int nNewWide, int nNewTall )
	{
		BaseClass::OnSizeChanged( nNewWide, nNewTall );

		UpdateDrawnLabel();
	}

	virtual void Paint()
	{
		int w = 0, h = 0;
		GetSize( w, h );

		if ( m_bSelected )
		{
			surface()->DrawSetColor( 255, 255, 255, 48 );
			surface()->DrawFilledRect( 0, 0, w, h );
		}
		else if ( m_bHover )
		{
			surface()->DrawSetColor( 255, 255, 255, 24 );
			surface()->DrawFilledRect( 0, 0, w, h );
		}

		// The source icon is a 16x16 image; it is bound on first paint, so a
		// sidebar nobody looks at decodes nothing (the same rule as the cells).
		// An empty name means "no icon on disk" - the row then prints its source
		// name only, never a purple square.
		if ( m_szMaterial[0] && m_nTexture != -1 )
		{
			if ( !m_bBound )
			{
				surface()->DrawSetTextureFile( m_nTexture, m_szMaterial, true, false );
				m_bBound = true;
			}

			surface()->DrawSetColor( 255, 255, 255, 255 );
			surface()->DrawSetTexture( m_nTexture );
			surface()->DrawTexturedRect( 2, 2, 18, 18 );
		}

		// HL2SB: same rule as the grid cells - the label is measured and drawn
		// here, so it is never clipped by a rectangle that was guessed at.  The
		// sidebar is widened for the longest name (CSMenu::PerformLayout), so
		// the "..." is only reached if a single source name is absurd.
		if ( m_szLabel[0] && m_hFont != INVALID_FONT && g_pVGuiLocalize )
		{
			if ( !m_szDrawn[0] )
				UpdateDrawnLabel();

			if ( m_szDrawn[0] )
			{
				wchar_t wszLabel[192];
				g_pVGuiLocalize->ConvertANSIToUnicode( m_szDrawn, wszLabel, sizeof( wszLabel ) );

				int nTextW = 0, nTextH = 0;
				surface()->GetTextSize( m_hFont, wszLabel, nTextW, nTextH );
				NOTE_UNUSED( nTextW );

				int y = ( h - nTextH ) / 2;
				if ( y < 0 )
					y = 0;

				surface()->DrawSetTextFont( m_hFont );
				surface()->DrawSetTextColor( 255, 255, 255, 255 );
				surface()->DrawSetTextPos( 22, y );
				surface()->DrawPrintText( wszLabel, V_wcslen( wszLabel ) );
			}
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

	virtual void OnMouseReleased( vgui::MouseCode code );

private:
	// Shorten the source name to this row's width, once per size or font change.
	void UpdateDrawnLabel( void )
	{
		SMenu_ShortenText( m_hFont, m_szLabel, GetWide() - 28, m_szDrawn, sizeof( m_szDrawn ) );
	}

	CSMenu			*m_pOwner;
	HFont			m_hFont;
	char			m_szMaterial[128];
	char			m_szLabel[128];
	char			m_szDrawn[160];
	int				m_nIndex;
	int				m_nTexture;
	bool			m_bBound;
	bool			m_bHover;
	bool			m_bSelected;
};

class CSMTabBar : public vgui::Panel
{
	typedef vgui::Panel BaseClass;
public:
	CSMTabBar( vgui::Panel *pParent, const char *pName ) : BaseClass( pParent, pName )
	{
		m_pOwner = NULL;
		SetPaintBackgroundEnabled( false );
	}

	void SetOwner( CSMenu *pOwner ) { m_pOwner = pOwner; }

	// Rough text metric: the strip must not need a font handle before
	// ApplySchemeSettings has run, and the titles are short.
	static int TabWidth( const char *pszText )
	{
		int nWidth = 16 + 7 * ( pszText ? Q_strlen( pszText ) : 0 );

		if ( nWidth < 64 ) nWidth = 64;
		if ( nWidth > 170 ) nWidth = 170;

		return nWidth;
	}

	// How tall the strip has to be to hold every button: one row unless the
	// frame is dragged narrow.
	int GetPreferredHeight( int nWidth )
	{
		int x = 0, nRows = 1;

		for ( int i = 0; i < GetChildCount(); ++i )
		{
			vgui::Panel *pChild = GetChild( i );
			if ( !pChild )
				continue;

			int w = 0, h = 0;
			pChild->GetSize( w, h );

			if ( x > 0 && x + w > nWidth )
			{
				nRows++;
				x = 0;
			}

			x += w + 2;
		}

		return nRows * 22 + ( nRows - 1 ) * 2;
	}

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		int w = 0, h = 0;
		GetSize( w, h );
		NOTE_UNUSED( h );

		int x = 0, y = 0;

		for ( int i = 0; i < GetChildCount(); ++i )
		{
			vgui::Panel *pChild = GetChild( i );
			if ( !pChild )
				continue;

			int cw = 0, ch = 0;
			pChild->GetSize( cw, ch );

			if ( x > 0 && x + cw > w )
			{
				x = 0;
				y += ch + 2;
			}

			pChild->SetPos( x, y );
			x += cw + 2;
		}
	}

	virtual void OnCommand( const char *command );

private:
	CSMenu	*m_pOwner;
};

class CSMSourceList : public vgui::PanelListPanel
{
	typedef vgui::PanelListPanel BaseClass;
public:
	CSMSourceList( vgui::Panel *pParent, const char *pName ) : BaseClass( pParent, pName )
	{
		m_pOwner = NULL;
		SetFirstColumnWidth( 0 );
		SetNumColumns( 1 );
		SetVerticalBufferPixels( 2 );
	}

	void SetOwner( CSMenu *pOwner ) { m_pOwner = pOwner; }

	void AddSourceRow( CSMSourceButton *pRow )
	{
		m_pRows.AddToTail( pRow );
		AddItem( NULL, pRow );
	}

	void ClearRows( void )
	{
		DeleteAllItems();
		m_pRows.RemoveAll();
	}

	void SetSelection( int nIndex )
	{
		for ( int i = 0; i < m_pRows.Count(); ++i )
			m_pRows[i]->SetSelected( i == nIndex );
	}

	// HL2SB: how wide the sidebar has to be to print every source of the current
	// category in full.  Measured from the real rows, so the frame can size the
	// sidebar to its content instead of guessing (which is what clipped
	// "addons/the ultimate..." mid-word). Returns 0 before any row exists.
	int GetWantedWidth( void )
	{
		// Measured with the SAME font the rows draw with, or the sidebar would be
		// sized for text nobody sees.
		HFont hFont = SMenu_SourceFont();
		if ( hFont == INVALID_FONT )
			hFont = SMenu_LabelFont( this );
		if ( hFont == INVALID_FONT )
			return 0;

		int nWidest = 0;
		for ( int i = 0; i < m_pRows.Count(); ++i )
		{
			const int nWidth = SMenu_TextWidth( hFont, m_pRows[i]->GetLabel() ) + 30;
			if ( nWidth > nWidest )
				nWidest = nWidth;
		}

		return nWidest;
	}

	virtual void OnCommand( const char *command );

private:
	CSMenu							*m_pOwner;
	CUtlVector< CSMSourceButton * >	m_pRows;
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

		int nScreenW = 0, nScreenH = 0;
		surface()->GetScreenSize( nScreenW, nScreenH );

		int nMarginX = (int)( ( nScreenW - 1024 ) * SMENU_BORDER_FRACTION );
		int nMarginY = (int)( ( nScreenH - 768 ) * SMENU_BORDER_FRACTION );

		if ( nMarginX < 25 ) nMarginX = 25;
		if ( nMarginX > 256 ) nMarginX = 256;
		if ( nMarginY < 25 ) nMarginY = 25;
		if ( nMarginY > 256 ) nMarginY = 256;

		// "At this size we can't spare any space for emptiness" (spawnmenu.lua:141).
		if ( nScreenW < 1024 || nScreenH < 768 )
		{
			nMarginX = 0;
			nMarginY = 0;
		}

		s_nSMenuFrameW = nScreenW - 2 * nMarginX;
		s_nSMenuFrameH = nScreenH - 2 * nMarginY;

		if ( s_nSMenuFrameW < 640 ) s_nSMenuFrameW = 640;
		if ( s_nSMenuFrameH < 480 ) s_nSMenuFrameH = 480;

		SetSize( s_nSMenuFrameW, s_nSMenuFrameH );
		SetPos( ( nScreenW - s_nSMenuFrameW ) / 2, ( nScreenH - s_nSMenuFrameH ) / 2 );

		SetMinimizeButtonVisible( false );
		SetMaximizeButtonVisible( false );

		m_szBuiltLevel[0] = 0;
		m_uBuiltHash = 0;
		m_bBuiltOnce = false;
		m_nCurrentCat = 0;
		m_nSidebarCat = -1;
		m_bSidebarDirty = true;
		m_bModelPagePossible = false;
		m_bModelPageBuilt = false;

		// GMod's structure: a tab strip of CATEGORIES on top, the SOURCE list
		// down the left, the icon grid on the right.
		m_pTabs = new CSMTabBar( this, "CategoryTabs" );
		m_pTabs->SetOwner( this );

		m_pSourceList = new CSMSourceList( this, "SourceList" );
		m_pSourceList->SetOwner( this );

		m_pGridHost = new vgui::Panel( this, "GridHost" );

		// One page per category.  The page buttons themselves are (re)built by
		// RebuildTabs(), which skips every category that came up empty - an
		// empty category must not reserve a dead tab (GMod hides those too).
		// The SOURCES of the selected category are a second, independent list.
		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			m_bCatVisible[i] = false;
			m_pTabButtons[i] = NULL;
			m_szSelSource[i][0] = 0;

			char szName[32];
			Q_snprintf( szName, sizeof( szName ), "Page%d", i );
			m_pPages[i] = new CSMList( m_pGridHost, szName );
			m_pPages[i]->SetVisible( false );
		}

		// HL2SB: tick EVERY frame while a page is being filled in slices.  The
		// per-frame cost when the menu is closed is a couple of comparisons, and
		// it means a page completes over a handful of frames instead of one
		// slice per 100 ms.
		vgui::ivgui()->AddTickSignal( GetVPanel(), 0 );

		SetMoveable( true );
		SetVisible( false );
		SetSizeable( true );

		// GMod-style: the spawn menu must NOT capture keyboard input, otherwise
		// vgui treats the popup as the key focus and swallows WASD before the
		// engine can fire +forward/+back/+moveleft/+moveright, so the player
		// cannot walk while the menu is open.  Mouse input stays enabled so the
		// tabs, the source list and the icons still respond to clicks.
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

		// The top strip takes what its buttons need (one row unless the frame is
		// dragged narrow); the sidebar keeps the width it always had - unless
		// its current source names need more, in which case it grows so they are
		// printed in full instead of being cut mid-word.
		const int nTabH = m_pTabs->GetPreferredHeight( w - 8 ) + 4;

		int nSideW = w / 4;
		if ( nSideW < 170 )
			nSideW = 170;
		if ( nSideW > 260 )
			nSideW = 260;

		if ( m_pSourceList )
		{
			const int nWanted = m_pSourceList->GetWantedWidth();
			if ( nWanted > nSideW )
				nSideW = MIN( nWanted, SMENU_SIDE_MAX_W );
		}

		int nBodyH = h - nTabH - 12;
		if ( nBodyH < 10 )
			nBodyH = 10;

		m_pTabs->SetBounds( x + 4, y + 4, w - 8, nTabH );
		m_pSourceList->SetBounds( x + 4, y + 4 + nTabH + 4, nSideW, nBodyH );
		m_pGridHost->SetBounds( x + nSideW + 10, y + 4 + nTabH + 4, w - nSideW - 14, nBodyH );

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

		// Top tab: pick the category (and with it, the source list to show).
		if ( !Q_strnicmp( command, "smcat ", 6 ) )
		{
			const int nCat = atoi( command + 6 );

			if ( nCat >= 0 && nCat < (int)SMENU_CAT_COUNT )
				ShowCategory( nCat );

			return;
		}

		// Left sidebar: pick the source inside the current category.
		if ( !Q_strnicmp( command, "smsrc ", 6 ) )
		{
			const int nSrc = atoi( command + 6 );

			if ( m_nCurrentCat >= 0 && m_nCurrentCat < (int)SMENU_CAT_COUNT &&
				 nSrc >= 0 && nSrc < g_SMenuCatSources[m_nCurrentCat].Count() )
			{
				ShowSource( m_nCurrentCat, nSrc );
			}
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

			// HL2SB: pay for the visible page in slices so that no single frame
			// builds a whole grid.  GMod gets the same effect by populating a
			// category only when its node is first clicked (content.lua:122-174)
			// and by deferring the tab population out of the construction frame
			// with timer.Simple(0, ...) (creationmenu.lua:46-53).
			StepCurrentPage();
		}

		SetVisible( bVisible );
	}

private:
	// Re-reads every source and, only when the resulting entry set changed,
	// rebuilds the pages and the category tree.  Returns true when it rebuilt.
	bool RebuildIfNeeded( void )
	{
		g_bSMenuDebug = SMenu_QueryDebugCvar();

		const double flStart = SMenu_Now();

		SMenu_BuildEntries();

		const double flBuilt = SMenu_Now();

		const char *pszLevel = engine->GetLevelName();
		if ( !pszLevel )
			pszLevel = "";

		const unsigned int uHash = SMenu_HashEntries();
		const bool bLevelChanged = Q_stricmp( m_szBuiltLevel, pszLevel ) != 0;

		SMenu_Debug( "open: %d entries, hash %s (%s)",
					 g_SMenuEntries.Count(),
					 ( uHash == m_uBuiltHash ) ? "unchanged" : "CHANGED",
					 bLevelChanged ? "level changed" : "same level" );

		if ( m_bBuiltOnce && uHash == m_uBuiltHash && !bLevelChanged )
			return false;		// nothing changed: leave the live panels alone

		Q_strncpy( m_szBuiltLevel, pszLevel, sizeof( m_szBuiltLevel ) );
		m_uBuiltHash = uHash;

		// HL2SB: NO panel work here.  Every page is merely invalidated and will
		// repopulate itself in slices the next time it is shown - that is what
		// removes the hitch from opening the menu.  (The old code created every
		// cell of every category, and scanned models/props_* on top of that, in
		// this very frame.)
		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
			m_pPages[i]->InvalidatePage();

		m_bModelPageBuilt = false;

		SMenu_CountCategories();

		// Is the models page even possible?  It lists models/props_* that have a
		// per-model icon under materials/vgui/smenu/models/.  When that directory
		// is absent the page can never hold anything, and skipping it removes the
		// whole models/ directory walk from the open path.  One file system call
		// decides it.
		FileFindHandle_t hProbe = FILESYSTEM_INVALID_FIND_HANDLE;
		const char *pszProbe = filesystem->FindFirstEx( "materials/vgui/smenu/models/*", "GAME", &hProbe );
		m_bModelPagePossible = ( pszProbe != NULL );
		if ( hProbe != FILESYSTEM_INVALID_FIND_HANDLE )
			filesystem->FindClose( hProbe );

		const double flCounted = SMenu_Now();

		// HL2SB: the source lists of every category.  This is not panel work -
		// it is a pass over g_SMenuEntries - and it must happen AFTER the models
		// probe above, because that probe decides whether the models page has its
		// single "Half-Life 2" source at all.
		SMenu_BuildSources( m_bModelPagePossible );

		const double flSourced = SMenu_Now();

		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			for ( int j = 0; j < g_SMenuCatSources[i].Count(); ++j )
				SMenu_Debug( "source: %-14s / %-14s -> %d entries, icon \"%s\"",
							 s_SMenuCats[i].pszTitle, g_SMenuCatSources[i][j].szId,
							 g_SMenuCatSources[i][j].nCount, g_SMenuCatSources[i][j].szMaterial );
		}

		m_bBuiltOnce = true;

		// The sidebar belongs to the selected category and its content just
		// changed, so it is rebuilt on the next ShowCategory - not here.
		m_bSidebarDirty = true;
		m_nSidebarCat = -1;

		RebuildTabs();

		const double flEnd = SMenu_Now();

		SMenu_Debug( "open: %d entries - read %.2f ms, count %.2f ms, sources %.2f ms, tabs %.2f ms, TOTAL %.2f ms (pages are lazy)",
					 g_SMenuEntries.Count(), flBuilt - flStart, flCounted - flBuilt, flSourced - flCounted,
					 flEnd - flSourced, flEnd - flStart );

		return true;
	}

	//-----------------------------------------------------------------------------
	// HL2SB: one TOP tab per non-empty category.  A category with nothing in it
	// (Vehicles on a server that does not publish its entity list, for instance)
	// is left out instead of showing a dead tab - the same rule GMod applies to
	// an empty spawnlist.  Built from the per-category COUNTS, so it needs no
	// page to exist yet.
	//-----------------------------------------------------------------------------
	void RebuildTabs( void )
	{
		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			m_bCatVisible[i] = false;

			if ( m_pTabButtons[i] )
			{
				m_pTabButtons[i]->SetParent( (vgui::Panel *)NULL );
				delete m_pTabButtons[i];
				m_pTabButtons[i] = NULL;
			}
		}

		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			if ( s_SMenuCats[i].bModelPage )
			{
				if ( !m_bModelPagePossible )
					continue;
			}
			else if ( s_nSMenuCatCounts[i] <= 0 )
			{
				continue;
			}

			m_bCatVisible[i] = true;

			char szName[32];
			Q_snprintf( szName, sizeof( szName ), "Cat%d", i );

			char szCmd[32];
			Q_snprintf( szCmd, sizeof( szCmd ), "smcat %d", i );

			CSMTabButton *pButton = new CSMTabButton( m_pTabs, szName, s_SMenuCats[i].pszTitle, szCmd );
			pButton->SetContentAlignment( vgui::Label::a_center );
			pButton->SetSize( CSMTabBar::TabWidth( s_SMenuCats[i].pszTitle ), 22 );
			m_pTabButtons[i] = pButton;
		}

		m_pTabs->InvalidateLayout( true );
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
				SMenu_CopyString( entry.szSource, SMENU_SRC_HL2, sizeof( entry.szSource ) );
				Q_strncpy( entry.szClass, szModel, sizeof( entry.szClass ) );
				// HL2SB: a model cell is labelled with the model's own file
				// stem, the way GMod's spawnicon.lua:257 does it (the tooltip
				// there is the model's file name without ".mdl").
				SMenu_CopyString( entry.szName, szBase, sizeof( entry.szName ) );
				Q_snprintf( entry.szFixedCmd, sizeof( entry.szFixedCmd ), "prop_physics_create %s", szModel );
				Q_snprintf( entry.szMaterial, sizeof( entry.szMaterial ), "vgui/smenu/models/%s", szModel );

				pPage->AddEntry( entry );
			}
			g_pFullFileSystem->FindClose( mh );
		}
		g_pFullFileSystem->FindClose( fh );
	}

	//-----------------------------------------------------------------------------
	// HL2SB: advance the visible page by one slice.  Driven once per frame from
	// OnTick(), and once right after a category is selected so a click feels
	// immediate; the rest of the grid fills in over the next frames.
	//-----------------------------------------------------------------------------
	void StepCurrentPage( void )
	{
		if ( m_nCurrentCat < 0 || m_nCurrentCat >= (int)SMENU_CAT_COUNT )
			return;

		if ( !m_bCatVisible[m_nCurrentCat] )
			return;

		CSMList *pPage = m_pPages[m_nCurrentCat];
		if ( !pPage || pPage->IsPageBuilt() )
			return;

		if ( s_SMenuCats[m_nCurrentCat].bModelPage )
		{
			if ( m_bModelPageBuilt )
				return;

			// The models page is a FILES scan, not a filter over g_SMenuEntries,
			// so it cannot use the cell cursor.  It runs in one go the first time
			// the page is shown - gated behind the probe in RebuildIfNeeded(), so
			// an install without per-model icons never walks the directory at all.
			// GMod also scans a game's models synchronously when its node is
			// selected (gameprops.lua:100-111).
			const double flStart = SMenu_Now();
			SMenu_BuildModelPage( pPage );
			pPage->FinishPage();
			m_bModelPageBuilt = true;
			SMenu_Debug( "models page built: %d cells in %.2f ms", pPage->GetItemCount(), SMenu_Now() - flStart );
			return;
		}

		pPage->BuildSomeCells( SMENU_BUILD_BUDGET_MS, m_nCurrentCat );
	}

	//-----------------------------------------------------------------------------
	// HL2SB: show the source list of category nCat - one row per source that
	// actually HAS entries in it (SMenu_BuildSources built that list; the icons
	// were resolved there).  The list is only rebuilt when the category changed
	// or the entry set was rebuilt, so clicking around does not churn panels.
	//-----------------------------------------------------------------------------
	void RebuildSidebarIfNeeded( int nCat )
	{
		if ( !m_bSidebarDirty && m_nSidebarCat == nCat )
			return;

		m_pSourceList->ClearRows();

		for ( int i = 0; i < g_SMenuCatSources[nCat].Count(); ++i )
		{
			CSMSourceButton *pRow = new CSMSourceButton( this, m_pSourceList, g_SMenuCatSources[nCat][i], i, false );
			m_pSourceList->AddSourceRow( pRow );
		}

		m_nSidebarCat = nCat;
		m_bSidebarDirty = false;

		// HL2SB: the rows now exist, so the sidebar's required width is known -
		// relayout so PerformLayout can widen it before the first paint.
		InvalidateLayout( true );
	}

	// The index of a remembered source id inside a category's list, or -1.
	int FindSourceIndex( int nCat, const char *pszSourceId )
	{
		if ( !pszSourceId || !pszSourceId[0] )
			return -1;

		for ( int i = 0; i < g_SMenuCatSources[nCat].Count(); ++i )
		{
			if ( !Q_stricmp( g_SMenuCatSources[nCat][i].szId, pszSourceId ) )
				return i;
		}

		return -1;
	}

	void ShowCategory( int nCat )
	{
		if ( nCat < 0 || nCat >= (int)SMENU_CAT_COUNT || !m_bCatVisible[nCat] )
		{
			// The requested page is empty or gone (the tab strip hides those),
			// so land on the first category that does have entries.
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

		for ( int i = 0; i < SMENU_CAT_COUNT; ++i )
		{
			if ( m_pTabButtons[i] )
				m_pTabButtons[i]->SetActiveTab( i == nCat );
		}

		m_nCurrentCat = nCat;

		if ( m_pPages[nCat] )
		{
			m_pPages[nCat]->SetBounds( 0, 0, m_pGridHost->GetWide(), m_pGridHost->GetTall() );
			m_pPages[nCat]->InvalidateLayout( true );
		}

		RebuildSidebarIfNeeded( nCat );

		// Remember the source per category (GMod's sidebar keeps the selected
		// node for the tab you are on), falling back to the first one.  The
		// remembered id, not the index: a rebuild can reorder the list.
		int nSrc = FindSourceIndex( nCat, m_szSelSource[nCat] );
		if ( nSrc < 0 )
			nSrc = 0;

		ShowSource( nCat, nSrc );
	}

	// Select one source inside one category: highlight the row, point the page's
	// source filter at it and build its first slice right away.
	void ShowSource( int nCat, int nSrc )
	{
		if ( nCat < 0 || nCat >= (int)SMENU_CAT_COUNT )
			return;

		if ( nSrc < 0 || nSrc >= g_SMenuCatSources[nCat].Count() )
			return;

		SMenu_CopyString( m_szSelSource[nCat], g_SMenuCatSources[nCat][nSrc].szId, sizeof( m_szSelSource[nCat] ) );

		if ( m_nSidebarCat == nCat )
			m_pSourceList->SetSelection( nSrc );

		if ( CSMList *pPage = m_pPages[nCat] )
		{
			pPage->SetSourceFilter( g_SMenuCatSources[nCat][nSrc].szId );
			pPage->SetBounds( 0, 0, m_pGridHost->GetWide(), m_pGridHost->GetTall() );
			pPage->InvalidateLayout( true );
		}

		// HL2SB: build the first slice right now, so the click shows content
		// immediately; OnTick() finishes the page over the next frames.
		StepCurrentPage();
	}

	CSMTabBar			*m_pTabs;
	CSMSourceList		*m_pSourceList;
	vgui::Panel			*m_pGridHost;
	CSMTabButton		*m_pTabButtons[SMENU_CAT_COUNT];	// NULL = category hidden
	CSMList				*m_pPages[SMENU_CAT_COUNT];
	bool				m_bCatVisible[SMENU_CAT_COUNT];
	int					m_nCurrentCat;
	int					m_nSidebarCat;			// which category the sidebar lists (-1 = none)
	bool				m_bSidebarDirty;
	char				m_szSelSource[SMENU_CAT_COUNT][128];
	char				m_szBuiltLevel[256];
	unsigned int		m_uBuiltHash;
	bool				m_bBuiltOnce;
	bool				m_bModelPagePossible;	// materials/vgui/smenu/models/ exists
	bool				m_bModelPageBuilt;
};

void CSMTabBar::OnCommand( const char *command )
{
	if ( m_pOwner )
		m_pOwner->OnCommand( command );
}

void CSMSourceList::OnCommand( const char *command )
{
	if ( m_pOwner )
		m_pOwner->OnCommand( command );
}

void CSMSourceButton::OnMouseReleased( vgui::MouseCode code )
{
	if ( code != MOUSE_LEFT || !m_pOwner )
		return;

	char szCmd[32];
	Q_snprintf( szCmd, sizeof( szCmd ), "smsrc %d", m_nIndex );

	m_pOwner->OnCommand( szCmd );
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
