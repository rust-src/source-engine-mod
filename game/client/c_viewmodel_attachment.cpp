//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for GMod-style c_hands.
//          Relies on the engine's native EF_BONEMERGE (with optional prefix
//          -stripped bone name matching) for all alignment. No per-bone
//          hand-tuning: GMod c_model weapons animate the ValveBiped arm bones
//          themselves, so merged arms follow the gun automatically. Stock
//          HL2/HL2MP viewmodels use unprefixed "Bip01_*" names, which the
//          lenient merge maps onto the c_arms "ValveBiped.Bip01_*" bones.
//
//=============================================================================//

#include "cbase.h"
#include "c_viewmodel_attachment.h"
#include "c_baseviewmodel.h"
#include "bone_setup.h"
#include "model_types.h"
#include "cliententitylist.h"
#include "gamestringpool.h"
#include "materialsystem/imaterialproxy.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "c_baseplayer.h"
#include "tier0/memdbgon.h"

// Master switch for the c_hands system
ConVar cl_hands( "cl_hands", "1", FCVAR_ARCHIVE, "Show GMod-style viewmodel hands (requires cl_hands_model or a player model mapping)" );

// ConVar for overriding hands model
// Set to "auto" to use automatic mapping, or a model path to force a specific hands model
ConVar cl_hands_model( "cl_hands_model", "auto", FCVAR_ARCHIVE, "Override hands model (auto = use player model mapping)" );

// ViewModel-space offsets applied after bone merge (fine tuning only - for
// correctly rigged c_arms + c_model weapons these should stay at 0)
ConVar cl_hands_offset_x( "cl_hands_offset_x", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space X offset" );
ConVar cl_hands_offset_y( "cl_hands_offset_y", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space Y offset" );
ConVar cl_hands_offset_z( "cl_hands_offset_z", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space Z offset" );
ConVar cl_hands_angle_pitch( "cl_hands_angle_pitch", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space pitch correction (degrees)" );
ConVar cl_hands_angle_yaw( "cl_hands_angle_yaw", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space yaw correction (degrees)" );
ConVar cl_hands_angle_roll( "cl_hands_angle_roll", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space roll correction (degrees)" );

ConVar cl_hands_debug( "cl_hands_debug", "0", FCVAR_ARCHIVE, "Verbose c_hands debug output" );

//-----------------------------------------------------------------------------
// Global registry of live hands-attachment entities. A hands entity leaked from
// a previous session (its owning viewmodel died without releasing it) keeps
// rendering as a "proliferated" second hand - the exact artifact the player
// sees after reconnecting. Registering them lets a spawn/level-change clean
// every leftover at once.
//-----------------------------------------------------------------------------
static CUtlVector< CHandle< C_ViewmodelAttachment > > s_HandsAttachments;

void HL2SB_RegisterHandsAttachment( C_ViewmodelAttachment *pAttach )
{
	if ( pAttach )
		s_HandsAttachments.AddToTail( pAttach );
}

void HL2SB_UnregisterHandsAttachment( C_ViewmodelAttachment *pAttach )
{
	if ( pAttach )
		s_HandsAttachments.FindAndRemove( pAttach );
}

void HL2SB_DestroyAllHandsAttachments( void )
{
	for ( int i = s_HandsAttachments.Count() - 1; i >= 0; --i )
	{
		C_ViewmodelAttachment *p = s_HandsAttachments[ i ].Get();
		if ( p )
			p->Release();
	}
	s_HandsAttachments.RemoveAll();
}

// When enabled, skip merging hands onto viewmodels that already draw their own
// arms (stock HL2/EP2/HL2MP weapon viewmodels, which reference the shared
// "v_hand" material). Merging an extra pair onto those double-draws the arms
// (visible on SLAM, grenade, crowbar, ...). MMOD-style replacement viewmodels
// are gun-only (no v_hand material) and still receive the merged hands.
// HL2SB: on by default - baked-arm weapons (detected by scanning the studio
// texture table for a "v_hand" material, see ViewModelHasBakedArms) get their
// own arms and must not receive a second, mis-merged c_hands attachment (that
// shows up as a detached pair of arms floating after death/respawn). Set to 0
// only if you run gun-only c_* viewmodels with no baked arms everywhere.
ConVar cl_hands_skip_baked_arms( "cl_hands_skip_baked_arms", "1", FCVAR_ARCHIVE, "Don't merge c_hands onto viewmodels that already have their own arms (stock HL2 v_hand models)" );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::C_ViewmodelAttachment( void ) :
	m_hParentViewModel( NULL ),
	m_bAttached( false ),
	m_iDefaultSequence( -1 ),
	m_flLastOffsetTime( -1.0f )
{
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::~C_ViewmodelAttachment( void )
{
	HL2SB_UnregisterHandsAttachment( this );
	DetachFromViewmodel();
}

//-----------------------------------------------------------------------------
// Purpose: Initialize as a client entity and load the hands model
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetHandsModel( const char *pszModelName )
{
	if ( !pszModelName || !pszModelName[0] )
		return false;

	// Register the hands model in the client model pool *by name*. Client-side
	// CBaseEntity::PrecacheModel is just an index lookup, and engine->LoadModel
	// alone can leave the name unindexed: on a fresh connection (where the
	// server's precache of these player-only c_arms models hasn't arrived yet)
	// GetModelIndex returned -1 and InitializeAsClientEntity failed, which
	// dropped the hands and spammed on every reconnect. RegisterDynamicModel
	// guarantees a valid index (bClientSide = don't wait for network precache),
	// and if the model data is still loading asynchronously C_BaseAnimating's
	// own OnNewModel load-callback completes the bones/skins when it arrives.
	int iModelIndex = modelinfo->GetModelIndex( pszModelName );
	if ( iModelIndex == -1 )
	{
		iModelIndex = modelinfo->RegisterDynamicModel( pszModelName, true );
	}
	if ( iModelIndex == -1 )
	{
		Warning( "[HL2SB-HANDS] SetHandsModel: could not register model %s\n", pszModelName );
		return false;
	}

	if ( !InitializeAsClientEntityByIndex( iModelIndex, RENDER_GROUP_OPAQUE_ENTITY ) )
	{
		Warning( "[HL2SB-HANDS] SetHandsModel: InitializeAsClientEntity failed for %s\n", pszModelName );
		return false;
	}

	// We are drawn manually from C_BaseViewModel::DrawModel inside the
	// viewmodel render pass - remove us from the normal leaf-system draws.
	RemoveFromLeafSystem();

	// c_arms rigs merge across rig name conventions (ValveBiped. prefix).
	SetLenientBoneMerge( true );

	// Avoid simulation/solidity nonsense - we only exist to be bonemerged.
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetCollisionGroup( COLLISION_GROUP_NONE );

	// Track this entity so a respawn/level-change can destroy any leftovers.
	HL2SB_RegisterHandsAttachment( this );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Attach to a viewmodel entity using standard Source "follow" method
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::AttachToViewmodel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel )
		return;

	// Store handle to parent
	m_hParentViewModel = pViewModel;

	// Standard follow attachment (see CBaseEntity::FollowEntity):
	// SetParent + MOVETYPE_NONE + not solid + zero local transforms + EF_BONEMERGE
	SetParent( pViewModel );
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetLocalOrigin( vec3_origin );
	SetLocalAngles( vec3_angle );
	// NOTE: no EF_BONEMERGE_FASTCULL - it re-places the render origin at the
	// parent's WorldSpaceCenter (the viewmodel sits at the camera near-plane),
	// which flings the merged arms out of view. We draw the arms manually from
	// C_BaseViewModel::DrawModel, so the leaf-cull optimization isn't needed.
	AddEffects( EF_BONEMERGE );

	m_bAttached = true;

	if ( cl_hands_debug.GetBool() )
	{
		CStudioHdr *pHdr = GetModelPtr();
		CStudioHdr *pVMHdr = pViewModel->GetModelPtr();
		Msg( "[HL2SB-HANDS] Attached to viewmodel %s (hands bones=%d, weapon bones=%d)\n",
			pVMHdr ? pVMHdr->pszName() : "<none>",
			pHdr ? pHdr->numbones() : 0,
			pVMHdr ? pVMHdr->numbones() : 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Detach from viewmodel
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::DetachFromViewmodel( void )
{
	if ( !m_bAttached )
		return;

	RemoveEffects( EF_BONEMERGE );
	SetParent( NULL );

	m_hParentViewModel = NULL;
	m_bAttached = false;
}

//-----------------------------------------------------------------------------
// Purpose: Pick the fallback sequence for a freshly loaded c_arms model.
//          "proportions" (autoplay predelta) fixes bone proportions, "idle" is
//          the usual relaxed pose, "reference" is the bind pose.
//-----------------------------------------------------------------------------
CStudioHdr *C_ViewmodelAttachment::OnNewModel( void )
{
	CStudioHdr *pNewHdr = BaseClass::OnNewModel();

	m_iDefaultSequence = -1;

	CStudioHdr *pHdr = GetModelPtr();
	if ( !pHdr || !pHdr->SequencesAvailable() )
		return pNewHdr;

	int iProp = LookupSequence( "proportions" );
	int iIdle = LookupSequence( "idle" );
	int iRef = LookupSequence( "reference" );

	// Prefer a gripping hold pose ("idle") over "proportions". "proportions" is
	// an autoplay-build pose with the fingers spread for calibration; a c_arms
	// rig resting on it looks like the hands are open/floating beside the gun.
	// "idle" is the relaxed two-handed grip and reads correctly on the weapon.
	m_iDefaultSequence = ( iIdle >= 0 ) ? iIdle : ( ( iRef >= 0 ) ? iRef : iProp );

	if ( m_iDefaultSequence >= 0 && GetSequence() < 0 )
	{
		SetSequence( m_iDefaultSequence );
		SetCycle( 0.0f );
	}

	// The bone merge cache is rebuilt lazily; if it already exists for a
	// previous model, drop it so the next setup rebuilds with lenient matching.
	if ( m_pBoneMergeCache )
	{
		m_pBoneMergeCache->SetLenientNameMatching( true );
	}

	if ( cl_hands_debug.GetBool() )
	{
		Msg( "[HL2SB-HANDS] OnNewModel: %s (%d bones), default sequence %d\n",
			pHdr->pszName(), pHdr->numbones(), m_iDefaultSequence );
	}

	return pNewHdr;
}

//-----------------------------------------------------------------------------
// Purpose: Per-frame animation sync with the parent viewmodel
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::SyncToViewModel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel || !GetModelPtr() || !pViewModel->GetModelPtr() )
		return;

	if ( !GetModelPtr()->SequencesAvailable() )
		return;

	// Lazy (re)compute of the default sequence - OnNewModel may have run before
	// the model data (sequences) was available.
	if ( m_iDefaultSequence < 0 )
	{
		int iProp = LookupSequence( "proportions" );
		int iIdle = LookupSequence( "idle" );
		int iRef = LookupSequence( "reference" );
		m_iDefaultSequence = ( iProp >= 0 ) ? iProp : ( ( iIdle >= 0 ) ? iIdle : iRef );
	}

	const char *pszVMSeq = pViewModel->GetSequenceName( pViewModel->GetSequence() );
	if ( !pszVMSeq || !pszVMSeq[0] )
		return;

	int iSeq = LookupSequence( pszVMSeq );
	if ( iSeq >= 0 )
	{
		// Same-named sequence exists in the hands model: play it in lockstep
		// (this is the fallback that aligns arms on weapons whose animations
		// don't move the arm bones).
		if ( GetSequence() != iSeq )
		{
			SetSequence( iSeq );
		}
		SetCycle( pViewModel->GetCycle() );
		SetPlaybackRate( pViewModel->GetPlaybackRate() );
	}
	else if ( m_iDefaultSequence >= 0 && GetSequence() != m_iDefaultSequence )
	{
		SetSequence( m_iDefaultSequence );
		SetCycle( 0.0f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Setup bones - native bonemerge does all alignment; we only apply the
//          optional viewmodel-space correction afterwards.
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
{
	bool bResult = BaseClass::SetupBones( pBoneToWorldOut, nMaxBones, boneMask, currentTime );

	// Apply at most once per frame (BaseClass::SetupBones early-outs on
	// repeated same-time calls, and the correction is not idempotent).
	if ( m_flLastOffsetTime != currentTime )
	{
		m_flLastOffsetTime = currentTime;
		ApplyHandsOffset();
	}

	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: Rigid viewmodel-space correction from cl_hands_offset_/angle_ cvars
//          newWorld = vmXform * localCorrection * Inverse(vmXform) * oldWorld
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::ApplyHandsOffset( void )
{
	QAngle angOffset(
		cl_hands_angle_pitch.GetFloat(),
		cl_hands_angle_yaw.GetFloat(),
		cl_hands_angle_roll.GetFloat() );
	Vector vecOffset(
		cl_hands_offset_x.GetFloat(),
		cl_hands_offset_y.GetFloat(),
		cl_hands_offset_z.GetFloat() );

	if ( vecOffset == vec3_origin && angOffset == vec3_angle )
		return;

	C_BaseViewModel *pViewModel = m_hParentViewModel.Get();
	if ( !pViewModel )
		return;

	CStudioHdr *pHdr = GetModelPtr();
	if ( !pHdr )
		return;

	// Build the correction in viewmodel space so X/Y/Z stay intuitive
	// regardless of where the viewmodel entity sits in the world.
	matrix3x4_t vmXform = pViewModel->EntityToWorldTransform();
	matrix3x4_t vmInv;
	MatrixInvert( vmXform, vmInv );

	matrix3x4_t localCorrection;
	AngleMatrix( angOffset, vecOffset, localCorrection );

	matrix3x4_t temp, correctionWorld;
	ConcatTransforms( vmXform, localCorrection, temp );
	ConcatTransforms( temp, vmInv, correctionWorld );

	int nBones = pHdr->numbones();
	for ( int i = 0; i < nBones; i++ )
	{
		const matrix3x4_t &oldWorld = m_BoneAccessor.GetBone( i );
		matrix3x4_t newWorld;
		ConcatTransforms( correctionWorld, oldWorld, newWorld );
		m_BoneAccessor.GetBoneForWrite( i ) = newWorld;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw the hands model
//-----------------------------------------------------------------------------
int C_ViewmodelAttachment::DrawModel( int flags )
{
	// Don't draw if not attached or no model
	if ( !m_bAttached || !GetModel() )
		return 0;

	// Don't draw if parent viewmodel isn't visible
	C_BaseViewModel *pViewModel = m_hParentViewModel.Get();
	if ( !pViewModel )
		return 0;

	// Only draw when the parent viewmodel is the owner's currently-active
	// viewmodel. A stale attachment left over from a previous death/respawn or
	// weapon switch is still parented to an old (non-active or orphaned)
	// viewmodel; without this gate it lingers and renders as a detached pair
	// of hands floating in the world after you die.
	C_BasePlayer *pOwner = ToBasePlayer( pViewModel->GetOwner() );
	if ( !pOwner )
		return 0;
	if ( pViewModel != pOwner->GetViewModel( 0 ) )
		return 0;

	// Use same render settings as parent viewmodel
	float blend = (float)( pViewModel->GetFxBlend() / 255.0f );
	if ( blend <= 0.0f )
		return 0;

	render->SetBlend( blend );

	float color[3];
	pViewModel->GetColorModulation( color );
	render->SetColorModulation( color );

	// Draw the merged bones directly. We must NOT call BaseClass::DrawModel here:
	// as an EF_BONEMERGE follower of the viewmodel, C_BaseAnimating::DrawModel would
	// take the "follow" branch (follow->DrawModel(0) to refresh the master, then
	// gate the child render on that returning non-zero). Called with the viewmodel's
	// flags, that reentrant master draw returns 0 (no STUDIO_RENDER) so our arms
	// would never render. InternalDrawModel runs SetupBones (which does the
	// bonemerge) and draws directly - matching the old visible path.
	int ret = InternalDrawModel( flags );

	return ret;
}

//-----------------------------------------------------------------------------
// Purpose: Always transmit to local player
//-----------------------------------------------------------------------------
int C_ViewmodelAttachment::ShouldTransmit( const CCheckTransmitInfo *pInfo, const void *pVSPTState )
{
	// Always transmit - it's attached to a viewmodel
	return FL_EDICT_ALWAYS;
}

//-----------------------------------------------------------------------------
// Purpose: "PlayerColor" material proxy - tints a material (e.g. the GMod
//          c_arms sleeves) by the local player's color, so each player's
//          arms/sleeves render in their own color. This is the proxy the GMod
//          c_arms_citizen_sleeves.vmt references via:
//            Proxies { PlayerColor { resultVar $color2 default 0.2 0.4 0.7 } }
//          HL2SB did not register it, so those materials failed to compile.
//-----------------------------------------------------------------------------
class CPlayerColorProxy : public IMaterialProxy
{
public:
	CPlayerColorProxy( void ) : m_pColor( NULL )
	{
		m_flDefault[0] = 0.2f; m_flDefault[1] = 0.4f; m_flDefault[2] = 0.7f;
	}
	virtual ~CPlayerColorProxy( void ) { }

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		bool found = false;
		const char *pszResultVar = pKeyValues->GetString( "resultVar", "$color2" );
		m_pColor = pMaterial->FindVar( pszResultVar, &found, false );

		// Parse the "default" color (e.g. "0.2 0.4 0.7").
		const char *pszDefault = pKeyValues->GetString( "default", NULL );
		if ( pszDefault )
		{
			sscanf( pszDefault, "%f %f %f", &m_flDefault[0], &m_flDefault[1], &m_flDefault[2] );
		}

		return m_pColor != NULL;
	}

	virtual void OnBind( void *pBindable )
	{
		if ( !m_pColor )
			return;

		float r = m_flDefault[0], g = m_flDefault[1], b = m_flDefault[2];

		// Prefer the local player's render color so sleeves match the model tint.
		C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
		if ( pLocal )
		{
			color32 c = pLocal->GetRenderColor();
			// Only use it if the player actually has a non-default color set;
			// otherwise keep the material's default so it doesn't wash to white.
			if ( c.r != 255 || c.g != 255 || c.b != 255 )
			{
				r = c.r / 255.0f;
				g = c.g / 255.0f;
				b = c.b / 255.0f;
			}
		}

		m_pColor->SetVecValue( r, g, b );
	}

	virtual void Release( void ) { }

	virtual IMaterial *GetMaterial( void ) { return NULL; }

private:
	IMaterialVar	*m_pColor;
	float			m_flDefault[3];
};

EXPOSE_INTERFACE( CPlayerColorProxy, IMaterialProxy, "PlayerColor" IMATERIAL_PROXY_INTERFACE_VERSION );

