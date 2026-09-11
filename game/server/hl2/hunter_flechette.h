//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The hunter's explosive flechette, ported out of the EP2 hunter NPC
//			(game/server/episodic/npc_hunter.cpp) so that HL2SB can spawn it as
//			a standalone, networked entity:
//
//				local ent = ents.Create( "hunter_flechette" )
//
//			It lives in game/server/hl2/ rather than game/server/episodic/
//			because HL2SB only builds server_base.vpc + server_hl2mp.vpc +
//			server_lua.vpc, and CNPC_Hunter still lives in npc_hunter.cpp and
//			uses this class for its flechette volley.
//
//=============================================================================//

#ifndef HUNTER_FLECHETTE_H
#define HUNTER_FLECHETTE_H
#ifdef _WIN32
#pragma once
#endif

#include "props.h"			// CPhysicsProp, IParentPropInteraction

//-----------------------------------------------------------------------------
// Flies fast with a slight gravity pull, sticks into walls and props, beeps
// for a moment and then explodes. Puffs bubbles while travelling under water.
//-----------------------------------------------------------------------------
class CHunterFlechette : public CPhysicsProp, public IParentPropInteraction
{
public:
	DECLARE_CLASS( CHunterFlechette, CPhysicsProp );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CHunterFlechette();
	~CHunterFlechette();

	virtual Class_T Classify() { return CLASS_NONE; }

	bool WasThrownBack() { return m_bThrownBack; }

public:
	virtual void Spawn();
	virtual void Activate();
	virtual void Precache();

	// Launch it. vecVelocity is copied, so the caller's vector is not modified.
	void Shoot( const Vector &vecVelocity, bool bBright );
	void SetSeekTarget( CBaseEntity *pTargetEntity );
	void Explode();

	virtual bool CreateVPhysics();

	virtual unsigned int PhysicsSolidMaskForEntity() const;

	static CHunterFlechette *FlechetteCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner = NULL );

	// IParentPropInteraction
	virtual void OnParentCollisionInteraction( parentCollisionInteraction_t eType, int index, gamevcollisionevent_t *pEvent );
	virtual void OnParentPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason );

protected:
	void SetupGlobalModelData();

	void StickTo( CBaseEntity *pOther, trace_t &tr );

	// Common "we are now in flight" setup, shared by Shoot() and by the
	// deferred setup think that picks up a velocity set from Lua.
	void BeginFlight( bool bBright );

	void FlightSetupThink();
	void BubbleThink();
	void DangerSoundThink();
	void ExplodeThink();
	void DopplerThink();
	void SeekThink();

	bool CreateSprites( bool bBright );

	void FlechetteTouch( CBaseEntity *pOther );

	Vector	m_vecShootPosition;
	EHANDLE	m_hSeekTarget;
	bool	m_bThrownBack;
	bool	m_bFlying;
};

#endif // HUNTER_FLECHETTE_H
