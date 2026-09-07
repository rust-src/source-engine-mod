//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Draws CSPort's death notices
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "c_playerresource.h"
#include "clientmode_hl2mpnormal.h"
#include <vgui_controls/Controls.h>
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <KeyValues.h>
#include "c_baseplayer.h"
#include "c_team.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar hud_deathnotice_time( "hud_deathnotice_time", "6", 0 );

// Player entries in a death notice
struct DeathNoticePlayer
{
	char		szName[MAX_PLAYER_NAME_LENGTH];
	int			iEntIndex;
};

// Contents of each entry in our list of death notices
struct DeathNoticeItem 
{
	DeathNoticePlayer	Killer;
	DeathNoticePlayer   Victim;
	CHudTexture *iconDeath;
	int			iSuicide;
	float		flDisplayTime;
	bool		bHeadshot;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CHudDeathNotice : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudDeathNotice, vgui::Panel );
public:
	CHudDeathNotice( const char *pElementName );

	void Init( void );
	void VidInit( void );
	virtual bool ShouldDraw( void );
	virtual void Paint( void );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme );

	void SetColorForNoticePlayer( int iTeamNumber );
	void RetireExpiredDeathNotices( void );
	
	virtual void FireGameEvent( IGameEvent * event );

private:

	CPanelAnimationVarAliasType( float, m_flLineHeight, "LineHeight", "15", "proportional_float" );

	CPanelAnimationVar( float, m_flMaxDeathNotices, "MaxDeathNotices", "4" );

	CPanelAnimationVar( bool, m_bRightJustify, "RightJustify", "1" );

	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "HudNumbersTimer" );

	// Texture for skull symbol
	CHudTexture		*m_iconD_skull;  
	CHudTexture		*m_iconD_headshot;  

	CUtlVector<DeathNoticeItem> m_DeathNotices;
};

using namespace vgui;

// GMod-style: fade the notice out to transparent over the last fraction of a
// second before it is retired, instead of popping to invisible.
#define	DEATHNOTICE_FADE_OUT	0.6f

DECLARE_HUDELEMENT( CHudDeathNotice );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudDeathNotice::CHudDeathNotice( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HudDeathNotice" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_iconD_headshot = NULL;
	m_iconD_skull = NULL;

	SetHiddenBits( HIDEHUD_MISCSTATUS );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::Init( void )
{
	ListenForGameEvent( "player_death" );	
	ListenForGameEvent( "entity_killed" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::VidInit( void )
{
	m_iconD_skull = gHUD.GetIcon( "d_skull" );
	m_DeathNotices.Purge();
}

//-----------------------------------------------------------------------------
// Purpose: Draw if we've got at least one death notice in the queue
//-----------------------------------------------------------------------------
bool CHudDeathNotice::ShouldDraw( void )
{
	return ( CHudElement::ShouldDraw() && ( m_DeathNotices.Count() ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::SetColorForNoticePlayer( int iTeamNumber )
{
	surface()->DrawSetTextColor( GameResources()->GetTeamColor( iTeamNumber ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::Paint()
{
	if ( !m_iconD_skull )
		return;

	int yStart = GetClientModeHL2MPNormal()->GetDeathMessageStartHeight();

	surface()->DrawSetTextFont( m_hTextFont );
	surface()->DrawSetTextColor( GameResources()->GetTeamColor( 0 ) );


	int iCount = m_DeathNotices.Count();
	for ( int i = 0; i < iCount; i++ )
	{
		CHudTexture *icon = m_DeathNotices[i].iconDeath;
		if ( !icon )
			continue;

		wchar_t victim[ 256 ];
		wchar_t killer[ 256 ];

		// Get the team numbers for the players involved
		int iKillerTeam = 0;
		int iVictimTeam = 0;

		if( g_PR )
		{
			iKillerTeam = g_PR->GetTeam( m_DeathNotices[i].Killer.iEntIndex );
			iVictimTeam = g_PR->GetTeam( m_DeathNotices[i].Victim.iEntIndex );
		}

		// Fade the whole row out over the last DEATHNOTICE_FADE_OUT seconds.
		float flRemaining = m_DeathNotices[i].flDisplayTime - gpGlobals->curtime;
		float flAlpha = clamp( flRemaining / DEATHNOTICE_FADE_OUT, 0.0f, 1.0f );
		int iAlpha = (int)( 255.0f * flAlpha );

		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Victim.szName, victim, sizeof( victim ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( m_DeathNotices[i].Killer.szName, killer, sizeof( killer ) );

		// Get the local position for this notice
		int len = UTIL_ComputeStringWidth( m_hTextFont, victim );
		int y = yStart + (m_flLineHeight * i);

		int iconWide;
		int iconTall;

		if( icon->bRenderUsingFont )
		{
			iconWide = surface()->GetCharacterWidth( icon->hFont, icon->cCharacterInFont );
			iconTall = surface()->GetFontTall( icon->hFont );
		}
		else
		{
			float scale = ( (float)ScreenHeight() / 480.0f );	//scale based on 640x480
			iconWide = (int)( scale * (float)icon->Width() );
			iconTall = (int)( scale * (float)icon->Height() );
		}

		int x;
		if ( m_bRightJustify )
		{
			x =	GetWide() - len - iconWide;
		}
		else
		{
			x = 0;
		}
		
		// Only draw killers name if it wasn't a suicide
		if ( !m_DeathNotices[i].iSuicide )
		{
			if ( m_bRightJustify )
			{
				x -= UTIL_ComputeStringWidth( m_hTextFont, killer );
			}

			// NPC / world / prop killers have no team; draw them in a neutral white
			// so the name isn't tinted with an arbitrary unassigned-team colour.
			if ( m_DeathNotices[i].Killer.iEntIndex == 0 )
			{
				surface()->DrawSetTextColor( Color( 235, 235, 235, iAlpha ) );
			}
			else
			{
				Color clr = GameResources()->GetTeamColor( iKillerTeam );
				surface()->DrawSetTextColor( Color( clr.r(), clr.g(), clr.b(), (int)( clr.a() * flAlpha ) ) );
			}

			// Draw killer's name
			surface()->DrawSetTextPos( x, y );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( killer );
			surface()->DrawGetTextPos( x, y );
		}

		Color iconColor( 255, 80, 0, iAlpha );

		// Draw death weapon
		//If we're using a font char, this will ignore iconTall and iconWide
		icon->DrawSelf( x, y, iconWide, iconTall, iconColor );
		x += iconWide;		

		Color clrVictim = GameResources()->GetTeamColor( iVictimTeam );
		surface()->DrawSetTextColor( Color( clrVictim.r(), clrVictim.g(), clrVictim.b(), (int)( clrVictim.a() * flAlpha ) ) );

		// Draw victims name
		surface()->DrawSetTextPos( x, y );
		surface()->DrawSetTextFont( m_hTextFont );	//reset the font, draw icon can change it
		surface()->DrawUnicodeString( victim );
	}

	// Now retire any death notices that have expired
	RetireExpiredDeathNotices();
}

//-----------------------------------------------------------------------------
// Purpose: This message handler may be better off elsewhere
//-----------------------------------------------------------------------------
void CHudDeathNotice::RetireExpiredDeathNotices( void )
{
	// Loop backwards because we might remove one
	int iSize = m_DeathNotices.Size();
	for ( int i = iSize-1; i >= 0; i-- )
	{
		if ( m_DeathNotices[i].flDisplayTime < gpGlobals->curtime )
		{
			m_DeathNotices.Remove(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Turn a killfeed entity classname (e.g. "npc_headcrab", "weapon_smg1",
//			"monster_zombie", "prop_physics") into a readable display name.
//-----------------------------------------------------------------------------
static const char *GetDisplayNameFromClassname( const char *szClass, char *szOut, int nOutSize )
{
	Q_strncpy( szOut, szClass, nOutSize );

	// Strip the server-side "class " prefix if the field was never overridden.
	if ( !Q_strnicmp( szOut, "class ", 6 ) )
	{
		Q_strncpy( szOut, szClass + 6, nOutSize );
	}

	// Strip the common entity type prefixes.
	const char *strip[] = { "npc_", "monster_", "weapon_", "item_", "ammo_", "entity_", "func_" };
	for ( int i = 0; i < ARRAYSIZE(strip); i++ )
	{
		int n = Q_strlen( strip[i] );
		if ( !Q_strnicmp( szOut, strip[i], n ) )
		{
			memmove( szOut, szOut + n, nOutSize - n );
			break;
		}
	}

	// Capitalise the first character.
	if ( szOut[0] >= 'a' && szOut[0] <= 'z' )
		szOut[0] -= ('a' - 'A');

	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Server's told us that someone's died
//-----------------------------------------------------------------------------
void CHudDeathNotice::FireGameEvent( IGameEvent * event )
{
	if (!g_PR)
		return;

	if ( hud_deathnotice_time.GetFloat() == 0 )
		return;

	// A player killed a non-player entity (NPC / prop). The server ships the
	// attacker's userid in attacker_uid and the victim's classname in
	// victimclass. Show it as a "PlayerName <icon> VictimName" killfeed entry the
	// same way as a player death, so it inherits the correct position/font/fade.
	if ( !Q_stricmp( event->GetName(), "entity_killed" ) )
	{
		int iAttackerUID = event->GetInt( "attacker_uid" );
		if ( iAttackerUID != 0 )
		{
			int iKillerEnt = engine->GetPlayerForUserID( iAttackerUID );
			if ( iKillerEnt != 0 && iKillerEnt != -1 )
			{
				const char *szKillerName = g_PR->GetPlayerName( iKillerEnt );
				if ( !szKillerName )
					szKillerName = "";

				// Ignore player-vs-player kills here (those arrive via player_death).
				const char *szVictimClass = event->GetString( "victimclass", "" );
				if ( Q_stricmp( szVictimClass, "player" ) != 0 )
				{
					// Do we have too many death messages in the queue?
					if ( m_DeathNotices.Count() > 0 &&
						m_DeathNotices.Count() >= (int)m_flMaxDeathNotices )
					{
						m_DeathNotices.Remove(0);
					}

					char szVictimName[ MAX_PLAYER_NAME_LENGTH ];
					GetDisplayNameFromClassname( szVictimClass, szVictimName, sizeof( szVictimName ) );

					char fullkilledwith[128];
					const char *pszWeapon = event->GetString( "weapon", "" );
					if ( pszWeapon && *pszWeapon )
						Q_snprintf( fullkilledwith, sizeof(fullkilledwith), "death_%s", pszWeapon );
					else
						fullkilledwith[0] = 0;

					DeathNoticeItem deathMsg;
					deathMsg.Killer.iEntIndex = iKillerEnt;
					deathMsg.Victim.iEntIndex = 0;
					Q_strncpy( deathMsg.Killer.szName, szKillerName, MAX_PLAYER_NAME_LENGTH );
					Q_strncpy( deathMsg.Victim.szName, szVictimName, MAX_PLAYER_NAME_LENGTH );
					deathMsg.flDisplayTime = gpGlobals->curtime + hud_deathnotice_time.GetFloat();
					deathMsg.iSuicide = 0;
					deathMsg.bHeadshot = false;
					deathMsg.iconDeath = gHUD.GetIcon( fullkilledwith );
					if ( !deathMsg.iconDeath )
						deathMsg.iconDeath = m_iconD_skull;

					m_DeathNotices.AddToTail( deathMsg );

					Msg( "%s killed %s\n", deathMsg.Killer.szName, deathMsg.Victim.szName );
				}
			}
		}
		return;
	}

	// the event should be "player_death"
	int killer = engine->GetPlayerForUserID( event->GetInt("attacker") );
	int victim = engine->GetPlayerForUserID( event->GetInt("userid") );
	const char *killedwith = event->GetString( "weapon" );

	// If the attacker wasn't a player (NPC / world / prop), the server ships the
	// killer's entity classname in "attackername" so we can show a real name.
	const char *szAttackerName = event->GetString( "attackername", "" );
	bool bKillerIsPlayer = ( killer != 0 && killer != -1 );

	char fullkilledwith[128];
	if ( killedwith && *killedwith )
	{
		Q_snprintf( fullkilledwith, sizeof(fullkilledwith), "death_%s", killedwith );
	}
	else
	{
		fullkilledwith[0] = 0;
	}

	// Do we have too many death messages in the queue?
	if ( m_DeathNotices.Count() > 0 &&
		m_DeathNotices.Count() >= (int)m_flMaxDeathNotices )
	{
		// Remove the oldest one in the queue, which will always be the first
		m_DeathNotices.Remove(0);
	}

	// Get the names of the players
	const char *killer_name = g_PR->GetPlayerName( killer );
	const char *victim_name = g_PR->GetPlayerName( victim );

	if ( !killer_name )
		killer_name = "";
	if ( !victim_name )
		victim_name = "";

	// Make a new death notice
	DeathNoticeItem deathMsg;
	deathMsg.Killer.iEntIndex = ( bKillerIsPlayer ) ? killer : 0;
	deathMsg.Victim.iEntIndex = victim;

	// For a player killer use the player name; otherwise fall back to the attacker
	// entity's classname so NPC kills show something instead of a blank/suicide line.
	if ( bKillerIsPlayer )
	{
		Q_strncpy( deathMsg.Killer.szName, killer_name, MAX_PLAYER_NAME_LENGTH );
	}
	else
	{
		char szDisplayName[ MAX_PLAYER_NAME_LENGTH ];
		GetDisplayNameFromClassname( szAttackerName, szDisplayName, sizeof( szDisplayName ) );
		Q_strncpy( deathMsg.Killer.szName, szDisplayName, MAX_PLAYER_NAME_LENGTH );
	}

	Q_strncpy( deathMsg.Victim.szName, victim_name, MAX_PLAYER_NAME_LENGTH );

	deathMsg.flDisplayTime = gpGlobals->curtime + hud_deathnotice_time.GetFloat();

	// Treat as a suicide only when the victim killed themselves with a real player
	// killer. NPC / world / prop deaths are not suicides.
	deathMsg.iSuicide = ( bKillerIsPlayer && killer == victim );

	// Try and find the death identifier in the icon list
	deathMsg.iconDeath = gHUD.GetIcon( fullkilledwith );

	if ( !deathMsg.iconDeath || deathMsg.iSuicide )
	{
		// Can't find it, so use the default skull & crossbones icon
		deathMsg.iconDeath = m_iconD_skull;
	}

	// Add it to our list of death notices
	m_DeathNotices.AddToTail( deathMsg );

	char sDeathMsg[512];

	// Record the death notice in the console
	if ( deathMsg.iSuicide )
	{
		if ( !strcmp( fullkilledwith, "d_worldspawn" ) )
		{
			Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s died.\n", deathMsg.Victim.szName );
		}
		else	//d_world
		{
			Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s suicided.\n", deathMsg.Victim.szName );
		}
	}
	else
	{
		Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s killed %s", deathMsg.Killer.szName, deathMsg.Victim.szName );

		if ( fullkilledwith && *fullkilledwith && (*fullkilledwith > 13 ) )
		{
			Q_strncat( sDeathMsg, VarArgs( " with %s.\n", fullkilledwith+6 ), sizeof( sDeathMsg ), COPY_ALL_CHARACTERS );
		}
	}

	Msg( "%s", sDeathMsg );
}



