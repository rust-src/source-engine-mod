//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style kill feed / death notice HUD for HL2SB.
//
//			Anchored to the TOP-RIGHT of the screen. Each entry is right-
//			justified as:
//
//			   <KillerName>  <iconDeath>  <VictimName>
//
//			Colour rules (GMod-ish):
//			  - killer name  : the killer's team colour, or white if unassigned
//			  - victim name  : red if the victim is an NPC, else the victim's
//			                   team colour (or red when unassigned)
//			  - icon         : red for a suicide / world / NPC kill, else the
//			                   killer's team colour
//
//			Rows stack downward from a position derived from the res block
//			(xpos = right margin, ypos = top margin) plus a small screen
//			percentage, so they don't sit flush against the top edge. The icon
//			is vertically centred against the text on the same row, and each row
//			fades out over the last ~1.2s of its lifetime (an explicit alpha
//			fade, not an abrupt pop-out).
//
//			Events:
//			  - player_death    : killer vs victim (players), or suicide
//			  - entity_killed   : player put down a non-player entity (NPC)
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

static ConVar hud_deathnotice_time( "hud_deathnotice_time", "6", FCVAR_ARCHIVE, "How long each death notice stays on screen (seconds)." );
static ConVar cl_drawdeathnotice( "cl_drawdeathnotice", "1", FCVAR_ARCHIVE, "Toggle the death notice / kill feed HUD on and off." );
static ConVar hud_killfeed_iconscale( "hud_killfeed_iconscale", "0.9", FCVAR_ARCHIVE, "Kill feed icon height as a fraction of the text height." );

//-----------------------------------------------------------------------------
// Player (or entity) entries in a death notice.
//-----------------------------------------------------------------------------
struct KillFeedPlayer
{
	char		szName[MAX_PLAYER_NAME_LENGTH];
	int			iEntIndex;	// engine player index, or 0 if not a real player
};

//-----------------------------------------------------------------------------
// Contents of each entry in our list of death notices.
//-----------------------------------------------------------------------------
struct KillFeedItem
{
	KillFeedPlayer	Killer;			// may be empty (suicide / world)
	KillFeedPlayer	Victim;
	CHudTexture		*iconDeath;		// death_<weapon> or skull
	int				iSuicide;		// 1 = no killer (world / self)
	bool			bKillerIsPlayer;
	bool			bVictimIsNPC;	// victim is a non-player entity
	float			flAddTime;		// server time when this entry was added
	float			flDisplayTime;	// server time when it should be removed
};

//-----------------------------------------------------------------------------
// Turn an entity classname into a readable display name.
//-----------------------------------------------------------------------------
static const char *KillFeed_DisplayName( const char *szClass, char *szOut, int nOutSize )
{
	Q_strncpy( szOut, szClass, nOutSize );

	if ( !Q_strnicmp( szOut, "class ", 6 ) )
		Q_strncpy( szOut, szClass + 6, nOutSize );

	const char *strip[] = { "npc_", "monster_", "weapon_", "item_", "ammo_", "entity_", "func_", "prop_" };
	for ( int i = 0; i < ARRAYSIZE(strip); i++ )
	{
		int n = Q_strlen( strip[i] );
		if ( !Q_strnicmp( szOut, strip[i], n ) )
		{
			memmove( szOut, szOut + n, nOutSize - n );
			break;
		}
	}

	if ( szOut[0] >= 'a' && szOut[0] <= 'z' )
		szOut[0] -= ( 'a' - 'A' );

	return szOut;
}

//-----------------------------------------------------------------------------
// Kill feed HUD element.
//-----------------------------------------------------------------------------
class CHudKillFeed : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudKillFeed, vgui::Panel );
public:
	CHudKillFeed( const char *pElementName );

	void Init( void );
	void VidInit( void );
	virtual bool ShouldDraw( void );
	virtual void Paint( void );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme );

	virtual void FireGameEvent( IGameEvent *event );

private:
	void RetireExpiredDeathNotices( void );
	Color GetKillerColour( const KillFeedItem &e );
	Color GetVictimColour( const KillFeedItem &e );
	Color GetIconColour( const KillFeedItem &e );

	// Res-driven layout/size fields (all read from the "HudKillFeed" block in
	// HudLayout.res, so everything can be tuned without recompiling).
	CPanelAnimationVarAliasType( float, m_flLineHeight, "LineHeight", "16", "proportional_float" );
	CPanelAnimationVar( float, m_flMaxDeathNotices, "MaxDeathNotices", "4" );
	CPanelAnimationVar( bool, m_bRightJustify, "RightJustify", "1" );
	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "HudNumbersTimer" );

	// Position/size.
	CPanelAnimationVar( float, m_flXPos, "xpos", "0" );
	CPanelAnimationVar( float, m_flYPos, "ypos", "0" );
	CPanelAnimationVar( float, m_flIconGap, "IconGap", "12" );
	CPanelAnimationVar( float, m_flKillerGap, "KillerGap", "12" );
	CPanelAnimationVar( float, m_flVictimGap, "VictimGap", "12" );
	CPanelAnimationVar( float, m_flLineGap, "LineGap", "8" );
	CPanelAnimationVar( float, m_flTopFrac, "TopFrac", "0.03" );
	CPanelAnimationVar( float, m_flFadeInTime, "FadeInTime", "0.3" );
	CPanelAnimationVar( float, m_flFadeOutTime, "FadeOutTime", "1.2" );

	// Texture for skull symbol (suicide / world / unknown kill).
	CHudTexture		*m_iconD_skull;
	// Loaded texture id for the GMod killicon material (hud/killicons/default)
	// so the kill icon can be drawn as a texture and scaled/centred exactly,
	// avoiding the vertical-ink offset of font glyphs.
	int				m_iKillIconTex;

	CUtlVector<KillFeedItem> m_DeathNotices;
};

using namespace vgui;

DECLARE_HUDELEMENT( CHudKillFeed );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudKillFeed::CHudKillFeed( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HudKillFeed" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_iconD_skull = NULL;
	m_iKillIconTex = -1;

	// Don't hide this element with the rest of the HUD when we die.
	SetHiddenBits( HIDEHUD_MISCSTATUS );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false );

	// Fix the font here so the width/height math never runs against an
	// invalid font (the res-driven "HudNumbersTimer" may not exist in HL2MP).
	m_hTextFont = scheme->GetFont( "Default", true );

	// Fill the screen so the Panel doesn't clip the right-justified block.
	SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::Init( void )
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "entity_killed" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::VidInit( void )
{
	m_iconD_skull = gHUD.GetIcon( "d_skull" );

	// Load the GMod killicon material as a texture so we can draw it as a
	// texture and scale/centre it exactly (font glyphs carry a vertical ink
	// offset we can't compensate without exact bounds).
	if ( m_iKillIconTex == -1 )
	{
		m_iKillIconTex = surface()->CreateNewTextureID();
		// Material path is relative to "materials/" (the material system prepends it).
		surface()->DrawSetTextureFile( m_iKillIconTex, "hud/killicons/default", true, false );
	}

	m_DeathNotices.Purge();
	SetPaintBackgroundEnabled( false );

	SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
}

//-----------------------------------------------------------------------------
// Purpose: Draw if we've got at least one death notice in the queue.
//-----------------------------------------------------------------------------
bool CHudKillFeed::ShouldDraw( void )
{
	if ( !cl_drawdeathnotice.GetBool() )
		return false;

	return ( CHudElement::ShouldDraw() && ( m_DeathNotices.Count() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Colour for the killer name.
//-----------------------------------------------------------------------------
Color CHudKillFeed::GetKillerColour( const KillFeedItem &e )
{
	if ( e.bKillerIsPlayer && e.Killer.iEntIndex > 0 )
	{
		int iTeam = g_PR ? g_PR->GetTeam( e.Killer.iEntIndex ) : 0;
		if ( iTeam > 0 )
			return GameResources()->GetTeamColor( iTeam );
	}
	// Non-player killer or unassigned: GMod-ish white.
	return Color( 240, 240, 240, 255 );
}

//-----------------------------------------------------------------------------
// Purpose: Colour for the victim name.
//-----------------------------------------------------------------------------
Color CHudKillFeed::GetVictimColour( const KillFeedItem &e )
{
	// NPC victim: always red (as requested).
	if ( e.bVictimIsNPC )
		return Color( 220, 40, 40, 255 );

	if ( e.Victim.iEntIndex > 0 )
	{
		int iTeam = g_PR ? g_PR->GetTeam( e.Victim.iEntIndex ) : 0;
		if ( iTeam > 0 )
			return GameResources()->GetTeamColor( iTeam );
	}

	// Unassigned / world victim: red.
	return Color( 220, 40, 40, 255 );
}

//-----------------------------------------------------------------------------
// Purpose: Colour for the weapon / skull icon.
//-----------------------------------------------------------------------------
Color CHudKillFeed::GetIconColour( const KillFeedItem &e )
{
	if ( e.iSuicide || e.bVictimIsNPC || !e.bKillerIsPlayer )
		return Color( 255, 60, 20, 255 );

	if ( e.Killer.iEntIndex > 0 )
	{
		int iTeam = g_PR ? g_PR->GetTeam( e.Killer.iEntIndex ) : 0;
		if ( iTeam > 0 )
			return GameResources()->GetTeamColor( iTeam );
	}

	return Color( 255, 60, 20, 255 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::Paint()
{
	if ( m_DeathNotices.Count() == 0 )
		return;

	if ( !m_hTextFont )
		m_hTextFont = vgui::scheme()->GetIScheme( GetScheme() )->GetFont( "Default", true );

	surface()->DrawSetTextFont( m_hTextFont );
	int iTextTall = surface()->GetFontTall( m_hTextFont );

	// Anchor near the top-right, but not flush against the top edge.
	int yStart = (int)( (float)ScreenHeight() * m_flTopFrac ) + (int)m_flYPos;
	int nScreenW = ScreenWidth();
	int xRight = ( m_bRightJustify ) ? ( nScreenW - (int)m_flXPos ) : (int)m_flXPos;

	int iCount = m_DeathNotices.Count();

	// Icon size driven by the cvar, scaled against the current text height.
	float flIconScale = hud_killfeed_iconscale.GetFloat();
	if ( flIconScale <= 0.0f )
		flIconScale = 0.9f;

	for ( int i = 0; i < iCount; i++ )
	{
		KillFeedItem &e = m_DeathNotices[i];
		CHudTexture *icon = e.iconDeath;
		if ( !icon )
			continue;

		wchar_t victim[ 256 ];
		wchar_t killer[ 256 ];
		g_pVGuiLocalize->ConvertANSIToUnicode( e.Victim.szName, victim, sizeof( victim ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( e.Killer.szName, killer, sizeof( killer ) );

		// Fade in on add, then fade out over the last FadeOutTime seconds.
		float flFadeIn = MAX( m_flFadeInTime, 0.0f );
		float flFadeOut = MAX( m_flFadeOutTime, 0.01f );
		float flAge = gpGlobals->curtime - e.flAddTime;
		float flRemain = e.flDisplayTime - gpGlobals->curtime;
		float flAlpha = 1.0f;
		if ( flFadeIn > 0.0f && flAge < flFadeIn )
			flAlpha = flAge / flFadeIn;
		else if ( flRemain < flFadeOut )
			flAlpha = clamp( flRemain / flFadeOut, 0.0f, 1.0f );
		flAlpha = clamp( flAlpha, 0.0f, 1.0f );
		int nAlpha = (int)( 255.0f * flAlpha );

		int iVictimW = UTIL_ComputeStringWidth( m_hTextFont, victim );
		int iKillerW = UTIL_ComputeStringWidth( m_hTextFont, killer );
		bool bShowKiller = ( !e.iSuicide && e.Killer.szName[0] );

		// Icon size: text height * scale, roughly square.
		int iconTall = (int)( (float)iTextTall * flIconScale );
		int iconWide = (int)( (float)iconTall * 1.0f );

		// Row vertical metrics: centre the names against the icon's height.
		int iRowTall = max( iTextTall, iconTall );
		int iRowY = yStart + ( i * ( (int)m_flLineGap + iRowTall ) );
		int iTextY = iRowY + ( iRowTall - iTextTall ) / 2;
		int iIconY = iTextY + ( iTextTall - iconTall ) / 2;

		// Layout, right-justified: victim name rightmost, icon to its left,
		// killer name further left, each separated by its own res-defined gap.
		int iIconGap = (int)m_flIconGap;
		int iVictimX = xRight - iVictimW;
		int iIconX = iVictimX - iIconGap - iconWide;
		int iKillerX = iIconX - (int)m_flKillerGap - ( ( bShowKiller ) ? iKillerW : 0 );

		// Killer name (leftmost).
		if ( bShowKiller )
		{
			Color c = GetKillerColour( e );
			c[3] = nAlpha;
			surface()->DrawSetTextColor( c );
			surface()->DrawSetTextPos( iKillerX, iTextY );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( killer );
		}

		// Icon (middle): draw the killicon texture, scaled and centred exactly.
		Color iconColor = GetIconColour( e );
		iconColor[3] = nAlpha;
		if ( m_iKillIconTex != -1 )
		{
			surface()->DrawSetTexture( m_iKillIconTex );
			surface()->DrawSetColor( iconColor );
			surface()->DrawTexturedRect( iIconX, iIconY, iIconX + iconWide, iIconY + iconTall );
		}
		else if ( icon )
		{
			icon->DrawSelf( iIconX, iIconY, iconWide, iconTall, iconColor );
		}
		// Reset text font so the victim name below isn't affected.
		surface()->DrawSetTextFont( m_hTextFont );

		// Victim name (rightmost).
		Color c = GetVictimColour( e );
		c[3] = nAlpha;
		surface()->DrawSetTextColor( c );
		surface()->DrawSetTextPos( iVictimX, iTextY );
		surface()->DrawSetTextFont( m_hTextFont );
		surface()->DrawUnicodeString( victim );
	}

	// Now retire any death notices that have expired.
	RetireExpiredDeathNotices();
}

//-----------------------------------------------------------------------------
// Purpose: This removes any death notices that have expired.
//-----------------------------------------------------------------------------
void CHudKillFeed::RetireExpiredDeathNotices( void )
{
	int iSize = m_DeathNotices.Size();
	for ( int i = iSize - 1; i >= 0; i-- )
	{
		if ( m_DeathNotices[i].flDisplayTime < gpGlobals->curtime )
		{
			m_DeathNotices.Remove(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Server's told us that someone's died.
//-----------------------------------------------------------------------------
void CHudKillFeed::FireGameEvent( IGameEvent * event )
{
	if ( !g_PR )
		return;

	if ( hud_deathnotice_time.GetFloat() == 0 )
		return;

	const char *pszName = event->GetName();

	KillFeedItem deathMsg;
	deathMsg.Killer.iEntIndex = 0;
	deathMsg.Victim.iEntIndex = 0;
	deathMsg.Killer.szName[0] = 0;
	deathMsg.Victim.szName[0] = 0;
	deathMsg.iconDeath = NULL;
	deathMsg.iSuicide = 0;
	deathMsg.bKillerIsPlayer = false;
	deathMsg.bVictimIsNPC = false;

	if ( !Q_stricmp( pszName, "entity_killed" ) )
	{
		// Player killed a non-player entity (NPC / prop).
		int iAttackerUID = event->GetInt( "attacker_uid" );
		if ( iAttackerUID == 0 )
			return;

		int killer = engine->GetPlayerForUserID( iAttackerUID );
		if ( killer == 0 || killer == -1 )
			return;

		const char *pszVictimClass = event->GetString( "victimclass", "" );
		if ( !Q_stricmp( pszVictimClass, "player" ) )
			return;		// player-vs-player handled via player_death

		Q_strncpy( deathMsg.Killer.szName, g_PR->GetPlayerName( killer ), MAX_PLAYER_NAME_LENGTH );
		KillFeed_DisplayName( pszVictimClass, deathMsg.Victim.szName, sizeof( deathMsg.Victim.szName ) );

		deathMsg.Killer.iEntIndex = killer;
		deathMsg.Victim.iEntIndex = 0;
		deathMsg.bKillerIsPlayer = true;
		deathMsg.bVictimIsNPC = true;

		const char *pszWeapon = event->GetString( "weapon", "" );
		deathMsg.iconDeath = gHUD.GetIcon( VarArgs( "death_%s", pszWeapon ) );
		if ( !deathMsg.iconDeath )
		{
			// No weapon death icon found; fall back to the skull.
			deathMsg.iconDeath = m_iconD_skull;
		}
	}
	else if ( !Q_stricmp( pszName, "player_death" ) )
	{
		int killer = engine->GetPlayerForUserID( event->GetInt("attacker") );
		int victim = engine->GetPlayerForUserID( event->GetInt("userid") );
		int iKillerUID = event->GetInt( "attacker" );

		const char *killer_name = ( killer != 0 && killer != -1 ) ? g_PR->GetPlayerName( killer ) : "";
		const char *victim_name = ( victim != 0 && victim != -1 ) ? g_PR->GetPlayerName( victim ) : "";

		if ( !killer_name )
			killer_name = "";
		if ( !victim_name )
			victim_name = "";

		const char *killedwith = event->GetString( "weapon" );

		deathMsg.Killer.iEntIndex = killer;
		deathMsg.Victim.iEntIndex = victim;
		Q_strncpy( deathMsg.Killer.szName, killer_name, MAX_PLAYER_NAME_LENGTH );
		Q_strncpy( deathMsg.Victim.szName, victim_name, MAX_PLAYER_NAME_LENGTH );

		deathMsg.bKillerIsPlayer = ( killer != 0 && killer != -1 );
		deathMsg.bVictimIsNPC = false;

		// Suicide / world kill (attacker is 0 or the killer is the victim).
		deathMsg.iSuicide = ( !killer || killer == victim || iKillerUID == 0 );

		char fullkilledwith[128];
		if ( killedwith && *killedwith )
		{
			Q_snprintf( fullkilledwith, sizeof(fullkilledwith), "death_%s", killedwith );
		}
		else
		{
			fullkilledwith[0] = 0;
		}

		deathMsg.iconDeath = gHUD.GetIcon( fullkilledwith );
		if ( !deathMsg.iconDeath || deathMsg.iSuicide )
		{
			deathMsg.iconDeath = m_iconD_skull;
		}
	}
	else
	{
		return;
	}

	// Do we have too many death messages in the queue?
	if ( m_DeathNotices.Count() > 0 &&
		m_DeathNotices.Count() >= (int)m_flMaxDeathNotices )
	{
		// Remove the oldest one, which will always be the first.
		m_DeathNotices.Remove(0);
	}

	deathMsg.flAddTime = gpGlobals->curtime;
	float flTime = hud_deathnotice_time.GetFloat();
	if ( flTime <= 0.0f )
		flTime = 6.0f;		// safety net: never let an entry pop out instantly
	deathMsg.flDisplayTime = gpGlobals->curtime + flTime;

	m_DeathNotices.AddToTail( deathMsg );

	Msg( "%s killed %s\n", deathMsg.Killer.szName, deathMsg.Victim.szName );
}
