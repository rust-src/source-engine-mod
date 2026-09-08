// hands_model_mapping.h
// Maps player models to their corresponding hands/arms models
// Used by c_viewmodel_attachment system
// Now uses hl2sb_model_config system for dynamic configuration

#ifndef HANDS_MODEL_MAPPING_H
#define HANDS_MODEL_MAPPING_H

#ifdef _WIN32
#pragma once
#endif

#include "convar.h"
#include "tier0/platform.h"  // MAX_PATH
#include "Color.h"           // Color (per-player sleeve colour)

// Default hands model when no mapping is found
#define HANDS_MODEL_DEFAULT "models/arms/hands.mdl"

// ConVar for overriding hands model
extern ConVar cl_hands_model;

// Last successfully attached hands model (populated by C_BaseViewModel).
// Empty string means no c_hands have been attached -> stock/none.
extern char g_pszLastHandsModel[MAX_PATH];

// Hands model whose entity creation failed (see c_baseviewmodel.cpp).
extern char g_pszFailedHandsModel[MAX_PATH];

// Returns the currently active hands model path, or NULL if none is attached.
const char *HL2SB_GetActiveHandsModel( void );

// Destroy every live c_hands attachment entity. Called on player spawn /
// level change so a hands entity leaked from a previous session (one that kept
// rendering as a "proliferated" second hand) is torn down instead of lingering.
void HL2SB_DestroyAllHandsAttachments( void );

// Per-player sleeve colour (GMod player:GetPlayerColor/SetPlayerColor). The
// c_arms "PlayerColor" material proxy reads the local player's colour.
// Implemented in game/shared/lua/lbaseplayer_shared.cpp.
Color HL2SB_GetPlayerColor( int iUserID );
void HL2SB_SetPlayerColor( int iUserID, const Color &clr );

// How many c_hands entities are alive right now. Each weapon viewmodel owns at
// most one, so this stays bounded by the number of viewmodels in use.
int HL2SB_CountLiveHandsAttachments( void );

// Invariant check: a c_hands entity must NEVER be registered in the leaf
// system - the world renderer would then draw it a second time at the raw
// viewmodel origin, which is the "extra arm" artifact. hl2sb_status prints this
// so the invariant is observable from the console.
bool HL2SB_AnyHandsInLeafSystem( void );

// How many times the arms were really rendered in the most recent client frame.
// 1 is correct; 2+ means a duplicate draw path is back; 0 means no arms are
// being drawn at all.
int HL2SB_HandsDrawCountLastFrame( void );

// How many arm renders ever came from outside the viewmodel pass. Must stay 0:
// anything else is the ghost second arm.
int HL2SB_GhostHandsDrawCount( void );

// Bracket the one legitimate arms draw (C_BaseViewModel::DrawModel -> the
// viewmodel render pass). Anything that renders the arms outside this scope is
// the ghost second arm and gets flagged in the console.
void HL2SB_BeginManualHandsDraw( void );
void HL2SB_EndManualHandsDraw( void );

// Get the hands model path for a given player model
// Returns NULL if no hands should be shown
// Uses hl2sb_model_config system for dynamic configuration
inline const char *Hands_GetModelForPlayerModel( const char *pszPlayerModel )
{
	// If ConVar override is set, use it
	const char *pszOverride = cl_hands_model.GetString();
	if ( pszOverride && pszOverride[0] != '\0' && Q_strcmp( pszOverride, "auto" ) != 0 )
	{
		return pszOverride;
	}

	if ( !pszPlayerModel )
		return NULL;

	// Use the model config system to find hands
	// This will be called from hl2sb_model_config.cpp
	// For now, return NULL to use the config system
	return NULL;
}

#endif // HANDS_MODEL_MAPPING_H
