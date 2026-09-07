//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style fullscreen "create server" dialog
//
// $NoKeywords: $
//=============================================================================//

#include "CreateMultiplayerGameDialog.h"

// include original page classes so we can still read cvars / bot settings
#include "CreateMultiplayerGameServerPage.h"
#include "CreateMultiplayerGameGameplayPage.h"
#include "CreateMultiplayerGameBotPage.h"

#include "EngineInterface.h"
#include "ModInfo.h"
#include "GameUI_Interface.h"
#include "PNGImagePanel.h"
#include "MouseMessageForwardingPanel.h"

#include <stdio.h>

using namespace vgui;

#include "vgui_controls/ComboBox.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/CheckButton.h"
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>

#include "filesystem.h"
#include <KeyValues.h>
#include <tier0/memdbgon.h>

#define RANDOM_MAP "#GameUI_RandomMap"
#define MAX_PLAYERS_DEFAULT 32

//-----------------------------------------------------------------------------
// Purpose: A single selectable map card (thumbnail + name)
//-----------------------------------------------------------------------------
class CMapCardPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CMapCardPanel, vgui::EditablePanel );
public:
	CMapCardPanel( PanelListPanel *parent, const char *name, CCreateMultiplayerGameDialog *pOwner, const char *pszMapName )
		: BaseClass( parent, name )
	{
		m_pOwner = pOwner;
		Q_strncpy( m_szMapName, pszMapName, sizeof( m_szMapName ) );

		m_pThumb = new CPNGImagePanel( this, "MapThumb" );
		m_pThumb->SetMouseInputEnabled( false );
		m_pThumb->SetMapImage( pszMapName );

		m_pName = new Label( this, "MapName", pszMapName );
		m_pName->SetMouseInputEnabled( false );

		// transparent panel that fills the card and forwards clicks to us
		m_pClickCatcher = new CMouseMessageForwardingPanel( this, NULL );
		m_pClickCatcher->SetZPos( 2 );

		// Square-ish card to match the 128x128 map thumbnails (image + name bar)
		SetSize( 150, 178 );
		SetPaintBackgroundEnabled( true );
		m_bSelected = false;
	}

	~CMapCardPanel() {}

	const char *GetMapName() const { return m_szMapName; }

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		if ( m_pName )
		{
			m_pName->SetContentAlignment( Label::a_center );
			m_pName->SetTextInset( 0, 0 );
		}
	}

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		int w, h;
		GetSize( w, h );

		// Square thumbnail area (matches the 128x128 source), name bar at bottom
		int nameBarH = 24;
		int pad = 6;

		if ( m_pThumb )
		{
			// Keep the thumb square, centered horizontally, filling the height above the name bar
			int thumbArea = h - nameBarH - pad;
			int size = w - pad * 2;
			if ( size > thumbArea )
				size = thumbArea;
			int x = (w - size) / 2;
			m_pThumb->SetBounds( x, pad, size, size );
		}
		if ( m_pName )
		{
			m_pName->SetBounds( pad, h - nameBarH - 2, w - pad * 2, nameBarH - 2 );
		}
	}

	// Forwarded from the click-catcher panel (CMouseMessageForwardingPanel ->
	// CallParentFunction("MousePressed")).
	virtual void OnMousePressed( vgui::MouseCode code )
	{
		BaseClass::OnMousePressed( code );
		if ( m_pOwner )
		{
			m_pOwner->OnMapSelected( m_szMapName );
		}
	}

	virtual void PaintBackground()
	{
		// selected highlight
		if ( m_bSelected )
		{
			vgui::surface()->DrawSetColor( 255, 200, 0, 90 );
			vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
		}
		else
		{
			vgui::surface()->DrawSetColor( 0, 0, 0, 120 );
			vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
		}
	}

	void SetSelected( bool b ) { m_bSelected = b; }

private:
	CPNGImagePanel *m_pThumb;
	Label *m_pName;
	CMouseMessageForwardingPanel *m_pClickCatcher;
	CCreateMultiplayerGameDialog *m_pOwner;
	char m_szMapName[256];
	bool m_bSelected;
};

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CCreateMultiplayerGameDialog::CCreateMultiplayerGameDialog(vgui::Panel *parent) : BaseClass(parent, "CreateMultiplayerGameDialog")
{
	m_bBotsEnabled = false;
	m_bBuilt = false;
	m_pSavedData = NULL;
	m_szSelectedMap[0] = 0;

	SetDeleteSelfOnClose(true);

	// Content & chrome are built in ApplySchemeSettings() (first call), which
	// runs after the Frame is fully constructed. Building children or invoking
	// Frame chome mutators here crashes the Frame's half-built internals.
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CCreateMultiplayerGameDialog::~CCreateMultiplayerGameDialog()
{
	if ( m_pSavedData )
	{
		m_pSavedData->deleteThis();
		m_pSavedData = NULL;
	}

	for ( int i = 0; i < m_MapNames.Count(); ++i )
	{
		delete[] m_MapNames[i];
	}
	m_MapNames.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Layout everything
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::PerformLayout()
{
	BaseClass::PerformLayout();

	int nScreenW, nScreenH;
	vgui::surface()->GetScreenSize( nScreenW, nScreenH );
	int sw = nScreenW;
	int sh = nScreenH;

	// left column (game modes)
	int leftX = (int)(sw * 0.02);
	int leftW = (int)(sw * 0.14);
	int topY = (int)(sh * 0.16);
	int bottomY = (int)(sh * 0.92);

	if ( m_pGameModeList )
		m_pGameModeList->SetBounds( leftX, topY, leftW, bottomY - topY );

	// center map grid
	int mapX = (int)(sw * 0.18);
	int mapW = (int)(sw * 0.52);
	if ( m_pMapList )
		m_pMapList->SetBounds( mapX, topY, mapW, bottomY - topY );

	// right settings panel
	int rightX = (int)(sw * 0.72);
	int rightW = sw - rightX - (int)(sw * 0.02);

	if ( m_pTitleLabel )
		m_pTitleLabel->SetBounds( (int)(sw * 0.18), (int)(sh * 0.05), (int)(sw * 0.5), 40 );

	if ( m_pSelectedMapLabel )
		m_pSelectedMapLabel->SetBounds( rightX, topY, rightW, 24 );

	if ( m_pHostNameLabel )
		m_pHostNameLabel->SetBounds( rightX, topY + 34, rightW, 20 );
	if ( m_pHostName )
		m_pHostName->SetBounds( rightX, topY + 54, rightW, 26 );

	if ( m_pPasswordLabel )
		m_pPasswordLabel->SetBounds( rightX, topY + 90, rightW, 20 );
	if ( m_pPassword )
		m_pPassword->SetBounds( rightX, topY + 110, rightW, 26 );

	if ( m_pMaxPlayersLabel )
		m_pMaxPlayersLabel->SetBounds( rightX, topY + 146, rightW, 20 );
	if ( m_pMaxPlayers )
		m_pMaxPlayers->SetBounds( rightX, topY + 166, rightW, 26 );

	// start / back buttons at bottom
	int btnH = (int)(sh * 0.05);
	int btnY = sh - btnH - (int)(sh * 0.02);

	if ( m_pStartButton )
	{
		m_pStartButton->SetBounds( rightX, btnY, (int)(rightW * 0.55), btnH );
		const wchar_t *pwsz = g_pVGuiLocalize->Find( "#GameUI_StartGame" );
		m_pStartButton->SetText( pwsz ? pwsz : L"Start Game" );
	}

	if ( m_pBackButton )
	{
		m_pBackButton->SetBounds( (int)(sw * 0.02), btnY, (int)(rightW * 0.3), btnH );
		const wchar_t *pwsz = g_pVGuiLocalize->Find( "#GameUI_Back" );
		m_pBackButton->SetText( pwsz ? pwsz : L"Back" );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	if ( !m_bBuilt )
	{
		m_bBuilt = true;

		SetTitle("#GameUI_CreateServer", false);

		if ( ModInfo().UseBots() )
		{
			m_bBotsEnabled = true;
		}

		m_pGameModeList = new vgui::PanelListPanel( this, "GameModeList" );
		m_pGameModeList->SetFirstColumnWidth( 0 );

		m_pMapList = new vgui::PanelListPanel( this, "MapList" );
		m_pMapList->SetFirstColumnWidth( 0 );

		m_pSelectedMapLabel = new Label( this, "SelectedMapLabel", "" );
		m_pHostName = new TextEntry( this, "HostName" );
		m_pPassword = new TextEntry( this, "Password" );
		m_pMaxPlayers = new ComboBox( this, "MaxPlayers", 8, false );

		m_pTitleLabel = new Label( this, "TitleLabel", "#GameUI_CreateServer" );
		m_pHostNameLabel = new Label( this, "HostNameLabel", "#GameUI_ServerName" );
		m_pPasswordLabel = new Label( this, "PasswordLabel", "#GameUI_Password" );
		m_pMaxPlayersLabel = new Label( this, "MaxPlayersLabel", "#GameUI_MaxPlayers" );

		m_pStartButton = new Button( this, "StartButton", "#GameUI_Start" );
		m_pStartButton->SetCommand( "CreateGame" );
		m_pStartButton->SetVisible( true );

		m_pBackButton = new Button( this, "BackButton", "#GameUI_Back" );
		m_pBackButton->SetCommand( "Close" );
		m_pBackButton->SetVisible( true );

		for ( int i = 2; i <= 128; i *= 2 )
		{
			char sz[16];
			Q_snprintf( sz, sizeof( sz ), "%d", i );
			m_pMaxPlayers->AddItem( sz, new KeyValues( "maxplayers", "val", i ) );
		}
		m_pMaxPlayers->ActivateItemByRow( 4 );

		m_pSavedData = new KeyValues( "ServerConfig" );
		if ( m_pSavedData )
		{
			m_pSavedData->LoadFromFile( g_pFullFileSystem, "ServerConfig.vdf", "GAME" );
		}

		BuildGameModeList();
		BuildMapGrid();

		if ( m_pHostName )
			m_pHostName->SetText( ModInfo().GetGameName() );
		if ( m_pPassword )
			m_pPassword->SetText( "" );
		if ( m_pHostName )
			m_pHostName->SetMultiline( false );
		if ( m_pPassword )
			m_pPassword->SetMultiline( false );
	}

	int nScreenW, nScreenH;
	vgui::surface()->GetScreenSize( nScreenW, nScreenH );
	SetSize( nScreenW, nScreenH );
	SetPos( 0, 0 );
}

//-----------------------------------------------------------------------------
// Purpose: Build the game mode list (left). hl2sb has sandbox/deathmatch/campaign
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::BuildGameModeList()
{
	if ( !m_pGameModeList )
		return;

	m_pGameModeList->DeleteAllItems();

	struct GameModeInfo_t
	{
		const char *pszName;
	};

	// static list of game modes shipped with hl2sb
	GameModeInfo_t modes[] =
	{
		{ "Sandbox" },
		{ "Deathmatch" },
		{ "Campaign" },
	};

	for ( int i = 0 ; i < ARRAYSIZE( modes ); ++i )
	{
		Label *pLabel = new Label( m_pGameModeList, "GameModeLabel", modes[i].pszName );
		pLabel->SetContentAlignment( Label::a_west );
		pLabel->SetTextInset( 8, 0 );
		pLabel->SetTall( 28 );
		m_pGameModeList->AddItem( NULL, pLabel );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Build the map grid
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::BuildMapGrid()
{
	if ( !m_pMapList )
		return;

	m_pMapList->DeleteAllItems();

	LoadMapList();

	// 3-column grid
	m_pMapList->SetNumColumns( 3 );

	RefreshSelection();
}

//-----------------------------------------------------------------------------
// Purpose: Loads the list of available maps into the map list
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Purpose: Scan one search path for maps and add them to the grid (deduped)
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::LoadMaps( const char *pszPathID )
{
	FileFindHandle_t findHandle = NULL;

	KeyValues *hiddenMaps = ModInfo().GetHiddenMaps();

	const char *pszFilename = g_pFullFileSystem->FindFirstEx( "maps/*.bsp", pszPathID, &findHandle );
	while ( pszFilename )
	{
		char mapname[256];
		char *ext;

		Q_strncpy( mapname, pszFilename, sizeof( mapname ) - 1 );
		mapname[ sizeof(mapname) - 1 ] = 0;

		ext = Q_strstr( mapname, ".bsp" );
		if ( ext )
		{
			*ext = 0;
		}

		// skip hidden maps
		if ( hiddenMaps )
		{
			if ( hiddenMaps->GetInt( mapname, 0 ) )
			{
				goto nextFile;
			}
		}

		// skip duplicates
		bool bDup = false;
		for ( int i = 0; i < m_MapNames.Count(); ++i )
		{
			if ( !Q_stricmp( m_MapNames[i], mapname ) )
			{
				bDup = true;
				break;
			}
		}
		if ( bDup )
		{
			goto nextFile;
		}

		// add a card to the grid
		CMapCardPanel *pCard = new CMapCardPanel( m_pMapList, "MapCard", this, mapname );
		m_pMapList->AddItem( NULL, pCard );

		// store map name for retrieval
		char *pszCopy = new char[ strlen(mapname) + 1 ];
		Q_strcpy( pszCopy, mapname );
		m_MapNames.AddToTail( pszCopy );

	nextFile:
		pszFilename = g_pFullFileSystem->FindNext( findHandle );
	}
	g_pFullFileSystem->FindClose( findHandle );
}

//-----------------------------------------------------------------------------
// Purpose: Load all available maps into the grid
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::LoadMapList()
{
	// GAME search path covers the mod dir + mounted custom dirs
	LoadMaps( "GAME" );

	// fall back to MOD in case the mod dir doesn't resolve through GAME
	if ( m_MapNames.Count() == 0 )
	{
		LoadMaps( "MOD" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called when a map card is clicked
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::OnMapSelected( const char *pszMapName )
{
	Q_strncpy( m_szSelectedMap, pszMapName, sizeof( m_szSelectedMap ) );

	// highlight the matching card
	RefreshSelection();

	// reflect the selection on the label
	if ( m_pSelectedMapLabel )
	{
		char szLabel[512];
		Q_snprintf( szLabel, sizeof( szLabel ), "Map: %s", m_szSelectedMap );
		m_pSelectedMapLabel->SetText( szLabel );
	}
}

//-----------------------------------------------------------------------------
// Purpose: highlight the selected map card
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::RefreshSelection()
{
	if ( !m_pMapList )
		return;

	for ( int nItemID = m_pMapList->FirstItem(); nItemID != m_pMapList->InvalidItemID(); nItemID = m_pMapList->NextItem( nItemID ) )
	{
		CMapCardPanel *pCard = dynamic_cast< CMapCardPanel * >( m_pMapList->GetItemPanel( nItemID ) );
		if ( pCard )
		{
			bool bSelected = ( m_szSelectedMap[0] != 0 && !Q_stricmp( pCard->GetMapName(), m_szSelectedMap ) );
			pCard->SetSelected( bSelected );
		}
	}

	// default selection
	if ( m_szSelectedMap[0] == 0 )
	{
		const char *startMap = m_pSavedData ? m_pSavedData->GetString("map", "") : "";
		if ( startMap[0] )
		{
			OnMapSelected( startMap );
		}
		else if ( m_MapNames.Count() )
		{
			OnMapSelected( m_MapNames[0] );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if randomly selected map
//-----------------------------------------------------------------------------
bool CCreateMultiplayerGameDialog::IsRandomMapSelected()
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Returns currently selected map
//-----------------------------------------------------------------------------
const char *CCreateMultiplayerGameDialog::GetMapName()
{
	if ( m_szSelectedMap[0] == 0 && m_MapNames.Count() )
		return m_MapNames[0];
	return m_szSelectedMap;
}

//-----------------------------------------------------------------------------
// Purpose: getters for server settings
//-----------------------------------------------------------------------------
const char *CCreateMultiplayerGameDialog::GetHostName()
{
	static char szValue[256];
	if ( m_pHostName )
		m_pHostName->GetText( szValue, sizeof( szValue ) );
	else
		szValue[0] = 0;
	return szValue;
}

const char *CCreateMultiplayerGameDialog::GetPassword()
{
	static char szValue[256];
	if ( m_pPassword )
		m_pPassword->GetText( szValue, sizeof( szValue ) );
	else
		szValue[0] = 0;
	return szValue;
}

int CCreateMultiplayerGameDialog::GetMaxPlayers()
{
	if ( m_pMaxPlayers )
	{
		KeyValues *kv = m_pMaxPlayers->GetActiveItemUserData();
		if ( kv )
			return kv->GetInt( "val", MAX_PLAYERS_DEFAULT );
	}
	return MAX_PLAYERS_DEFAULT;
}

//-----------------------------------------------------------------------------
// Purpose: Save the config to disk
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::SaveConfig()
{
	if ( m_pSavedData )
	{
		m_pSavedData->SetString( "map", GetMapName() );
		m_pSavedData->SaveToFile( g_pFullFileSystem, "ServerConfig.vdf", "GAME" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: runs the server when the Start button is pressed
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::CreateGame()
{
	// reset server enforced cvars
	g_pCVar->RevertFlaggedConVars( FCVAR_REPLICATED );
	g_pCVar->RevertFlaggedConVars( FCVAR_CHEAT );

	char szMapName[64], szHostName[64], szPassword[64];
	Q_strncpy( szMapName, GetMapName(), sizeof( szMapName ) );
	Q_strncpy( szHostName, GetHostName(), sizeof( szHostName ) );
	Q_strncpy( szPassword, GetPassword(), sizeof( szPassword ) );

	SaveConfig();

	char szMapCommand[1024];
	Q_snprintf(szMapCommand, sizeof( szMapCommand ), "disconnect\nwait\nwait\nsv_lan 1\nsetmaster enable\nmaxplayers %i\nsv_password \"%s\"\nhostname \"%s\"\nprogress_enable\nmap %s\n",
		GetMaxPlayers(),
		szPassword,
		szHostName,
		szMapName
	);

	engine->ClientCmd_Unrestricted(szMapCommand);
}

//-----------------------------------------------------------------------------
// Purpose: command handler
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "CreateGame" ) )
	{
		CreateGame();
		return;
	}
	else if ( !Q_stricmp( command, "Close" ) )
	{
		Close();
		return;
	}

	BaseClass::OnCommand( command );
}

void CCreateMultiplayerGameDialog::OnKeyCodePressed( vgui::KeyCode code )
{
	ButtonCode_t nButtonCode = GetBaseButtonCode( code );

	if ( nButtonCode == KEY_XBUTTON_B || nButtonCode == STEAMCONTROLLER_B )
	{
		Close();
		return;
	}
	else if ( nButtonCode == KEY_ENTER || nButtonCode == KEY_XBUTTON_A || nButtonCode == STEAMCONTROLLER_A )
	{
		CreateGame();
		return;
	}

	BaseClass::OnKeyCodePressed( code );
}
