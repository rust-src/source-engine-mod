//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include <vgui_controls/Controls.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>
#include "hud_crosshair.h"
#ifdef LUA_SDK
#include "luamanager.h"
#include "lbasecombatweapon_shared.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CHudWeapon : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudWeapon, vgui::Panel );
public:
	CHudWeapon( const char *pElementName );

	virtual void	ApplySchemeSettings( vgui::IScheme *scheme );
	virtual void	Paint( void );
	virtual void	PerformLayout();

private:
	CHudCrosshair *m_pCrosshair;
};

DECLARE_HUDELEMENT( CHudWeapon );

CHudWeapon::CHudWeapon( const char *pElementName ) :
  CHudElement( pElementName ), BaseClass( NULL, "HudWeapon" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_pCrosshair = NULL;

	SetHiddenBits( HIDEHUD_WEAPONSELECTION );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *scheme - 
//-----------------------------------------------------------------------------
void CHudWeapon::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );

	SetPaintBackgroundEnabled( false );

	m_pCrosshair = GET_HUDELEMENT( CHudCrosshair );
	//Assert( m_pCrosshair );
}


//-----------------------------------------------------------------------------
// Performs layout
//-----------------------------------------------------------------------------
void CHudWeapon::PerformLayout()
{
	BaseClass::PerformLayout();

	vgui::Panel *pParent = GetParent();

	int w, h;
	pParent->GetSize( w, h );
	SetPos( 0, 0 );
	SetSize( w, h );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudWeapon::Paint( void )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();

	if ( !player )
		return;

	MDLCACHE_CRITICAL_SECTION();

	C_BaseCombatWeapon *pWeapon = player->GetActiveWeapon();
	
	if ( pWeapon )
	{
#if defined( LUA_SDK )
		// GMod-compatible SWEP:DrawHUD()  -- let scripted weapons draw their own
		// per-frame HUD. Guard with lua_isfunction so a SWEP that does not define
		// DrawHUD is skipped safely (BEGIN_LUA_CALL_WEAPON_HOOK would pcall nil).
		if ( pWeapon->IsScripted() )
		{
			lua_getref( L, pWeapon->m_nTableReference );
			lua_getfield( L, -1, "DrawHUD" );
			lua_remove( L, -2 );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_pushweapon( L, pWeapon );
				luasrc_pcall( L, 1, 0, 0 );
			}
			else
			{
				lua_pop( L, 1 );
			}
		}
#endif
		pWeapon->Redraw();
	}
	else
	{
		if ( m_pCrosshair )
		{
			m_pCrosshair->ResetCrosshair();
		}
	}
}
