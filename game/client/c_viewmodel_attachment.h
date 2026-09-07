//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for GMod-style c_hands.
//          The hands model is a proper client entity, parented to the weapon
//          viewmodel with EF_BONEMERGE. Matching bones (ValveBiped.* on c_arms
//          rigs, with or without the prefix on the weapon) are driven entirely
//          by the weapon viewmodel's animation, which is what aligns the arms
//          with the gun - the same way GMod does it.
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
// Purpose: Entity that renders a hands/arms model bonemerged onto a viewmodel
//-----------------------------------------------------------------------------
class C_ViewmodelAttachment : public C_BaseAnimating
{
	DECLARE_CLASS( C_ViewmodelAttachment, C_BaseAnimating );
public:
	C_ViewmodelAttachment( void );
	~C_ViewmodelAttachment( void );

	// Initialize as a proper client entity and load the hands model.
	// Returns false if the model could not be registered at all.
	bool SetHandsModel( const char *pszModelName );

	// Attach to a viewmodel entity (SetParent + EF_BONEMERGE follow).
	void AttachToViewmodel( C_BaseViewModel *pViewModel );

	// Detach from viewmodel
	void DetachFromViewmodel( void );

	bool IsAttached( void ) const { return m_bAttached; }

	// Per-frame sync with the parent viewmodel: if the hands model has a
	// sequence with the same name as the viewmodel's current sequence, play it
	// with the same cycle/playback rate (GMod fallback for weapons whose
	// animations don't drive the arm bones).
	void SyncToViewModel( C_BaseViewModel *pViewModel );

	// Override drawing to use bonemerge from parent
	virtual int DrawModel( int flags );

	// The arms are drawn MANUALLY by C_BaseViewModel::DrawModel inside the
	// viewmodel render pass. They must never be picked up by the world
	// renderer, so this entity always answers "do not draw" to the leaf
	// system. See the implementation for why the one-shot RemoveFromLeafSystem()
	// in SetHandsModel() is not enough.
	virtual bool ShouldDraw( void );

	// Identity check: is this entity currently merged onto pViewModel?
	// The viewmodel uses this so a stale handle can never draw the arms twice.
	bool IsAttachedTo( C_BaseViewModel *pViewModel ) const;

	// Verification helper: is the entity still registered in the leaf system?
	// It must never be - that is exactly what makes the engine draw a second,
	// ghost pair of arms next to the real viewmodel hands.
	bool IsInLeafSystem( void ) const;

	// The model path this entity was created for (for status/debug output).
	const char *GetHandsModelName( void ) const { return m_szHandsModelName; }

	// The hands key ("model|skin|body") this entity implements. Each weapon
	// viewmodel owns exactly one entity, so the "already attached" cache has to
	// live on the entity itself - a single global cache let one viewmodel's
	// release silently change another viewmodel's attach decision and rebuild a
	// second pair of arms.
	void SetHandsKey( const char *pszKey );
	const char *GetHandsKey( void ) const { return m_szHandsKey; }

	// Override SetupBones to apply the cl_hands_offset_* / cl_hands_angle_*
	// correction (in viewmodel space) after native bonemerge.
	virtual bool SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime );

	// Re-apply model-dependent state (default sequence) when the model changes
	virtual CStudioHdr *OnNewModel( void );

	// Entity is always transmitted to local player
	virtual int ShouldTransmit( const CCheckTransmitInfo *pInfo, const void *pVSPTState );

private:
	// Rigid viewmodel-space correction from the cl_hands_offset_/angle_ cvars,
	// applied uniformly to every bone after the merge.
	void ApplyHandsOffset( void );

	CHandle<C_BaseViewModel> m_hParentViewModel;	// Handle to parent viewmodel
	bool m_bAttached;								// Is currently attached
	int m_iDefaultSequence;							// "proportions"/"idle"/"reference" fallback
	float m_flLastOffsetTime;						// Guard so the offset is applied once per frame
	char m_szHandsModelName[ MAX_PATH ];			// Model this entity was built for (debug)
	char m_szHandsKey[ MAX_PATH + 64 ];				// "model|skin|body" this entity implements
};

#endif // C_VIEWMODEL_ATTACHMENT_H
