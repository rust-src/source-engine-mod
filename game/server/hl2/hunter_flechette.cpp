//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The hunter's explosive flechette.
//
//			Ported from game/server/episodic/npc_hunter.cpp (CHunterFlechette,
//			originally lines ~306-1010) into a standalone, buildable entity so
//			that HL2SB's ported Lua weapon weapon_flechettegun can simply do:
//
//				local ent = ents.Create( "hunter_flechette" )
//				ent:SetVelocity( ... )
//
//			Everything that pulled in the EP2 hunter NPC - or other content
//			HL2SB does not build - has been stripped:
//
//				* no CNPC_Hunter / hunter AI at all.  The optional seek target is
//				  a plain EHANDLE that any caller can set with SetSeekTarget().
//				* no weapon_striderbuster interaction
//				  (StriderBuster_OnFlechetteAttach).
//				* no vehicle_jeep interaction.
//				* no achievements_ep2 / ep2_gamestats.
//
//			IParentPropInteraction *is* available in this engine
//			(game/server/props.h declares it and game/server/props.cpp calls
//			it), so the "thrown back at the hunter" behaviour is kept.
//
//			The behaviour is otherwise the original one: fast flight with a
//			slight gravity pull, stick into walls and props, beep for a moment,
//			then explode - and puff bubbles while travelling through water.
//
//=============================================================================//

#include "cbase.h"
#include "hunter_flechette.h"
#include "func_break.h"			// CBreakable, matGlass
#include "IEffects.h"			// g_pEffects->Sparks()
#include "soundent.h"			// CSoundEnt::InsertSound
#include "particle_parse.h"		// DispatchParticleEffect, PATTACH_*
#include "te_effect_dispatch.h"	// CEffectData, DispatchEffect
#include "recipientfilter.h"	// CPASFilter
#include "util.h"				// UTIL_ImpactTrace / UTIL_Bubbles / UTIL_BubbleTrail
#include "util_shared.h"		// UTIL_PointContents
#include "ai_utils.h"			// AI_GetNearestPlayer
#include "basecombatcharacter.h"	// RadiusDamage
#include "takedamageinfo.h"		// ClearMultiDamage / ApplyMultiDamage

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static const char *HUNTER_FLECHETTE_MODEL = "models/weapons/hunter_flechette.mdl";

// Think contexts
static const char *s_szHunterFlechetteBubbles = "HunterFlechetteBubbles";
static const char *s_szHunterFlechetteSeekThink = "HunterFlechetteSeekThink";
static const char *s_szHunterFlechetteSetupThink = "HunterFlechetteSetupThink";

#define HUNTER_FLECHETTE_WARN_TIME		1.0f

// These are defined next to CNPC_Hunter in game/server/episodic/npc_hunter.cpp,
// which is part of the hl2sb build (server_hl2mp.vpc lists episodic\npc_hunter.cpp)
// and which still uses several of them for its flechette volley scheduling.  Keep
// the single definition there and reference it here rather than duplicating the
// ConVar objects (duplicate definitions of the same global are a link error).
extern ConVar hunter_flechette_speed;
extern ConVar sk_hunter_dmg_flechette;
extern ConVar sk_hunter_flechette_explode_dmg;
extern ConVar sk_hunter_flechette_explode_radius;
extern ConVar hunter_flechette_explode_delay;
extern ConVar hunter_cheap_explosions;

static int s_nHunterFlechetteImpact = -2;
static int s_nFlechetteFuseAttach = -1;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CHunterFlechette *CHunterFlechette::FlechetteCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner )
{
	// Create a new entity with CHunterFlechette private data
	CHunterFlechette *pFlechette = (CHunterFlechette *)CreateEntityByName( "hunter_flechette" );
	if ( !pFlechette )
		return NULL;

	UTIL_SetOrigin( pFlechette, vecOrigin );
	pFlechette->SetAbsAngles( angAngles );
	pFlechette->Spawn();
	pFlechette->Activate();
	pFlechette->SetOwnerEntity( pentOwner );

	return pFlechette;
}


//-----------------------------------------------------------------------------
// HL2SB: DispatchParticleEffect() (game/shared/particle_parse.cpp) hands the
// effect to DispatchEffect(), which builds its own filter - a
// CBroadcastRecipientFilter for follow-attached effects, a CPASFilter
// otherwise - and CTempEntsSystem::DispatchEffect() (game/server/te.cpp:490)
// then runs it through SuppressTE():
//
//     if ( GetSuppressHost() )
//     {
//         if ( !_filter.IgnorePredictionCull() )
//             _filter.RemoveRecipient( GetSuppressHost() );
//         if ( !_filter.GetRecipientCount() )
//             return true;				// the whole effect is thrown away
//     }
//
// A Lua SWEP's PrimaryAttack() runs inside prediction on the server too, so an
// effect dispatched from there has the predicting player removed from a filter
// that (on a listen server) only ever held that one player, and the effect is
// silently swallowed: no trail, no fireball, no smoke and not one line in the
// log.  See AGENTS.md section 5.3.1 and game/server/explode.cpp, which sets the
// same flag for exactly this reason.
//
// The flechette builds its own filter here so the flag can be set before the
// temp entity goes out.  This is the only temp entity the projectile sends -
// EP2's explosion is a "ParticleEffect" dispatch as well, it does not call
// te->Explosion().
//-----------------------------------------------------------------------------
static void DispatchFlechetteParticleAtOrigin( const char *pszParticleName, const Vector &vecOrigin, const QAngle &angAngles )
{
	CEffectData data;
	data.m_nHitBox = GetParticleSystemIndex( pszParticleName );
	data.m_vOrigin = vecOrigin;
	data.m_vStart = vecOrigin;
	data.m_vAngles = angAngles;
	data.m_nEntIndex = 0;
	data.m_fFlags = 0;

	CPASFilter filter( vecOrigin );
	filter.SetIgnorePredictionCull( true );
	DispatchEffect( "ParticleEffect", data, filter );
}

//-----------------------------------------------------------------------------
// Same thing, but for an effect that follows an entity (the flight trail).
//-----------------------------------------------------------------------------
static void DispatchFlechetteParticleOnEntity( const char *pszParticleName, CBaseEntity *pEntity, ParticleAttachment_t iAttachType )
{
	CEffectData data;
	data.m_nHitBox = GetParticleSystemIndex( pszParticleName );
	data.m_nEntIndex = pEntity->entindex();
	data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
	data.m_vOrigin = pEntity->GetAbsOrigin();
	data.m_nDamageType = iAttachType;
	data.m_nAttachmentIndex = -1;

	CPASFilter filter( pEntity->GetAbsOrigin() );
	filter.SetIgnorePredictionCull( true );
	DispatchEffect( "ParticleEffect", data, filter );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CHunterFlechette::CHunterFlechette()
{
	UseClientSideAnimation();

	m_bThrownBack = false;
	m_bFlying = false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CHunterFlechette::~CHunterFlechette()
{
}


//-----------------------------------------------------------------------------
// If set, the flechette will seek unerringly toward the target as it flies.
//-----------------------------------------------------------------------------
void CHunterFlechette::SetSeekTarget( CBaseEntity *pTargetEntity )
{
	if ( pTargetEntity )
	{
		m_hSeekTarget = pTargetEntity;
		SetContextThink( &CHunterFlechette::SeekThink, gpGlobals->curtime, s_szHunterFlechetteSeekThink );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CHunterFlechette::CreateVPhysics()
{
	// Create the object in the physics system
	VPhysicsInitNormal( SOLID_BBOX, FSOLID_NOT_STANDABLE, false );

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
unsigned int CHunterFlechette::PhysicsSolidMaskForEntity() const
{
	return ( BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_HITBOX ) & ~CONTENTS_GRATE;
}


//-----------------------------------------------------------------------------
// Called from CPropPhysics code when we're attached to a physics object.
//-----------------------------------------------------------------------------
void CHunterFlechette::OnParentCollisionInteraction( parentCollisionInteraction_t eType, int index, gamevcollisionevent_t *pEvent )
{
	if ( eType == COLLISIONINTER_PARENT_FIRST_IMPACT )
	{
		m_bThrownBack = true;
		Explode();
	}
}

void CHunterFlechette::OnParentPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason )
{
	m_bThrownBack = true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CHunterFlechette::CreateSprites( bool bBright )
{
	if ( bBright )
	{
		DispatchFlechetteParticleOnEntity( "hunter_flechette_trail_striderbuster", this, PATTACH_ABSORIGIN_FOLLOW );
	}
	else
	{
		DispatchFlechetteParticleOnEntity( "hunter_flechette_trail", this, PATTACH_ABSORIGIN_FOLLOW );
	}

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::Spawn()
{
	Precache();

	SetModel( HUNTER_FLECHETTE_MODEL );
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
	UTIL_SetSize( this, -Vector(1,1,1), Vector(1,1,1) );
	SetSolid( SOLID_BBOX );
	SetGravity( 0.05f );
	SetCollisionGroup( COLLISION_GROUP_PROJECTILE );

	// Make sure we're updated if we're underwater
	UpdateWaterState();

	SetTouch( &CHunterFlechette::FlechetteTouch );

	// Make us glow until we've hit the wall
	m_nSkin = 1;

	// HL2SB: the Lua SWEP only does ents.Create() + SetVelocity(); it never
	// calls Shoot(). Arm a deferred setup think that picks the velocity up once
	// the caller has set it, so the trail, the near-miss sound and the
	// underwater bubbles all still happen. Shoot() clears it again.
	SetContextThink( &CHunterFlechette::FlightSetupThink, gpGlobals->curtime + 0.02f, s_szHunterFlechetteSetupThink );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::Activate()
{
	BaseClass::Activate();
	SetupGlobalModelData();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::SetupGlobalModelData()
{
	if ( s_nHunterFlechetteImpact == -2 )
	{
		s_nHunterFlechetteImpact = LookupSequence( "impact" );
		s_nFlechetteFuseAttach = LookupAttachment( "attach_fuse" );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::Precache()
{
	PrecacheModel( HUNTER_FLECHETTE_MODEL );
	PrecacheModel( "sprites/light_glow02_noz.vmt" );

	PrecacheScriptSound( "NPC_Hunter.FlechetteNearmiss" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteHitBody" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteHitWorld" );
	PrecacheScriptSound( "NPC_Hunter.FlechettePreExplode" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteExplode" );

	PrecacheParticleSystem( "hunter_flechette_trail_striderbuster" );
	PrecacheParticleSystem( "hunter_flechette_trail" );
	PrecacheParticleSystem( "hunter_projectile_explosion_1" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::StickTo( CBaseEntity *pOther, trace_t &tr )
{
	EmitSound( "NPC_Hunter.FlechetteHitWorld" );

	SetMoveType( MOVETYPE_NONE );

	if ( !pOther->IsWorld() )
	{
		SetParent( pOther );
		SetSolid( SOLID_NONE );
		SetSolidFlags( FSOLID_NOT_SOLID );
	}

	// Do an impact effect.
	//Vector vecDir = GetAbsVelocity();
	//float speed = VectorNormalize( vecDir );

	//Vector vForward;
	//AngleVectors( GetAbsAngles(), &vForward );
	//VectorNormalize ( vForward );

	//CEffectData	data;
	//data.m_vOrigin = tr.endpos;
	//data.m_vNormal = vForward;
	//data.m_nEntIndex = 0;
	//DispatchEffect( "BoltImpact", data );

	SetTouch( NULL );

	// We're no longer flying. Stop checking for water volumes.
	SetContextThink( NULL, 0, s_szHunterFlechetteBubbles );
	SetContextThink( NULL, 0, s_szHunterFlechetteSetupThink );

	// Stop seeking.
	m_hSeekTarget = NULL;
	SetContextThink( NULL, 0, s_szHunterFlechetteSeekThink );

	// Get ready to explode.
	// HL2SB: EP2 checked StriderBuster_OnFlechetteAttach() here and stayed inert
	// when the flechette stuck to a strider buster. HL2SB does not build the
	// strider buster, so the flechette always arms itself.
	SetThink( &CHunterFlechette::DangerSoundThink );
	SetNextThink( gpGlobals->curtime + ( hunter_flechette_explode_delay.GetFloat() - HUNTER_FLECHETTE_WARN_TIME ) );

	// Play our impact animation.
	ResetSequence( s_nHunterFlechetteImpact );

	static int s_nImpactCount = 0;
	s_nImpactCount++;
	if ( s_nImpactCount & 0x01 )
	{
		UTIL_ImpactTrace( &tr, DMG_BULLET );

		// Shoot some sparks
		if ( UTIL_PointContents( GetAbsOrigin() ) != CONTENTS_WATER )
		{
			g_pEffects->Sparks( GetAbsOrigin() );
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::FlechetteTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	if ( FClassnameIs( pOther, "hunter_flechette" ) )
		return;

	trace_t	tr;
	tr = BaseClass::GetTouchTrace();

	if ( pOther->m_takedamage != DAMAGE_NO )
	{
		Vector	vecNormalizedVel = GetAbsVelocity();

		ClearMultiDamage();
		VectorNormalize( vecNormalizedVel );

		float flDamage = sk_hunter_dmg_flechette.GetFloat();
		CBreakable *pBreak = dynamic_cast <CBreakable *>(pOther);
		if ( pBreak && ( pBreak->GetMaterialType() == matGlass ) )
		{
			flDamage = MAX( pOther->GetHealth(), flDamage );
		}

		CTakeDamageInfo	dmgInfo( this, GetOwnerEntity(), flDamage, DMG_DISSOLVE | DMG_NEVERGIB );
		CalculateMeleeDamageForce( &dmgInfo, vecNormalizedVel, tr.endpos, 0.7f );
		dmgInfo.SetDamagePosition( tr.endpos );
		pOther->DispatchTraceAttack( dmgInfo, vecNormalizedVel, &tr );

		ApplyMultiDamage();

		// Keep going through breakable glass.
		if ( pOther->GetCollisionGroup() == COLLISION_GROUP_BREAKABLE_GLASS )
			 return;

		SetAbsVelocity( Vector( 0, 0, 0 ) );

		// play body "thwack" sound
		EmitSound( "NPC_Hunter.FlechetteHitBody" );

		StopParticleEffects( this );

		Vector vForward;
		AngleVectors( GetAbsAngles(), &vForward );
		VectorNormalize ( vForward );

		trace_t	tr2;
		UTIL_TraceLine( GetAbsOrigin(),	GetAbsOrigin() + vForward * 128, MASK_BLOCKLOS, pOther, COLLISION_GROUP_NONE, &tr2 );

		if ( tr2.fraction != 1.0f )
		{
			//NDebugOverlay::Box( tr2.endpos, Vector( -16, -16, -16 ), Vector( 16, 16, 16 ), 0, 255, 0, 0, 10 );
			//NDebugOverlay::Box( GetAbsOrigin(), Vector( -16, -16, -16 ), Vector( 16, 16, 16 ), 0, 0, 255, 0, 10 );

			if ( tr2.m_pEnt == NULL || ( tr2.m_pEnt && tr2.m_pEnt->GetMoveType() == MOVETYPE_NONE ) )
			{
				CEffectData	data;

				data.m_vOrigin = tr2.endpos;
				data.m_vNormal = vForward;
				data.m_nEntIndex = tr2.fraction != 1.0f;

				//DispatchEffect( "BoltImpact", data );
			}
		}

		if ( ( ( pOther->GetMoveType() == MOVETYPE_VPHYSICS ) || ( pOther->GetMoveType() == MOVETYPE_PUSH ) ) && ( ( pOther->GetHealth() > 0 ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) ) )
		{
			CPhysicsProp *pProp = dynamic_cast<CPhysicsProp *>( pOther );
			if ( pProp )
			{
				pProp->SetInteraction( PROPINTER_PHYSGUN_NOTIFY_CHILDREN );
			}

			// We hit a physics object that survived the impact. Stick to it.
			StickTo( pOther, tr );
		}
		else
		{
			SetTouch( NULL );
			SetThink( NULL );
			SetContextThink( NULL, 0, s_szHunterFlechetteBubbles );
			SetContextThink( NULL, 0, s_szHunterFlechetteSetupThink );

			UTIL_Remove( this );
		}
	}
	else
	{
		// See if we struck the world
		if ( pOther->GetMoveType() == MOVETYPE_NONE && !( tr.surface.flags & SURF_SKY ) )
		{
			// We hit a physics object that survived the impact. Stick to it.
			StickTo( pOther, tr );
		}
		else if( pOther->GetMoveType() == MOVETYPE_PUSH && FClassnameIs(pOther, "func_breakable") )
		{
			// We hit a func_breakable, stick to it.
			// The MOVETYPE_PUSH is a micro-optimization to cut down on the classname checks.
			StickTo( pOther, tr );
		}
		else
		{
			// Put a mark unless we've hit the sky
			if ( ( tr.surface.flags & SURF_SKY ) == false )
			{
				UTIL_ImpactTrace( &tr, DMG_BULLET );
			}

			UTIL_Remove( this );
		}
	}
}


//-----------------------------------------------------------------------------
// Fixup flechette position when seeking towards a target.
// (In EP2 the only seek target was a strider buster; here it is whatever the
// caller handed to SetSeekTarget().)
//-----------------------------------------------------------------------------
void CHunterFlechette::SeekThink()
{
	if ( m_hSeekTarget )
	{
		Vector vecBodyTarget = m_hSeekTarget->BodyTarget( GetAbsOrigin() );

		Vector vecClosest;
		CalcClosestPointOnLineSegment( GetAbsOrigin(), m_vecShootPosition, vecBodyTarget, vecClosest, NULL );

		Vector vecDelta = vecBodyTarget - m_vecShootPosition;
		VectorNormalize( vecDelta );

		QAngle angShoot;
		VectorAngles( vecDelta, angShoot );

		float flSpeed = hunter_flechette_speed.GetFloat();
		if ( !flSpeed )
		{
			flSpeed = 2500.0f;
		}

		Vector vecVelocity = vecDelta * flSpeed;
		Teleport( &vecClosest, &angShoot, &vecVelocity );

		SetNextThink( gpGlobals->curtime, s_szHunterFlechetteSeekThink );
	}
}


//-----------------------------------------------------------------------------
// Play a near miss sound as we travel past the player.
//-----------------------------------------------------------------------------
void CHunterFlechette::DopplerThink()
{
	CBasePlayer *pPlayer = AI_GetNearestPlayer( GetAbsOrigin() );
	if ( !pPlayer )
		return;

	Vector vecVelocity = GetAbsVelocity();
	VectorNormalize( vecVelocity );

	float flMyDot = DotProduct( vecVelocity, GetAbsOrigin() );
	float flPlayerDot = DotProduct( vecVelocity, pPlayer->GetAbsOrigin() );

	if ( flPlayerDot <= flMyDot )
	{
		EmitSound( "NPC_Hunter.FlechetteNearMiss" );

		// We've played the near miss sound and we're not seeking. Stop thinking.
		SetThink( NULL );
	}
	else
	{
		SetNextThink( gpGlobals->curtime );
	}
}


//-----------------------------------------------------------------------------
// Think every 0.1 seconds to make bubbles if we're flying through water.
//-----------------------------------------------------------------------------
void CHunterFlechette::BubbleThink()
{
	SetNextThink( gpGlobals->curtime + 0.1f, s_szHunterFlechetteBubbles );

	if ( GetWaterLevel()  == 0 )
		return;

	UTIL_BubbleTrail( GetAbsOrigin() - GetAbsVelocity() * 0.1f, GetAbsOrigin(), 5 );
}


//-----------------------------------------------------------------------------
// HL2SB: deferred "we are now flying" setup for callers that create the
// flechette with ents.Create() and only set a velocity - they never call
// Shoot(). Runs from a think, i.e. outside prediction, so the trail/bubble
// effects it starts are not affected by the prediction cull.
//-----------------------------------------------------------------------------
void CHunterFlechette::FlightSetupThink()
{
	if ( m_bFlying )
		return;

	if ( GetAbsVelocity().LengthSqr() < 1.0f )
	{
		// Not launched yet. Keep checking for a little while; a flechette that
		// never gets a velocity just falls, hits something and sticks.
		SetContextThink( &CHunterFlechette::FlightSetupThink, gpGlobals->curtime + 0.05f, s_szHunterFlechetteSetupThink );
		return;
	}

	BeginFlight( false );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::BeginFlight( bool bBright )
{
	if ( m_bFlying )
		return;

	m_bFlying = true;

	CreateSprites( bBright );

	m_vecShootPosition = GetAbsOrigin();

	SetThink( &CHunterFlechette::DopplerThink );
	SetNextThink( gpGlobals->curtime );

	SetContextThink( &CHunterFlechette::BubbleThink, gpGlobals->curtime + 0.1, s_szHunterFlechetteBubbles );
	SetContextThink( NULL, 0, s_szHunterFlechetteSetupThink );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::Shoot( const Vector &vecVelocity, bool bBrightFX )
{
	SetAbsVelocity( vecVelocity );

	// BeginFlight() also clears the deferred setup think armed by Spawn(), so a
	// flechette launched through this path does not light its trail twice.
	BeginFlight( bBrightFX );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::DangerSoundThink()
{
	EmitSound( "NPC_Hunter.FlechettePreExplode" );

	CSoundEnt::InsertSound( SOUND_DANGER|SOUND_CONTEXT_EXCLUDE_COMBINE, GetAbsOrigin(), 150.0f, 0.5, this );
	SetThink( &CHunterFlechette::ExplodeThink );
	SetNextThink( gpGlobals->curtime + HUNTER_FLECHETTE_WARN_TIME );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::ExplodeThink()
{
	Explode();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHunterFlechette::Explode()
{
	SetSolid( SOLID_NONE );

	// Don't catch self in own explosion!
	m_takedamage = DAMAGE_NO;

	EmitSound( "NPC_Hunter.FlechetteExplode" );

	// Move the explosion effect to the tip to reduce intersection with the world.
	Vector vecFuse = GetAbsOrigin();
	GetAttachment( s_nFlechetteFuseAttach, vecFuse );
	DispatchFlechetteParticleAtOrigin( "hunter_projectile_explosion_1", vecFuse, GetAbsAngles() );

	int nDamageType = DMG_DISSOLVE;

	// Perf optimization - only every other explosion makes a physics force. This is
	// hardly noticeable since flechettes usually explode in clumps.
	static int s_nExplosionCount = 0;
	s_nExplosionCount++;
	if ( ( s_nExplosionCount & 0x01 ) && hunter_cheap_explosions.GetBool() )
	{
		nDamageType |= DMG_PREVENT_PHYSICS_FORCE;
	}

	RadiusDamage( CTakeDamageInfo( this, GetOwnerEntity(), sk_hunter_flechette_explode_dmg.GetFloat(), nDamageType ), GetAbsOrigin(), sk_hunter_flechette_explode_radius.GetFloat(), CLASS_NONE, NULL );

	AddEffects( EF_NODRAW );

	SetThink( &CBaseEntity::SUB_Remove );
	SetNextThink( gpGlobals->curtime + 0.1f );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( hunter_flechette, CHunterFlechette );

BEGIN_DATADESC( CHunterFlechette )

	DEFINE_THINKFUNC( FlightSetupThink ),
	DEFINE_THINKFUNC( BubbleThink ),
	DEFINE_THINKFUNC( DangerSoundThink ),
	DEFINE_THINKFUNC( ExplodeThink ),
	DEFINE_THINKFUNC( DopplerThink ),
	DEFINE_THINKFUNC( SeekThink ),

	DEFINE_ENTITYFUNC( FlechetteTouch ),

	DEFINE_FIELD( m_vecShootPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_hSeekTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bThrownBack, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFlying, FIELD_BOOLEAN ),

END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CHunterFlechette, DT_HunterFlechette )
END_SEND_TABLE()
