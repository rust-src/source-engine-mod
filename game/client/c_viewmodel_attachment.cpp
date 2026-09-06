//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for hands/arms models
//          Relies on Source SDK's native EF_BONEMERGE for bone matching.
//
//=============================================================================//

#include "cbase.h"
#include "c_viewmodel_attachment.h"
#include "c_baseviewmodel.h"
#include "bone_setup.h"
#include "model_types.h"
#include "hands_model_mapping.h"
#include "cliententitylist.h"
#include "gamestringpool.h"
#include "tier0/memdbgon.h"

// ConVar for overriding hands model (defined in hl2sb_model_config.cpp)
extern ConVar cl_hands_model;

// ViewModel-space offset (applied AFTER bone merge, as a rigid whole-model translation)
ConVar cl_hands_offset_x( "cl_hands_offset_x", "0", FCVAR_ARCHIVE, "Hands model ViewModel-space X offset" );
ConVar cl_hands_offset_y( "cl_hands_offset_y", "0", FCVAR_ARCHIVE, "Hands model ViewModel-space Y offset" );
ConVar cl_hands_offset_z( "cl_hands_offset_z", "0", FCVAR_ARCHIVE, "Hands model ViewModel-space Z offset" );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::C_ViewmodelAttachment( void ) : 
	m_hParentViewModel( NULL ),
	m_bAttached( false )
{
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::~C_ViewmodelAttachment( void )
{
	DetachFromViewmodel();
}

//-----------------------------------------------------------------------------
// Purpose: Initialize with a hands model path
//          Uses InitializeAsClientEntity for proper client entity lifecycle
// Input  : pszModelName - Path to the hands/arms model
// Output : Returns true on success, false on failure
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetHandsModel( const char *pszModelName )
{
	if ( !pszModelName )
		return false;

	// Proper client entity initialization
	bool bSuccess = InitializeAsClientEntity( pszModelName, RENDER_GROUP_VIEW_MODEL_OPAQUE );

	// If init fails, remove this entity
	if ( !bSuccess )
	{
		Msg( "[HL2SB-HANDS] SetHandsModel FAILED: %s\n", pszModelName );
		Remove();
		return false;
	}

	Msg( "[HL2SB-HANDS] SetHandsModel OK: %s\n", pszModelName );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Attach to a viewmodel entity using standard Source follow/bonemerge
// Input  : pViewModel - The viewmodel to attach to
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::AttachToViewmodel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel )
		return;

	// Store handle to parent
	m_hParentViewModel = pViewModel;

	// Set owner and initial position from the player
	C_BaseEntity *pOwner = pViewModel->GetOwner();
	if ( pOwner )
	{
		SetOwnerEntity( pOwner );
		SetAbsOrigin( pOwner->GetAbsOrigin() );
	}
	else
	{
		SetAbsOrigin( vec3_origin );
	}
	SetAbsAngles( vec3_angle );

	// Standard follow attachment:
	// SetParent + EF_BONEMERGE + MOVETYPE_NONE
	// SDK's CBoneMergeCache will copy bone matrices from parent by name.
	SetParent( pViewModel );
	AddEffects( EF_BONEMERGE );
	SetMoveType( MOVETYPE_NONE );

	m_bAttached = true;

	Msg( "[HL2SB-HANDS] Attached to ViewModel, BoneMerge enabled\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Detach from viewmodel
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::DetachFromViewmodel( void )
{
	if ( !m_bAttached )
		return;

	// Remove effects
	RemoveEffects( EF_BONEMERGE );

	// Detach from parent
	SetParent( NULL );

	// Remove from client entity list
	RemoveFromLeafSystem();

	m_hParentViewModel = NULL;
	m_bAttached = false;

	Msg( "[HL2SB-HANDS] Detached from ViewModel\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Setup bones - allow native bonemerge, then apply whole-model offset
//          Offset is ViewModel-space, applied uniformly to all bones.
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
{
	// First let native bonemerge do its job
	bool bResult = BaseClass::SetupBones( pBoneToWorldOut, nMaxBones, boneMask, currentTime );

	// Apply uniform offset in viewmodel space after bone merge
	C_BaseViewModel *pViewModel = m_hParentViewModel.Get();
	if ( pViewModel && GetModelPtr() )
	{
		Vector vecOffset( cl_hands_offset_x.GetFloat(), cl_hands_offset_y.GetFloat(), cl_hands_offset_z.GetFloat() );

		if ( vecOffset != vec3_origin )
		{
			int nBones = GetModelPtr()->numbones();

			// Convert offset to world space using the viewmodel's orientation
			Vector vecWorldOffset;
			QAngle vecViewAngles = pViewModel->GetAbsAngles();
			VectorRotate( vecOffset, vecViewAngles, vecWorldOffset );

			for ( int i = 0; i < nBones; i++ )
			{
				matrix3x4_t boneToWorld = m_BoneAccessor.GetBone( i );
				Vector vecPos;
				MatrixPosition( boneToWorld, vecPos );
				vecPos += vecWorldOffset;
				PositionMatrix( vecPos, boneToWorld );
				m_BoneAccessor.GetBoneForWrite( i ) = boneToWorld;
			}
		}
	}

	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: Draw the hands model (inherits viewmodel render state via DrawModel)
// Input  : flags - Drawing flags
// Output : Number of bones rendered
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

	// Use same render settings as parent viewmodel
	float blend = (float)( pViewModel->GetFxBlend() / 255.0f );
	if ( blend <= 0.0f )
		return 0;

	render->SetBlend( blend );

	float color[3];
	pViewModel->GetColorModulation( color );
	render->SetColorModulation( color );

	// Draw with bone transforms set by SetupBones
	int ret = BaseClass::DrawModel( flags );

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
