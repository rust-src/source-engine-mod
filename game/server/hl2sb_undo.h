//========== HL2SB ===========//
//
// Purpose: Server-side undo stack for entities created by the spawnmenu
//          console commands (ent_create / prop_physics_create).  Each spawn
//          command records one undoable action, keyed by the player who ran
//          it, so the player can undo the most recent action with the Z key
//          (bound to hl2sb_undo) or clear the whole stack.
//
//          This is an engine-level equivalent of GMod's undo system: the
//          spawn commands call HL2SB_UndoRecord() to register a freshly
//          created entity, and HL2SB_UndoLast() removes the most recent
//          entry's entities and notifies the player.
//
//===========================================================================//

#ifndef HL2SB_UNDO_H
#define HL2SB_UNDO_H

class CBasePlayer;
class CBaseEntity;

//-----------------------------------------------------------------------------
// Record a freshly spawned entity into the given player's undo stack.
// Called by CC_Ent_Create / CC_Prop_Physics_Create after the entity is
// spawned.  The first entity recorded without an explicit Begin() starts a
// new undoable action (block); subsequent Add* calls in the same frame append
// to it.  pOwner may be NULL (console), in which case the record is dropped.
//-----------------------------------------------------------------------------
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt );

//-----------------------------------------------------------------------------
// Explicitly begin / end a multi-entity undoable action for a player.
//-----------------------------------------------------------------------------
void HL2SB_UndoBegin( CBasePlayer *pOwner );
void HL2SB_UndoEnd( CBasePlayer *pOwner );

//-----------------------------------------------------------------------------
// Undo the most recent action for a player.  Returns the number of entities
// removed (0 when there was nothing to undo or the stack was empty).
//-----------------------------------------------------------------------------
int HL2SB_UndoLast( CBasePlayer *pOwner );

//-----------------------------------------------------------------------------
// Clear a player's whole undo stack (entities are NOT removed).
//-----------------------------------------------------------------------------
void HL2SB_UndoClear( CBasePlayer *pOwner );

//-----------------------------------------------------------------------------
// Number of undoable actions currently queued for a player.
//-----------------------------------------------------------------------------
int HL2SB_UndoCount( CBasePlayer *pOwner );

//-----------------------------------------------------------------------------
// A debug dump of the current undo stacks (to the console).
//-----------------------------------------------------------------------------
void HL2SB_UndoDebugPrint();

#endif // HL2SB_UNDO_H
