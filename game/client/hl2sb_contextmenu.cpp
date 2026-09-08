// hl2sb_contextmenu.cpp
// In-game player-model context menu for HL2SB.
//
// Lessons baked in here:
//   * clientscheme.res only defines "Default", "DefaultSmall",
//     "DefaultVerySmall".  GetFont("DefaultBold") is INVALID_FONT and the
//     control then draws no text at all.
//   * vgui only calls Think()/OnThink() on panels registered with
//     ivgui()->AddTickSignal().  Without it the click polling never runs.
//   * CModelPanel::SwapModel() bails out when m_pModelInfo is NULL, and
//     m_pModelInfo only comes from ParseModelInfo(), i.e. from ApplySettings()
//     seeing a "model" sub-key.
//   * A .res "xpos c-600" is evaluated against a parent that is still 0x0
//     during construction, so the frame lands in the corner.  Only wide/tall
//     are read from the .res; centring is done against the real screen.
//   * CModelPanel::CalculateFrameDistance() frames from the panel size at
//     SetupModel() time and is unreliable for custom models, so the camera is
//     fitted manually from the model's render bounds instead.

#include "cbase.h"
#include "hl2sb_contextmenu.h"
#include "hl2sb_model_config.h"
#include "hands_model_mapping.h"
#include "c_baseplayer.h"
#include "ienginevgui.h"
#include "game_controls/baseviewport.h"
#include "cdll_int.h"
#include "iclientmode.h"
#include "engine/ivmodelinfo.h"
#include "studio.h"

#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui/IVGui.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

extern IVEngineClient *engine;
extern IViewPort *gViewPortInterface;

static CHL2SBContextMenu *g_pHL2SBContextMenu = NULL;
CHL2SBContextMenu *HL2SB_GetContextMenu() { return g_pHL2SBContextMenu; }

//-----------------------------------------------------------------------------
// Tunables — defined in cfg/hl2sb_config.cfg (exec'd from autoexec.cfg).
// No FCVAR_ARCHIVE so the cfg file stays authoritative every launch.
//-----------------------------------------------------------------------------
static ConVar hl2sb_ctx_window_width ( "hl2sb_ctx_window_width",  "1200", 0, "Context menu window width in pixels" );
static ConVar hl2sb_ctx_window_height( "hl2sb_ctx_window_height", "800",  0, "Context menu window height in pixels" );
static ConVar hl2sb_ctx_list_ratio   ( "hl2sb_ctx_list_ratio",    "0.44", 0, "Model list width as a fraction of the window" );
static ConVar hl2sb_ctx_anim_ratio   ( "hl2sb_ctx_anim_ratio",    "0.34", 0, "Animation list height as a fraction of the right column" );
static ConVar hl2sb_ctx_fov          ( "hl2sb_ctx_fov",           "54",   0, "Preview camera FOV in degrees" );
static ConVar hl2sb_ctx_fit_padding  ( "hl2sb_ctx_fit_padding",   "1.0",  0, "Empty space around the model when auto-fitting (lower = closer)" );
static ConVar hl2sb_ctx_default_zoom ( "hl2sb_ctx_default_zoom",  "1.0",  0, "Starting zoom multiplier for the preview camera" );
static ConVar hl2sb_ctx_wheel_step   ( "hl2sb_ctx_wheel_step",    "0.08", 0, "Zoom change per mouse-wheel notch" );
static ConVar hl2sb_ctx_zoom_min     ( "hl2sb_ctx_zoom_min",      "0.25", 0, "Minimum preview zoom multiplier" );
static ConVar hl2sb_ctx_zoom_max     ( "hl2sb_ctx_zoom_max",      "4.0",  0, "Maximum preview zoom multiplier" );
static ConVar hl2sb_ctx_rotate_speed ( "hl2sb_ctx_rotate_speed",  "0.5",  0, "Model yaw degrees per pixel of drag" );
static ConVar hl2sb_ctx_default_anim ( "hl2sb_ctx_default_anim",  "walk", 0, "Preferred default preview animation sequence" );
static ConVar hl2sb_ctx_text_r       ( "hl2sb_ctx_text_r",        "220",  0, "Model/animation list text colour red" );
static ConVar hl2sb_ctx_text_g       ( "hl2sb_ctx_text_g",        "220",  0, "Model/animation list text colour green" );
static ConVar hl2sb_ctx_text_b       ( "hl2sb_ctx_text_b",        "220",  0, "Model/animation list text colour blue" );

#define SECTION_MODELS	1
#define SECTION_ANIMS	2
#define DEFAULT_W		1200
#define DEFAULT_H		800
#define MARGIN			4

// Preferred default animations, in order.  Locomotion first: a static idle
// bind pose looks "abstract" on custom models, a walk cycle reads much better.
static const char *s_pDefaultAnimNames[] =
{
	"walk", "walk_all", "Walk", "walk_all_01", "ACT_WALK", "walk_ar2",
	"run", "run_all", "Run",
	"idle", "idle_all_01", "idle_stand", "Idle_Stand", "ACT_IDLE",
	NULL
};

static const char *ShortName( const char *p )
{
	if ( !p ) return "(none)";
	const char *s = Q_strrchr( p, '/' );
	if ( !s ) s = Q_strrchr( p, '\\' );
	return s ? s + 1 : p;
}

//-----------------------------------------------------------------------------
// Localization helpers.  Tokens live in resource/hl2sb_english.txt and
// resource/hl2sb_schinese.txt (both UTF-16LE + BOM, tokens inside "Tokens").
//-----------------------------------------------------------------------------
static void SetLocalizedLabel( vgui::Label *pLabel, const char *pszToken, const char *pszArg1 = NULL )
{
	if ( !pLabel || !pszToken ) return;

	wchar_t *pwszFormat = g_pVGuiLocalize ? g_pVGuiLocalize->Find( pszToken ) : NULL;
	if ( !pwszFormat )
	{
		// Missing token — show the token itself so the gap is obvious.
		pLabel->SetText( pszToken );
		return;
	}

	if ( pszArg1 && pszArg1[0] )
	{
		wchar_t wszArg[512];
		g_pVGuiLocalize->ConvertANSIToUnicode( pszArg1, wszArg, sizeof( wszArg ) );

		wchar_t wszBuf[1024];
		g_pVGuiLocalize->ConstructString( wszBuf, sizeof( wszBuf ), pwszFormat, 1, wszArg );
		pLabel->SetText( wszBuf );
	}
	else
	{
		pLabel->SetText( pwszFormat );
	}
}

static void SetLocalizedButton( vgui::Button *pBtn, const char *pszToken )
{
	if ( !pBtn || !pszToken ) return;

	wchar_t *pwsz = g_pVGuiLocalize ? g_pVGuiLocalize->Find( pszToken ) : NULL;
	if ( pwsz )
		pBtn->SetText( pwsz );
	else
		pBtn->SetText( pszToken );
}

static const wchar_t *LocalizedW( const char *pszToken, const wchar_t *pwszFallback )
{
	wchar_t *pwsz = g_pVGuiLocalize ? g_pVGuiLocalize->Find( pszToken ) : NULL;
	return pwsz ? pwsz : pwszFallback;
}

//=============================================================================
// CHL2SBModelPreview
//=============================================================================
CHL2SBModelPreview::CHL2SBModelPreview( vgui::Panel *pParent, const char *pName )
	: BaseClass( pParent, pName )
{
	ResetState();
	SetMouseInputEnabled( true );
}

void CHL2SBModelPreview::ResetState()
{
	m_bDragging = false;
	m_bMoved = false;
	m_nDragStartX = 0;
	m_nLastX = 0;
	m_nSeqIndex = -1;
	m_flBaseDist = 150.0f;
	m_flZoomMul = hl2sb_ctx_default_zoom.GetFloat();
	m_vecBaseOffset.Init( 0, 0, 0 );
}

// Fit the whole model in frame, with padding, centred in the viewport.
// The computed distance is stored as m_flBaseDist so the user's zoom survives
// a re-fit (level change, panel resize, model swap).
//
// Bounds source order matters: ported/repacked models frequently ship a bogus
// static render bounds, which made the camera end up inside the mesh.  The
// studio hull is what the engine itself trusts, so it is preferred; the static
// render bounds are only a last resort.
void CHL2SBModelPreview::FitCameraToModel()
{
	if ( !m_pModelInfo || !m_hModel.Get() )
		return;

	Vector vecMin( 0, 0, 0 ), vecMax( 0, 0, 0 );
	const char *pszSource = "none";

	CStudioHdr *pHdr = m_hModel->GetModelPtr();
	if ( pHdr )
	{
		vecMin = pHdr->hull_min();
		vecMax = pHdr->hull_max();
		pszSource = "hull";

		Vector vecViewMin = pHdr->view_bbmin();
		Vector vecViewMax = pHdr->view_bbmax();
		if ( !VectorCompare( vec3_origin, vecViewMin ) || !VectorCompare( vec3_origin, vecViewMax ) )
		{
			VectorMin( vecViewMin, vecMin, vecMin );
			VectorMax( vecViewMax, vecMax, vecMax );
			pszSource = "hull+view_bb";
		}
	}

	Vector vecSize = vecMax - vecMin;
	float flMaxDim = MAX( vecSize.x, MAX( vecSize.y, vecSize.z ) );

	// Nothing usable in the studio header -> fall back to the static bounds.
	if ( flMaxDim < 1.0f )
	{
		const model_t *pModel = modelinfo->GetModel( m_hModel->GetModelIndex() );
		if ( pModel )
		{
			modelinfo->GetModelRenderBounds( pModel, vecMin, vecMax );
			vecSize = vecMax - vecMin;
			flMaxDim = MAX( vecSize.x, MAX( vecSize.y, vecSize.z ) );
			pszSource = "render_bounds";
		}
	}

	if ( flMaxDim < 1.0f )
	{
		flMaxDim = 72.0f;			// standard HL2 humanoid height
		vecMin.Init( -16, -16, 0 );
		vecMax.Init( 16, 16, 72 );
		pszSource = "fallback";
	}

	Vector vecCenter = ( vecMax + vecMin ) * 0.5f;

	float flHalfFOV = DEG2RAD( m_nFOV * 0.5f );
	float flDist = ( flMaxDim * 0.5f ) / tan( flHalfFOV );
	flDist *= hl2sb_ctx_fit_padding.GetFloat();
	if ( flDist < 30.0f ) flDist = 30.0f;

	m_flBaseDist = flDist;
	m_vecBaseOffset.y = -vecCenter.y;
	m_vecBaseOffset.z = -vecCenter.z;

	ApplyZoom();

	Msg( "[HL2SB] Preview fit: src=%s dim=%.1f dist=%.1f center=(%.1f %.1f %.1f)\n",
		pszSource, flMaxDim, flDist, vecCenter.x, vecCenter.y, vecCenter.z );
}

void CHL2SBModelPreview::ApplyZoom()
{
	if ( !m_pModelInfo )
		return;

	m_pModelInfo->m_vecOriginOffset.x = m_flBaseDist * m_flZoomMul;
	m_pModelInfo->m_vecOriginOffset.y = m_vecBaseOffset.y;
	m_pModelInfo->m_vecOriginOffset.z = m_vecBaseOffset.z;
	m_pModelInfo->m_vecViewportOffset.Init();
}

bool CHL2SBModelPreview::ApplyDefaultAnim()
{
	if ( !m_hModel.Get() )
		return false;

	CStudioHdr *pHdr = m_hModel->GetModelPtr();
	if ( !pHdr )
		return false;

	int nCount = pHdr->GetNumSeq();

	// Pass 0: the configured preference (hl2sb_ctx_default_anim).
	const char *pszPreferred = hl2sb_ctx_default_anim.GetString();
	if ( pszPreferred && pszPreferred[0] )
	{
		int seq = m_hModel->LookupSequence( pszPreferred );
		if ( seq >= 0 )
		{
			PlaySequence( seq );
			return true;
		}
	}

	// Pass 1: exact name match from the preference table.
	for ( int i = 0; s_pDefaultAnimNames[i]; i++ )
	{
		int seq = m_hModel->LookupSequence( s_pDefaultAnimNames[i] );
		if ( seq >= 0 )
		{
			PlaySequence( seq );
			return true;
		}
	}

	// Pass 2: any sequence whose name contains "walk" or "run".
	for ( int i = 0; i < nCount; i++ )
	{
		const char *pszName = m_hModel->GetSequenceName( i );
		if ( pszName && ( Q_stristr( pszName, "walk" ) || Q_stristr( pszName, "run" ) ) )
		{
			PlaySequence( i );
			return true;
		}
	}

	// Pass 3: any sequence containing "idle".
	for ( int i = 0; i < nCount; i++ )
	{
		const char *pszName = m_hModel->GetSequenceName( i );
		if ( pszName && Q_stristr( pszName, "idle" ) )
		{
			PlaySequence( i );
			return true;
		}
	}

	// Pass 4: first sequence so the model at least moves.
	if ( nCount > 0 )
	{
		PlaySequence( 0 );
		return true;
	}
	return false;
}

void CHL2SBModelPreview::CycleAnimation()
{
	if ( !m_hModel.Get() )
		return;

	CStudioHdr *pHdr = m_hModel->GetModelPtr();
	if ( !pHdr )
		return;

	int nCount = pHdr->GetNumSeq();
	if ( nCount <= 0 )
		return;

	PlaySequence( ( m_nSeqIndex + 1 ) % nCount );
}

bool CHL2SBModelPreview::PlaySequence( int nSeq )
{
	if ( !m_hModel.Get() || nSeq < 0 )
		return false;

	CStudioHdr *pHdr = m_hModel->GetModelPtr();
	if ( !pHdr || nSeq >= pHdr->GetNumSeq() )
		return false;

	m_hModel->ResetSequence( nSeq );
	m_hModel->SetCycle( 0 );
	m_nSeqIndex = nSeq;

	const char *pszName = m_hModel->GetSequenceName( nSeq );
	Msg( "[HL2SB] Preview anim: seq %d/%d '%s'\n",
		nSeq + 1, pHdr->GetNumSeq(), pszName ? pszName : "?" );
	return true;
}

void CHL2SBModelPreview::OnMousePressed( MouseCode code )
{
	if ( code == MOUSE_LEFT )
	{
		int x, y;
		input()->GetCursorPos( x, y );
		m_bDragging = true;
		m_bMoved = false;
		m_nDragStartX = x;
		m_nLastX = x;
		return;
	}
	BaseClass::OnMousePressed( code );
}

void CHL2SBModelPreview::OnCursorMoved( int x, int y )
{
	if ( !m_bDragging )
		return;

	int dx = x - m_nLastX;
	m_nLastX = x;

	if ( abs( x - m_nDragStartX ) > 3 )
		m_bMoved = true;

	if ( m_pModelInfo && dx != 0 )
		m_pModelInfo->m_vecAbsAngles.y += dx * hl2sb_ctx_rotate_speed.GetFloat();
}

void CHL2SBModelPreview::OnMouseReleased( MouseCode code )
{
	if ( code == MOUSE_LEFT && m_bDragging )
	{
		m_bDragging = false;
		if ( !m_bMoved )
			CycleAnimation();		// plain click -> next animation
		return;
	}
	BaseClass::OnMouseReleased( code );
}

// Mouse wheel zooms the preview camera.
void CHL2SBModelPreview::OnMouseWheeled( int delta )
{
	m_flZoomMul -= delta * hl2sb_ctx_wheel_step.GetFloat();
	float flMin = hl2sb_ctx_zoom_min.GetFloat();
	float flMax = hl2sb_ctx_zoom_max.GetFloat();
	if ( m_flZoomMul < flMin ) m_flZoomMul = flMin;
	if ( m_flZoomMul > flMax ) m_flZoomMul = flMax;

	ApplyZoom();
	Msg( "[HL2SB] Preview zoom: %.2f (dist %.1f)\n", m_flZoomMul, m_flBaseDist * m_flZoomMul );
}

//=============================================================================
// CHL2SBContextMenu
//=============================================================================
CHL2SBContextMenu::CHL2SBContextMenu( IViewPort *pViewPort )
	: BaseClass( NULL, "ContextMenuDialog" )
{
	m_pViewPort = pViewPort;
	m_nSelectedModelIndex = -1;
	m_nLastSelectedID = -1;
	m_nLastModelCount = 0;
	m_nLastAnimID = -1;
	m_nLastFitModelIndex = -1;
	m_nLastFitWide = 0;
	m_nLastFitTall = 0;
	m_bPreviewReady = false;
	m_bAnimApplied = false;
	m_bAnimListBuilt = false;

	SetTitleBarVisible( false );
	SetMoveable( false );
	SetSizeable( false );
	SetProportional( false );
	SetScheme( "ClientScheme" );
	SetKeyBoardInputEnabled( false );
	SetMouseInputEnabled( true );
	SetPaintBackgroundEnabled( true );
	SetBgColor( Color( 30, 30, 30, 245 ) );

	LoadControlSettings( "resource/ContextMenu.res" );
	int nWide = GetWide(), nTall = GetTall();
	if ( nWide < 200 || nTall < 200 )
	{
		nWide = hl2sb_ctx_window_width.GetInt();
		nTall = hl2sb_ctx_window_height.GetInt();
	}
	SetSize( nWide, nTall );
	{
		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		SetPos( ( sw - nWide ) / 2, ( sh - nTall ) / 2 );
	}

	// --- model list (left) ---
	m_pModelList = new SectionedListPanel( this, "ModelList" );
	m_pModelList->SetProportional( false );
	m_pModelList->SetPaintBackgroundEnabled( true );
	m_pModelList->SetBgColor( Color( 45, 45, 45, 255 ) );
	m_pModelList->SetDrawHeaders( false );
	m_pModelList->SetClickable( true );
	m_pModelList->SetVerticalScrollbar( true );
	m_pModelList->AddSection( SECTION_MODELS, "" );
	m_pModelList->AddColumnToSection( SECTION_MODELS, "name",
		LocalizedW( "#HL2SB_ContextMenu_ModelCol", L"Model" ), 0, DEFAULT_W );

	// --- 3D preview (right, upper) ---
	m_pPreviewTitle = new Label( this, "PreviewTitle", "#HL2SB_ContextMenu_PreviewTitle" );
	m_pPreviewTitle->SetProportional( false );
	m_pPreviewTitle->SetContentAlignment( Label::a_center );

	m_pModelPreview = new CHL2SBModelPreview( this, "ModelPreview" );
	m_pModelPreview->SetProportional( false );
	m_pModelPreview->SetPaintBackgroundEnabled( false );

	// Synthetic "model" block — without it m_pModelInfo stays NULL and
	// SwapModel() does nothing at all.
	{
		KeyValues *pKV = new KeyValues( "ModelPreview" );
		pKV->SetInt( "fov", hl2sb_ctx_fov.GetInt() );

		KeyValues *pModel = pKV->FindKey( "model", true );
		pModel->SetString( "modelname", "models/player/group01/male_01.mdl" );
		pModel->SetInt( "skin", 0 );
		pModel->SetString( "angles_x", "0" );
		pModel->SetString( "angles_y", "180" );
		pModel->SetString( "angles_z", "0" );
		pModel->SetString( "origin_x", "150" );
		pModel->SetString( "origin_y", "0" );
		pModel->SetString( "origin_z", "0" );
		pModel->SetInt( "spotlight", 1 );

		m_pModelPreview->ApplySettings( pKV );
		pKV->deleteThis();
	}

	// --- animation list (right, lower) ---
	m_pAnimTitle = new Label( this, "AnimTitle", "#HL2SB_ContextMenu_AnimTitle" );
	m_pAnimTitle->SetProportional( false );

	m_pAnimList = new SectionedListPanel( this, "AnimList" );
	m_pAnimList->SetProportional( false );
	m_pAnimList->SetPaintBackgroundEnabled( true );
	m_pAnimList->SetBgColor( Color( 45, 45, 45, 255 ) );
	m_pAnimList->SetDrawHeaders( false );
	m_pAnimList->SetClickable( true );
	m_pAnimList->SetVerticalScrollbar( true );
	m_pAnimList->AddSection( SECTION_ANIMS, "" );
	m_pAnimList->AddColumnToSection( SECTION_ANIMS, "name",
		LocalizedW( "#HL2SB_ContextMenu_AnimCol", L"Animation" ), 0, DEFAULT_W );

	// --- info labels ---
	m_pStatusLabel = new Label( this, "Status", "#HL2SB_ContextMenu_StatusSelect" );
	m_pStatusLabel->SetProportional( false );

	m_pCurrentModelLabel = new Label( this, "CurModel", "" );
	m_pCurrentModelLabel->SetProportional( false );

	m_pCurrentHandsLabel = new Label( this, "CurHands", "" );
	m_pCurrentHandsLabel->SetProportional( false );

	// --- confirm ---
	m_pConfirmBtn = new Button( this, "ConfirmBtn", "#HL2SB_ContextMenu_Confirm", this, "ConfirmModel" );
	m_pConfirmBtn->SetProportional( false );

	BuildModelList();
	SetVisible( false );

	// Required for OnThink() to ever fire (see file header).
	ivgui()->AddTickSignal( GetVPanel(), 0 );

	g_pHL2SBContextMenu = this;
}

CHL2SBContextMenu::~CHL2SBContextMenu()
{
	if ( g_pHL2SBContextMenu == this ) g_pHL2SBContextMenu = NULL;
}

void CHL2SBContextMenu::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	SetBgColor( Color( 30, 30, 30, 245 ) );

	HFont hFont = pScheme->GetFont( "Default" );

	if ( m_pPreviewTitle ) { m_pPreviewTitle->SetFgColor( Color( 200, 200, 200, 255 ) ); m_pPreviewTitle->SetFont( hFont ); }
	if ( m_pAnimTitle )    { m_pAnimTitle->SetFgColor( Color( 200, 200, 200, 255 ) ); m_pAnimTitle->SetFont( hFont ); }
	if ( m_pStatusLabel )  { m_pStatusLabel->SetFgColor( Color( 120, 180, 240, 255 ) ); m_pStatusLabel->SetFont( hFont ); }
	if ( m_pCurrentModelLabel ) { m_pCurrentModelLabel->SetFgColor( Color( 160, 160, 160, 255 ) ); m_pCurrentModelLabel->SetFont( hFont ); }
	if ( m_pCurrentHandsLabel ) { m_pCurrentHandsLabel->SetFgColor( Color( 160, 160, 160, 255 ) ); m_pCurrentHandsLabel->SetFont( hFont ); }
	if ( m_pConfirmBtn )
	{
		SetLocalizedButton( m_pConfirmBtn, "#HL2SB_ContextMenu_Confirm" );
		m_pConfirmBtn->SetFont( hFont );
		m_pConfirmBtn->SetFgColor( Color( 255, 255, 255, 255 ) );
	}
}

void CHL2SBContextMenu::PerformLayout()
{
	BaseClass::PerformLayout();

	int w = GetWide(), h = GetTall();
	if ( w < 200 || h < 200 ) return;

	int listW = (int)( w * hl2sb_ctx_list_ratio.GetFloat() );
	int prevX = listW + MARGIN * 3;
	int prevW = w - prevX - MARGIN;

	int bottomH = 72;
	int bodyTop = MARGIN;
	int bodyH = h - bottomH - MARGIN * 2;

	// left: model list
	m_pModelList->SetBounds( MARGIN, bodyTop, listW, bodyH );

	// right upper: preview
	int prevTitleH = 18;
	int animBlockH = (int)( bodyH * hl2sb_ctx_anim_ratio.GetFloat() );
	int previewH = bodyH - animBlockH - prevTitleH - 4;
	if ( previewH < 80 ) previewH = 80;

	m_pPreviewTitle->SetBounds( prevX, bodyTop, prevW, prevTitleH );
	m_pModelPreview->SetBounds( prevX, bodyTop + prevTitleH, prevW, previewH );

	// right lower: animation list
	int animY = bodyTop + prevTitleH + previewH + 4;
	m_pAnimTitle->SetBounds( prevX, animY, prevW, prevTitleH );
	m_pAnimList->SetBounds( prevX, animY + prevTitleH, prevW, animBlockH - prevTitleH - 4 );

	// bottom: status + current info + confirm
	int infoY = bodyTop + bodyH + MARGIN;
	m_pStatusLabel->SetBounds( MARGIN, infoY, listW, 20 );
	m_pCurrentModelLabel->SetBounds( MARGIN, infoY + 20, listW, 20 );
	m_pCurrentHandsLabel->SetBounds( MARGIN, infoY + 40, listW, 20 );

	m_pConfirmBtn->SetBounds( prevX, infoY + 8, prevW, 40 );
}

void CHL2SBContextMenu::OnThink()
{
	BaseClass::OnThink();
	if ( !IsVisible() ) return;

	if ( g_nHL2SB_ModelConfigCount != m_nLastModelCount )
		BuildModelList();

	HandleModelListClick();
	HandleAnimListClick();
	RefreshPreviewIfNeeded();
}

void CHL2SBContextMenu::RefreshPreviewIfNeeded()
{
	if ( !m_pModelPreview ) return;

	// Panel now has real bounds -> load whatever is selected / current.
	if ( !m_bPreviewReady && m_pModelPreview->GetWide() > 0 )
	{
		m_bPreviewReady = true;

		if ( m_nSelectedModelIndex >= 0 && m_nSelectedModelIndex < g_nHL2SB_ModelConfigCount )
			LoadPreviewModel( g_HL2SB_ModelConfigs[m_nSelectedModelIndex].szPlayerModel );
		else
		{
			C_BasePlayer *p = C_BasePlayer::GetLocalPlayer();
			if ( p && p->GetModel() )
				LoadPreviewModel( modelinfo->GetModelName( p->GetModel() ) );
		}
		return;
	}

	// Entity exists: keep the camera fitted (re-fit when the model or the
	// panel size changes so the distance stays stable across level changes),
	// pick a default animation, and build the animation menu once.
	if ( m_pModelPreview->m_hModel.Get() )
	{
		int nModelIdx = m_pModelPreview->m_hModel->GetModelIndex();
		int nW = m_pModelPreview->GetWide();
		int nH = m_pModelPreview->GetTall();

		if ( nModelIdx != m_nLastFitModelIndex || nW != m_nLastFitWide || nH != m_nLastFitTall )
		{
			m_nLastFitModelIndex = nModelIdx;
			m_nLastFitWide = nW;
			m_nLastFitTall = nH;
			m_pModelPreview->FitCameraToModel();
		}

		if ( !m_bAnimApplied )
		{
			m_pModelPreview->ApplyDefaultAnim();
			m_bAnimApplied = true;
		}
		if ( !m_bAnimListBuilt )
		{
			BuildAnimList();
			m_bAnimListBuilt = true;
		}
	}
}

void CHL2SBContextMenu::BuildModelList()
{
	if ( !m_pModelList ) return;

	m_pModelList->RemoveAll();
	m_nLastModelCount = g_nHL2SB_ModelConfigCount;

	HL2SB_EnsureModelConfigsLoaded();

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		const HL2SB_ModelConfig_t &cfg = g_HL2SB_ModelConfigs[i];
		KeyValues *kv = new KeyValues( "item" );
		kv->SetString( "name", cfg.szName[0] ? cfg.szName : ShortName( cfg.szConfigFile ) );

		int itemID = m_pModelList->AddItem( SECTION_MODELS, kv );
		m_pModelList->SetItemFgColor( itemID, Color( hl2sb_ctx_text_r.GetInt(), hl2sb_ctx_text_g.GetInt(), hl2sb_ctx_text_b.GetInt(), 255 ) );
		kv->deleteThis();
	}
}

// Fill the animation list from the sequences the MDL actually contains.
void CHL2SBContextMenu::BuildAnimList()
{
	if ( !m_pAnimList || !m_pModelPreview ) return;

	m_pAnimList->RemoveAll();
	m_nLastAnimID = -1;

	if ( !m_pModelPreview->m_hModel.Get() )
		return;

	CStudioHdr *pHdr = m_pModelPreview->m_hModel->GetModelPtr();
	if ( !pHdr )
		return;

	int nCount = pHdr->GetNumSeq();
	int nAdded = 0;

	for ( int i = 0; i < nCount; i++ )
	{
		const char *pszName = m_pModelPreview->m_hModel->GetSequenceName( i );
		if ( !pszName || !pszName[0] )
			continue;

		KeyValues *kv = new KeyValues( "item" );
		kv->SetString( "name", pszName );
		kv->SetInt( "seq", i );

		int itemID = m_pAnimList->AddItem( SECTION_ANIMS, kv );
		m_pAnimList->SetItemFgColor( itemID, Color( hl2sb_ctx_text_r.GetInt(), hl2sb_ctx_text_g.GetInt(), hl2sb_ctx_text_b.GetInt(), 255 ) );
		kv->deleteThis();
		nAdded++;
	}

	Msg( "[HL2SB] Anim list: %d sequences\n", nAdded );
}

void CHL2SBContextMenu::HandleModelListClick()
{
	if ( !m_pModelList ) return;

	int curSel = m_pModelList->GetSelectedItem();
	if ( curSel < 0 || curSel == m_nLastSelectedID ) return;

	m_nLastSelectedID = curSel;

	KeyValues *kv = m_pModelList->GetItemData( curSel );
	if ( !kv ) return;

	const char *pszName = kv->GetString( "name", "" );
	if ( !pszName[0] ) return;

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		if ( !Q_stricmp( g_HL2SB_ModelConfigs[i].szName, pszName ) )
		{
			SelectModel( i );
			return;
		}
	}
}

void CHL2SBContextMenu::HandleAnimListClick()
{
	if ( !m_pAnimList || !m_pModelPreview ) return;

	int curSel = m_pAnimList->GetSelectedItem();
	if ( curSel < 0 || curSel == m_nLastAnimID ) return;

	m_nLastAnimID = curSel;

	KeyValues *kv = m_pAnimList->GetItemData( curSel );
	if ( !kv ) return;

	int nSeq = kv->GetInt( "seq", -1 );
	if ( nSeq < 0 ) return;

	if ( m_pModelPreview->m_hModel.Get() )
	{
		m_pModelPreview->PlaySequence( nSeq );
	}
}

void CHL2SBContextMenu::ShowPanel( bool bShow )
{
	if ( bShow ) OnOpen(); else OnClose();
}

void CHL2SBContextMenu::OnOpen()
{
	int w = GetWide(), h = GetTall();
	int sw, sh;
	surface()->GetScreenSize( sw, sh );
	SetPos( ( sw - w ) / 2, ( sh - h ) / 2 );

	SetVisible( true );
	MoveToFront();
	SetMouseInputEnabled( true );
	RequestFocus();

	m_bPreviewReady = false;
	m_bAnimApplied = false;
	m_bAnimListBuilt = false;
	m_nLastSelectedID = -1;
	m_nLastAnimID = -1;
	m_nLastFitModelIndex = -1;
	m_nLastFitWide = 0;
	m_nLastFitTall = 0;
	if ( m_pModelPreview ) m_pModelPreview->ResetState();

	UpdateCurrentInfo();
	InvalidateLayout( false, true );
}

void CHL2SBContextMenu::OnClose()
{
	SetVisible( false );
	SetMouseInputEnabled( false );
	m_nLastSelectedID = -1;
	m_nLastAnimID = -1;
}

void CHL2SBContextMenu::Update() { UpdateCurrentInfo(); }

void CHL2SBContextMenu::OnCommand( const char *command )
{
	if ( FStrEq( command, "ConfirmModel" ) )
	{
		if ( m_nSelectedModelIndex >= 0 && m_nSelectedModelIndex < g_nHL2SB_ModelConfigCount )
		{
			const HL2SB_ModelConfig_t &cfg = g_HL2SB_ModelConfigs[m_nSelectedModelIndex];

			char cmd[256];
			Q_snprintf( cmd, sizeof(cmd), "hl2sb_setmodel %s", cfg.szName );
			engine->ClientCmd( cmd );

			SetLocalizedLabel( m_pStatusLabel, "#HL2SB_ContextMenu_StatusConfirmed", cfg.szName );
			Msg( "[HL2SB] Context menu: confirmed '%s' (%s)\n", cfg.szName, cfg.szPlayerModel );
		}
		else
		{
			SetLocalizedLabel( m_pStatusLabel, "#HL2SB_ContextMenu_StatusNeedSelect" );
		}
		return;
	}

	BaseClass::OnCommand( command );
}

void CHL2SBContextMenu::UpdateCurrentInfo()
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	if ( m_pCurrentModelLabel )
	{
		if ( pPlayer && pPlayer->GetModel() )
		{
			const char *psz = modelinfo->GetModelName( pPlayer->GetModel() );
			SetLocalizedLabel( m_pCurrentModelLabel, "#HL2SB_ContextMenu_CurrentModel",
				psz ? ShortName( psz ) : "?" );
		}
		else
		{
			SetLocalizedLabel( m_pCurrentModelLabel, "#HL2SB_ContextMenu_CurrentModel",
				"(none)" );
		}
	}

	if ( m_pCurrentHandsLabel )
	{
		const char *psz = HL2SB_GetActiveHandsModel();
		SetLocalizedLabel( m_pCurrentHandsLabel, "#HL2SB_ContextMenu_CurrentHands",
			( psz && psz[0] ) ? ShortName( psz ) : "(default)" );
	}
}

void CHL2SBContextMenu::SelectModel( int nIndex )
{
	if ( nIndex < 0 || nIndex >= g_nHL2SB_ModelConfigCount ) return;

	const HL2SB_ModelConfig_t *pCfg = &g_HL2SB_ModelConfigs[nIndex];
	if ( !pCfg->szPlayerModel[0] ) return;

	m_nSelectedModelIndex = nIndex;
	m_bAnimApplied = false;
	m_bAnimListBuilt = false;

	LoadPreviewModel( pCfg->szPlayerModel );

	SetLocalizedLabel( m_pStatusLabel, "#HL2SB_ContextMenu_StatusSelected", pCfg->szName );
}

void CHL2SBContextMenu::LoadPreviewModel( const char *pszPath )
{
	if ( !m_pModelPreview || !pszPath || !pszPath[0] ) return;

	if ( !m_pModelPreview->m_pModelInfo )
	{
		Msg( "[HL2SB] WARNING: preview m_pModelInfo is NULL - cannot preview\n" );
		return;
	}

	// The client must know the model or InitializeAsClientEntity() fails
	// silently and the panel stays black.
	if ( modelinfo->GetModelIndex( pszPath ) == -1 )
	{
		Msg( "[HL2SB] Preview: loading model '%s'\n", pszPath );
		engine->LoadModel( pszPath, false );
	}

	m_pModelPreview->ResetState();
	m_pModelPreview->SwapModel( pszPath );

	if ( m_pModelPreview->m_pModelInfo )
	{
		m_pModelPreview->m_pModelInfo->m_vecAbsAngles.Init( 0, 180, 0 );
		m_pModelPreview->m_pModelInfo->m_bUseSpotlight = true;
		m_pModelPreview->m_pModelInfo->m_nSkin = 0;
	}

	m_pModelPreview->SetPanelDirty();

	// Rebuild the animation menu for the new model once the entity exists.
	if ( m_pAnimList ) m_pAnimList->RemoveAll();
	m_nLastAnimID = -1;

	Msg( "[HL2SB] Preview model: %s\n", pszPath );
}

void CHL2SBContextMenu::OnKeyCodePressed( KeyCode code )
{
	if ( code == KEY_ESCAPE )
	{
		OnClose();
		return;
	}
	BaseClass::OnKeyCodePressed( code );
}

//=============================================================================
// +context_menu / -context_menu — direct show/hide
//=============================================================================
static void IN_ContextMenuDown( const CCommand &args )
{
	CHL2SBContextMenu *pMenu = HL2SB_GetContextMenu();
	if ( !pMenu )
	{
		CBaseViewport *pVP = dynamic_cast<CBaseViewport *>( g_pClientMode->GetViewport() );
		if ( pVP )
		{
			IViewPortPanel *p = pVP->FindPanelByName( PANEL_CONTEXT_MENU );
			if ( !p )
			{
				p = pVP->CreatePanelByName( PANEL_CONTEXT_MENU );
				if ( p ) pVP->AddNewPanel( p, PANEL_CONTEXT_MENU );
			}
			pMenu = HL2SB_GetContextMenu();
		}
	}
	if ( pMenu && !pMenu->IsVisible() )
		pMenu->OnOpen();
}

static void IN_ContextMenuUp( const CCommand &args )
{
	CHL2SBContextMenu *pMenu = HL2SB_GetContextMenu();
	if ( pMenu && pMenu->IsVisible() )
		pMenu->OnClose();
}

static ConCommand cc_down( "+context_menu", IN_ContextMenuDown, "Hold to open HL2SB context menu" );
static ConCommand cc_up( "-context_menu", IN_ContextMenuUp, "Release to close HL2SB context menu" );
