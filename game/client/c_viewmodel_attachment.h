//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for hands/arms models
//          Uses Source SDK's native EF_BONEMERGE to follow parent viewmodel
//
//=============================================================================//

#ifndef C_VIEWMODEL_ATTACHMENT_H
#define C_VIEWMODEL_ATTACHMENT_H

#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"

class C_BaseViewModel;

//-----------------------------------------------------------------------------
// Purpose: Entity that attaches hands/arms model to a viewmodel
//          Relies entirely on Source SDK's C_BaseAnimating::BuildTransformations
//          and CBoneMergeCache to copy bone matrices from the parent viewmodel
//          by matching bone names (ValveBiped.Bip01_*).
//-----------------------------------------------------------------------------
class C_ViewmodelAttachment : public C_BaseAnimating
{
	DECLARE_CLASS( C_ViewmodelAttachment, C_BaseAnimating );
public:
	C_ViewmodelAttachment( void );
	~C_ViewmodelAttachment( void );

	// Initialize with a hands model path (uses InitializeAsClientEntity)
	bool SetHandsModel( const char *pszModelName );

	// Attach to a viewmodel entity using standard follow/bonemerge
	void AttachToViewmodel( C_BaseViewModel *pViewModel );

	// Detach from viewmodel
	void DetachFromViewmodel( void );

	// Draw via viewmodel render state
	virtual int DrawModel( int flags );

	// Allow native bonemerge first, then apply overall viewmodel-space offset
	virtual bool SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime );

	// Entity is always transmitted to local player
	virtual int ShouldTransmit( const CCheckTransmitInfo *pInfo, const void *pVSPTState );

private:
	CHandle<C_BaseViewModel> m_hParentViewModel;		// Handle to parent viewmodel
	bool m_bAttached;								// Is currently attached
};

#endif // C_VIEWMODEL_ATTACHMENT_H
