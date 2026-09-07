//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style fullscreen "create server" dialog
//
// $NoKeywords: $
//=============================================================================//

#ifndef CREATEMULTIPLAYERGAMEDIALOG_H
#define CREATEMULTIPLAYERGAMEDIALOG_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Frame.h>

class CCreateMultiplayerGameServerPage;
class CCreateMultiplayerGameGameplayPage;
class CCreateMultiplayerGameBotPage;
class CPNGImagePanel;
namespace vgui { class PanelListPanel; }

//-----------------------------------------------------------------------------
// Purpose: GMod-style fullscreen dialog for launching a listenserver
//-----------------------------------------------------------------------------
class CCreateMultiplayerGameDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CCreateMultiplayerGameDialog,  vgui::Frame );

public:
	CCreateMultiplayerGameDialog(vgui::Panel *parent);
	~CCreateMultiplayerGameDialog();

	// map card callbacks
	virtual void OnMapSelected( const char *pszMapName );

protected:
	virtual void OnKeyCodePressed( vgui::KeyCode code );
	virtual void OnCommand( const char *command );
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void PerformLayout();

private:
	void BuildGameModeList();
	void BuildMapGrid();
	void LoadMapList();
	void LoadMaps( const char *pszPathID );
	void RefreshSelection( void );
	bool IsRandomMapSelected();
	const char *GetMapName();

	const char *GetHostName();
	const char *GetPassword();
	int GetMaxPlayers();
	void SaveConfig();

	void CreateGame();

private:
	vgui::PanelListPanel *m_pGameModeList;
	vgui::PanelListPanel *m_pMapList;
	vgui::TextEntry *m_pHostName;
	vgui::TextEntry *m_pPassword;
	vgui::ComboBox *m_pMaxPlayers;
	vgui::Label *m_pSelectedMapLabel;
	vgui::Button *m_pStartButton;
	vgui::Button *m_pBackButton;
	vgui::Label *m_pTitleLabel;
	vgui::Label *m_pHostNameLabel;
	vgui::Label *m_pPasswordLabel;
	vgui::Label *m_pMaxPlayersLabel;

	CUtlVector<char*> m_MapNames; // own the strings

	KeyValues *m_pSavedData;

	char m_szSelectedMap[256];

	bool m_bBotsEnabled;
	bool m_bBuilt;
};

#endif // CREATEMULTIPLAYERGAMEDIALOG_H
