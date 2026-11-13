/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cl_parse.c  -- parse a message received from the server

#include "client.h"
#include "cl_ui.h"
#include "cl_demohits.h"
#include "cl_demohud.h"
#include "../qcommon/bg_compat.h"

const char *svc_strings[256] = {
	"svc_bad",

	"svc_nop",
	"svc_gamestate",
	"svc_configstring",
	"svc_baseline",	
	"svc_serverCommand",
	"svc_download",
	"svc_snapshot",
	"svc_centerprint",
	"svc_locprint",
	"svc_cgameMessage"
};

msg_t *cl_currentMSG;

void SHOWNET( msg_t *msg, const char *s) {
	if ( cl_shownet->integer >= 2) {
		Com_Printf ("%3i:%s\n", msg->readcount-1, s);
	}
}


/*
=========================================================================

MESSAGE PARSING

=========================================================================
*/

/*
==================
CL_DeltaEntity

Parses deltas from the given base and adds the resulting entity
to the current frame
==================
*/
void CL_DeltaEntity (msg_t *msg, clSnapshot_t *frame, int newnum, entityState_t *old, 
					 qboolean unchanged) {
	entityState_t	*state;

	// save the parsed entity state into the big circular buffer so
	// it can be used as the source for a later delta
	state = &cl.parseEntities[cl.parseEntitiesNum & (MAX_PARSE_ENTITIES-1)];

	if ( unchanged ) {
		*state = *old;
	} else {
		MSG_ReadDeltaEntity( msg, old, state, newnum, cls.serverFrameTime);
	}

	if ( state->number == (MAX_GENTITIES-1) ) {
		return;		// entity was delta removed
	}
	cl.parseEntitiesNum++;
	frame->numEntities++;
}

/*
==================
CL_ParsePacketEntities

==================
*/
void CL_ParsePacketEntities( msg_t *msg, clSnapshot_t *oldframe, clSnapshot_t *newframe) {
	int			newnum;
	entityState_t	*oldstate;
	int			oldindex, oldnum;

	newframe->parseEntitiesNum = cl.parseEntitiesNum;
	newframe->numEntities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	oldstate = NULL;
	if (!oldframe) {
		oldnum = 99999;
	} else {
		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}

	while ( 1 ) {
		// read the entity index number
		newnum = MSG_ReadEntityNum(msg);

		if ( newnum == (MAX_GENTITIES-1) ) {
			break;
		}

		if ( msg->readcount > msg->cursize ) {
			Com_Error (ERR_DROP,"CL_ParsePacketEntities: end of message");
		}

		while ( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
			}
			CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );
			
			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
		}
		if (oldnum == newnum) {
			// delta from previous state
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  delta: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, oldstate, qfalse );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if ( oldnum > newnum ) {
			// delta from baseline
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  baseline: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, &cl.entityBaselines[newnum], qfalse );
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while ( oldnum != 99999 ) {
		// one or more entities from the old packet are unchanged
		if ( cl_shownet->integer == 3 ) {
			Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
		}
		CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );
		
		oldindex++;

		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}
}

/*
================
CL_UnpackNonPVSClient

Unpack info about non-visible client.
================
*/
qboolean CL_UnpackNonPVSClient(int* packed, radarUnpacked_t* unpacked) {
	int x, y;
	int yaw;
	qboolean bValid;
	float range;

	unpacked->clientNum = *packed & (MAX_CLIENTS - 1);
	if (unpacked->clientNum == cl.snap.ps.clientNum) {
		return qfalse;
	}

	x = ((*packed >> 6) & 0x7F) - (MAX_CLIENTS - 1);
	y = ((*packed >> 13) & 0x7F) - (MAX_CLIENTS - 1);
	yaw = (*packed >> 20) & 0x1F;
	bValid = (*packed >> 25) & 1;

	if (com_radar_range && com_radar_range->value) {
		range = com_radar_range->value;
	} else {
		range = 0;
	}

	unpacked->x = x * range / (MAX_CLIENTS - 1);
	unpacked->y = y * range / (MAX_CLIENTS - 1);

	if (!bValid)
	{
		unpacked->x = x * range / (MAX_CLIENTS - 1) * 1024.0;
		unpacked->y = y * range / (MAX_CLIENTS - 1) * 1024.0;
	}

	unpacked->yaw = yaw * 11.25f;

	return qtrue;
}

/*
================
CL_ReadNonPVSClient

Reads packed info about non-visible client.
================
*/
void CL_ReadNonPVSClient(int radarInfo) {
	radarUnpacked_t unpacked;

	if (!cge) {
		return;
	}

	if (CL_UnpackNonPVSClient(&radarInfo, &unpacked)) {
		unpacked.x += cl.snap.ps.origin[0];
		unpacked.y += cl.snap.ps.origin[1];
		cge->CG_ReadNonPVSClient(&unpacked);
	}
}

/*
================
CL_DemoFollowedHits

Added in OPM
With cg_followcamera 2, a demo shows the player the recorder follows from
their eyes, but it only has their health (STAT_INFOCLIENT_HEALTH), not the
view kick and red flash of their hits, nor where they came from: those are
made again from their health dropping, as the server makes them
(Player::DamageFeedback), from the shots and explosions that come with it,
after the snapshot in the same message (CL_DemoFollowedHitsEnd), and shown
with their health (CL_DemoShowFollowed). With cg_unspeccedcamera 2, a player
the demo doesn't show, seen from their eyes, gets theirs too, though their
health isn't in it: a hit is the pain the server plays on them for each one
they live through (Player::Pain), on CHAN_DIALOG_SECONDARY (96% of the hits
of players followed in 150 demos from stock servers, with a false one in all
of them), and how much it took from the blood their bullets drew (CGM6_BULLET_7
and 8 there: 98% of it with a hit): 23 for a small one, the middle of 20 to 25,
and 76 for a large one (a rifle's), of 64 to 85; without blood, 10, mostly a
fall's.
================
*/
typedef struct {
	int		messageNum;
	qboolean	followed;
	qboolean	unspecced;
	int		health;
	int		dir;
	float	alpha;
	vec3_t	angles;
	// their ammo, and an unspecced player's health, as worked out
	demoHudState_t	hud;
} demoFollowedHits_t;

#define MAX_DEMO_HIT_SOURCES 64
#define DEMO_HIT_SMALL 23
#define DEMO_HIT_LARGE 76
#define DEMO_HIT_BLOODLESS 10
// a hit whose damage the blood that comes with it tells (CL_DemoFollowedHitsEnd)
#define DEMO_HIT_PAIN -1
// where blood is on a player, around their origin
#define DEMO_HIT_BLOOD_SIDE 32
#define DEMO_HIT_BLOOD_BOTTOM -8
#define DEMO_HIT_BLOOD_TOP 104

typedef struct {
	vec3_t		pos;
	qboolean	large;
} demoHitFlesh_t;

static demoFollowedHits_t cl_demoFollowedHits[PACKET_BACKUP];
static demoHitSource_t cl_demoHitSources[MAX_DEMO_HIT_SOURCES];
static int cl_demoNumHitSources;
static demoHitFlesh_t cl_demoHitFlesh[MAX_DEMO_HIT_SOURCES];
static int cl_demoNumHitFlesh;
static struct {
	int		client; // -1 when not followed from their eyes
	qboolean	unspecced; // a player the demo doesn't show (cg_unspeccedcamera 2)
	int		health;
	int		serverTime;
	int		damage; // of this message, until what came with it is read
	int		ownDir; // the recorder's own STAT_DAMAGEDIR
	int		dir; // the STAT_DAMAGEDIR shown, that only changes with a hit
	demoHitFeedback_t feedback;
} cl_demoHits = { -1 };

void CL_DemoHitSource( const vec3_t start, const vec3_t end ) {
	demoHitSource_t	*source;

	if ( !clc.demoplaying || cl_demoNumHitSources >= MAX_DEMO_HIT_SOURCES ) {
		return;
	}
	source = &cl_demoHitSources[cl_demoNumHitSources++];
	VectorCopy( start, source->start );
	source->explosion = end == NULL;
	if ( end ) {
		VectorCopy( end, source->end );
	}
}

void CL_DemoHitFlesh( const vec3_t pos, qboolean large ) {
	if ( !clc.demoplaying || cl_demoNumHitFlesh >= MAX_DEMO_HIT_SOURCES ) {
		return;
	}
	VectorCopy( pos, cl_demoHitFlesh[cl_demoNumHitFlesh].pos );
	cl_demoHitFlesh[cl_demoNumHitFlesh].large = large;
	cl_demoNumHitFlesh++;
}

/*
================
CL_DemoSnapshotEntity

Added in OPM
The entity number of a snapshot read, NULL if it isn't in it
================
*/
static const entityState_t *CL_DemoSnapshotEntity( const clSnapshot_t *snap, int number ) {
	const entityState_t	*ent;
	int					i;

	if ( number < 0 || number >= MAX_CLIENTS ) {
		return NULL;
	}
	for ( i = 0; i < snap->numEntities; i++ ) {
		ent = &cl.parseEntities[( snap->parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 )];
		if ( ent->number == number ) {
			return ent;
		}
	}
	return NULL;
}

static void CL_DemoFollowedHitsKeep( demoFollowedHits_t *hits ) {
	hits->dir = cl_demoHits.dir;
	hits->alpha = cl_demoHits.feedback.alpha;
	DemoHits_ViewAngles( &cl_demoHits.feedback, hits->angles );
	// what the HUD has of them (cl_demohud.cpp): an unspecced player's health
	// too, a followed player's being in the demo
	CL_DemoHudState( cl_demoHits.client, &hits->hud );
	if ( hits->unspecced ) {
		hits->health = hits->hud.health;
	}
}

static void CL_DemoFollowedHits( const clSnapshot_t *snap ) {
	demoFollowedHits_t	*hits = &cl_demoFollowedHits[snap->messageNum & PACKET_MASK];
	int					client = snap->ps.stats[STAT_INFOCLIENT];
	int					health = snap->ps.stats[STAT_INFOCLIENT_HEALTH];
	int					damage, i;
	const entityState_t	*body = NULL;
	qboolean			unspecced = qfalse, pain = qfalse;

	hits->messageNum = -1;
	if ( !clc.demoplaying ) {
		return;
	}
	hits->messageNum = snap->messageNum;
	hits->followed = qfalse;
	hits->unspecced = qfalse;
	cl_demoNumHitSources = 0;
	cl_demoNumHitFlesh = 0;
	cl_demoHits.damage = 0;

	if ( snap->ps.stats[STAT_DAMAGEDIR] != cl_demoHits.ownDir ) {
		cl_demoHits.ownDir = snap->ps.stats[STAT_DAMAGEDIR];
		// no mark for hits from behind someone, as for a spectator
		if ( !CL_DemoShownFromBehind( &snap->ps ) ) {
			cl_demoHits.dir = cl_demoHits.ownDir;
		}
	}
	hits->dir = cl_demoHits.dir;

	// as cgame does, when their entity is in it: a player the demo doesn't
	// show, seen from their eyes, or else the one followed
	if ( Cvar_VariableIntegerValue( "cg_unspeccedcamera" ) == 2 ) {
		body = CL_DemoSnapshotEntity( snap, snap->demoBehind );
		if ( body ) {
			unspecced = qtrue;
			client = snap->demoBehind;
		}
	}
	if ( !body && Cvar_VariableIntegerValue( "cg_followcamera" ) == 2 && ( snap->ps.pm_flags & PMF_CAMERA_VIEW ) ) {
		body = CL_DemoSnapshotEntity( snap, client );
	}
	if ( !body ) {
		cl_demoHits.client = -1;
		return;
	}

	if ( unspecced ) {
		health = ( body->eFlags & EF_DEAD ) ? 0 : 1;
		for ( i = 0; i < snap->number_of_sounds; i++ ) {
			const server_sound_t *sound = &snap->sounds[i];

			if ( sound->entity_number == client && sound->channel == CHAN_DIALOG_SECONDARY && !sound->stop_flag ) {
				pain = qtrue;
			}
		}
	}

	if ( client != cl_demoHits.client || unspecced != cl_demoHits.unspecced || snap->serverTime < cl_demoHits.serverTime
		|| snap->serverTime - cl_demoHits.serverTime > 1000 ) {
		// someone else, or a jump: nothing going on
		DemoHits_Clear( &cl_demoHits.feedback );
	} else {
		// a server frame is 50 msec
		DemoHits_Fade( &cl_demoHits.feedback, ( snap->serverTime - cl_demoHits.serverTime ) / 50 );

		damage = cl_demoHits.health - health;
		if ( health <= 0 ) {
			DemoHits_Clear( &cl_demoHits.feedback );
		} else if ( unspecced ) {
			if ( pain ) {
				cl_demoHits.damage = DEMO_HIT_PAIN;
			}
		} else if ( damage > 0 ) {
			cl_demoHits.damage = damage;
		}
	}
	cl_demoHits.client = client;
	cl_demoHits.unspecced = unspecced;
	cl_demoHits.health = health;
	cl_demoHits.serverTime = snap->serverTime;

	hits->followed = qtrue;
	hits->unspecced = unspecced;
	hits->health = health;
	CL_DemoFollowedHitsKeep( hits );
}

/*
================
CL_DemoFollowedFromEyes

Added in OPM
Whether the last snapshot shows the player followed from their eyes
================
*/
qboolean CL_DemoFollowedFromEyes( void ) {
	return cl_demoHits.client >= 0;
}

/*
================
CL_DemoUnspeccedFromEyes

Added in OPM
Whether the last snapshot shows a player the demo doesn't show from their eyes
(cg_unspeccedcamera 2), with their hits
================
*/
qboolean CL_DemoUnspeccedFromEyes( void ) {
	return cl_demoHits.client >= 0 && cl_demoHits.unspecced;
}

/*
================
CL_DemoBloodDamage

Added in OPM
How much a hit took of a player whose origin is origin, as much as the blood
on them tells
================
*/
static int CL_DemoBloodDamage( const vec3_t origin ) {
	int damage = 0, i;

	for ( i = 0; i < cl_demoNumHitFlesh; i++ ) {
		const demoHitFlesh_t *flesh = &cl_demoHitFlesh[i];

		if ( fabs( flesh->pos[0] - origin[0] ) <= DEMO_HIT_BLOOD_SIDE && fabs( flesh->pos[1] - origin[1] ) <= DEMO_HIT_BLOOD_SIDE
			&& flesh->pos[2] - origin[2] >= DEMO_HIT_BLOOD_BOTTOM && flesh->pos[2] - origin[2] <= DEMO_HIT_BLOOD_TOP ) {
			damage += flesh->large ? DEMO_HIT_LARGE : DEMO_HIT_SMALL;
		}
	}
	return damage ? damage : DEMO_HIT_BLOODLESS;
}

/*
================
CL_DemoHudEnd

Added in OPM
The end of a message: the hits of all players in its snapshot, for what the
HUD has of them (cl_demohud.cpp), as for the one seen from their eyes
================
*/
static void CL_DemoHudEnd( void ) {
	int					clients[MAX_CLIENTS];
	vec3_t				origins[MAX_CLIENTS];
	demoFollowedHits_t	*hits;
	int					n, i;

	if ( !clc.demoplaying || !cl.snap.valid ) {
		return;
	}
	n = CL_DemoHudPained( clients, origins );
	for ( i = 0; i < n; i++ ) {
		CL_DemoHudHit( clients[i], CL_DemoBloodDamage( origins[i] ) );
	}

	hits = &cl_demoFollowedHits[cl.snap.messageNum & PACKET_MASK];
	if ( n && hits->messageNum == cl.snap.messageNum && hits->followed ) {
		CL_DemoFollowedHitsKeep( hits );
		CL_DemoShowFollowed( &cl.snap.ps, cl.snap.messageNum );
	}
}

/*
================
CL_DemoFollowedHitsEnd

Added in OPM
The end of a message: a hit of its snapshot, now that what came with it is read
================
*/
static void CL_DemoFollowedHitsEnd( void ) {
	demoHitPlayer_t		others[MAX_CLIENTS];
	demoFollowedHits_t	*hits;
	vec3_t				dir, origin;
	float				yaw;
	int					numOthers = 0, i;

	if ( !clc.demoplaying || !cl_demoHits.damage ) {
		return;
	}

	VectorCopy( cl.snap.ps.origin, origin );
	yaw = cl.snap.ps.viewangles[YAW];
	if ( cl_demoHits.unspecced ) {
		const entityState_t *body = CL_DemoSnapshotEntity( &cl.snap, cl_demoHits.client );

		if ( !body ) {
			cl_demoHits.damage = 0;
			return;
		}
		VectorCopy( body->netorigin, origin );
		yaw = body->netangles[YAW];

		cl_demoHits.damage = CL_DemoBloodDamage( origin );
	}

	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t *ent = &cl.parseEntities[( cl.snap.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 )];

		if ( ent->number < MAX_CLIENTS && ent->number != cl_demoHits.client && numOthers < MAX_CLIENTS ) {
			VectorCopy( ent->netorigin, others[numOthers].origin );
			others[numOthers].yaw = ent->netangles[YAW];
			numOthers++;
		}
	}

	DemoHits_Direction( origin, cl_demoHits.damage, cl_demoHitSources, cl_demoNumHitSources, others, numOthers, dir );
	DemoHits_Hit( &cl_demoHits.feedback, cl_demoHits.damage, dir, yaw );
	cl_demoHits.dir = DemoHits_DirStat( dir, cl_demoHits.dir );
	cl_demoHits.damage = 0;

	hits = &cl_demoFollowedHits[cl.snap.messageNum & PACKET_MASK];
	CL_DemoFollowedHitsKeep( hits );
	CL_DemoShowFollowed( &cl.snap.ps, cl.snap.messageNum );
}

/*
================
CL_DemoShowFollowed

Added in OPM
A demo's snapshot, as shown, with the health and hits of the player followed
from their eyes (CL_DemoFollowedHits); the snapshots kept stay as they came,
as the next ones build on them
================
*/
void CL_DemoShowFollowed( playerState_t *ps, int messageNum ) {
	const demoFollowedHits_t *hits = &cl_demoFollowedHits[messageNum & PACKET_MASK];

	if ( hits->messageNum != messageNum ) {
		return;
	}
	ps->stats[STAT_DAMAGEDIR] = hits->dir;
	if ( !hits->followed ) {
		return;
	}
	if ( hits->health >= 0 ) {
		ps->stats[STAT_HEALTH] = hits->health;
	}
	if ( hits->unspecced ) {
		ps->stats[STAT_MAXHEALTH] = 100;
	}
	// their gun and ammo, as worked out (cl_demohud.cpp)
	if ( hits->hud.weaponItem >= 0 ) {
		ps->activeItems[ITEM_WEAPON] = hits->hud.weaponItem;
		ps->stats[STAT_CLIPAMMO] = hits->hud.clip;
		ps->stats[STAT_MAXCLIPAMMO] = hits->hud.clipSize;
		ps->stats[STAT_AMMO] = hits->hud.ammo;
		ps->stats[STAT_MAXAMMO] = hits->hud.maxAmmo;
	}
	VectorCopy( hits->angles, ps->damage_angles );
	if ( hits->alpha > 0 ) {
		ps->blend[0] = 1;
		ps->blend[1] = ps->blend[2] = 0;
		ps->blend[3] = hits->alpha;
	} else if ( hits->unspecced ) {
		// not the recorder's
		ps->blend[3] = 0;
	}
}

/*
================
CL_ParseSnapshot

If the snapshot is parsed properly, it will be copied to
cl.snap and saved in cl.snapshots[].  If the snapshot is invalid
for any reason, no changes to the state will be made at all.
================
*/
void CL_ParseSnapshot( msg_t *msg ) {
	int			len;
	clSnapshot_t	*old;
	clSnapshot_t	newSnap;
	int			deltaNum;
	int			oldMessageNum;
	int			i, packetNum;

	// get the reliable sequence acknowledge number
	// NOTE: now sent with all server to client messages
	//clc.reliableAcknowledge = MSG_ReadLong( msg );

	// read in the new snapshot to a temporary buffer
	// we will only copy to cl.snap if it is valid
	Com_Memset (&newSnap, 0, sizeof(newSnap));

	// we will have read any new server commands in this
	// message before we got to svc_snapshot
	newSnap.serverCommandNum = clc.serverCommandSequence;

	newSnap.serverTime = MSG_ReadLong( msg );
	newSnap.serverTimeResidual = MSG_ReadByte( msg );

	newSnap.messageNum = clc.serverMessageSequence;

	deltaNum = MSG_ReadByte( msg );
	if ( !deltaNum ) {
		newSnap.deltaNum = -1;
	} else {
		newSnap.deltaNum = newSnap.messageNum - deltaNum;
	}
	newSnap.snapFlags = MSG_ReadByte( msg );

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message 
	if ( newSnap.deltaNum <= 0 ) {
		newSnap.valid = qtrue;		// uncompressed frame
		old = NULL;
		clc.demowaiting = qfalse;	// we can start recording now
	} else {
		old = &cl.snapshots[newSnap.deltaNum & PACKET_MASK];
		if ( !old->valid ) {
			// should never happen
			Com_Printf ("Delta from invalid frame (not supposed to happen!).\n");
		} else if ( old->messageNum != newSnap.deltaNum ) {
			// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Com_Printf ("Delta frame too old.\n");
		} else if ( cl.parseEntitiesNum - old->parseEntitiesNum > MAX_PARSE_ENTITIES-128 ) {
			Com_Printf ("Delta parseEntitiesNum too old.\n");
		} else {
			newSnap.valid = qtrue;	// valid delta parse
		}
	}

	// read areamask
	len = MSG_ReadByte( msg );
	
	if(len > sizeof(newSnap.areamask))
	{
		Com_Error (ERR_DROP,"CL_ParseSnapshot: Invalid size %d for areamask.", len);
		return;
	}
	
	MSG_ReadData( msg, &newSnap.areamask, len);

	// read playerinfo
    SHOWNET(msg, "playerstate");
	if ( old ) {
		MSG_ReadDeltaPlayerstate( msg, &old->ps, &newSnap.ps, cls.serverFrameTime );
	} else {
		MSG_ReadDeltaPlayerstate( msg, NULL, &newSnap.ps, cls.serverFrameTime);
	}
	// get normalized flags
	newSnap.ps.pm_flags = CPT_NormalizePlayerStateFlags(newSnap.ps.net_pm_flags);
	newSnap.ps.iViewModelAnim = CPT_NormalizeViewModelAnim(newSnap.ps.iNetViewModelAnim);

	// read packet entities
	SHOWNET( msg, "packet entities" );
	CL_ParsePacketEntities( msg, old, &newSnap );

	MSG_ReadSounds( msg, newSnap.sounds, &newSnap.number_of_sounds );

	// if not valid, dump the entire thing now that it has
	// been properly read
	if ( !newSnap.valid ) {
		return;
	}

	// clear the valid flags of any snapshots between the last
	// received and this one, so if there was a dropped packet
	// it won't look like something valid to delta from next
	// time we wrap around in the buffer
	oldMessageNum = cl.snap.messageNum + 1;

	if ( newSnap.messageNum - oldMessageNum >= PACKET_BACKUP ) {
		oldMessageNum = newSnap.messageNum - ( PACKET_BACKUP - 1 );
	}
	for ( ; oldMessageNum < newSnap.messageNum ; oldMessageNum++ ) {
		cl.snapshots[oldMessageNum & PACKET_MASK].valid = qfalse;
	}

	if( cl.snap.valid && ( newSnap.snapFlags ^ cl.snap.snapFlags ) & SNAPFLAG_SERVERCOUNT ) {
		cl.serverStartTime = newSnap.serverTime;
	}

	// Added in OPM
	//  When the view model's animation began: the first snapshot with it,
	//  so a demo seek, read without cgame, can land in the middle of it
	if ( cl.snap.valid && newSnap.ps.clientNum == cl.snap.ps.clientNum
		&& newSnap.ps.iViewModelAnim == cl.snap.ps.iViewModelAnim
		&& newSnap.ps.iViewModelAnimChanged == cl.snap.ps.iViewModelAnimChanged ) {
		newSnap.vmAnimTime = cl.snap.vmAnimTime;
	} else {
		newSnap.vmAnimTime = newSnap.serverTime;
	}

	// Added in OPM
	newSnap.demoBehind = CL_DemoBehindIn( newSnap.serverTime );
	if ( clc.demoplaying ) {
		CL_DemoHudSnapshot( &newSnap );
	}
	CL_DemoFollowedHits( &newSnap );

	// copy to the current good spot
	cl.snap = newSnap;
	cl.snap.ping = 999;
	// calculate ping time
	for ( i = 0 ; i < PACKET_BACKUP ; i++ ) {
		packetNum = ( clc.netchan.outgoingSequence - 1 - i ) & PACKET_MASK;
		if ( cl.snap.ps.commandTime >= cl.outPackets[ packetNum ].p_serverTime ) {
			cl.snap.ping = cls.realtime - cl.outPackets[ packetNum ].p_realtime;
			break;
		}
	}
	// save the frame off in the backup array for later delta comparisons
	cl.snapshots[cl.snap.messageNum & PACKET_MASK] = cl.snap;
	// Added in OPM
	//  as shown (the HUD), the one kept as it came
	CL_DemoShowFollowed( &cl.snap.ps, cl.snap.messageNum );
	CL_DemoSpectatorHud();

	if (cl_shownet->integer == 3) {
		Com_Printf( "   snapshot:%i  delta:%i  ping:%i\n", cl.snap.messageNum,
		cl.snap.deltaNum, cl.snap.ping );
	}

	cl.newSnapshots = qtrue;
	
	// read radar info about invisible clients
	CL_ReadNonPVSClient(cl.snap.ps.radarInfo);
}


//=====================================================================

int cl_connectedToPureServer;
int cl_connectedToCheatServer;

/*
==================
CL_SystemInfoChanged

The systeminfo configstring has been changed, so parse
new information out of it.  This will happen at every
gamestate, and possibly during gameplay.
==================
*/
void CL_SystemInfoChanged( void ) {
	char			*systemInfo;
	const char		*s, *t;
	char			key[BIG_INFO_KEY];
	char			value[BIG_INFO_VALUE];
	qboolean		gameSet;

	systemInfo = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SYSTEMINFO ];
	// NOTE TTimo:
	// when the serverId changes, any further messages we send to the server will use this new serverId
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=475
	// in some cases, outdated cp commands might get sent with this news serverId
	cl.serverId = atoi( Info_ValueForKey( systemInfo, "sv_serverid" ) );

#ifdef USE_VOIP
#ifdef LEGACY_PROTOCOL
	if(clc.compat)
		clc.voipEnabled = qfalse;
	else
#endif
	{
		s = Info_ValueForKey( systemInfo, "sv_voipProtocol" );
		clc.voipEnabled = !Q_stricmp(s, "opus");
	}
#endif

	// don't set any vars when playing a demo
	if ( clc.demoplaying ) {
		return;
	}

	s = Info_ValueForKey( systemInfo, "cheats" );
	cl_connectedToCheatServer = atoi( s );
	if ( !cl_connectedToCheatServer ) {
		Cvar_SetCheatState();
	}

	// check pure server string
	s = Info_ValueForKey( systemInfo, "sv_paks" );
	t = Info_ValueForKey( systemInfo, "sv_pakNames" );
	FS_PureServerSetLoadedPaks( s, t );

	s = Info_ValueForKey( systemInfo, "sv_referencedPaks" );
	t = Info_ValueForKey( systemInfo, "sv_referencedPakNames" );
	FS_PureServerSetReferencedPaks( s, t );

	gameSet = qfalse;
	// scan through all the variables in the systeminfo and locally set cvars to match
	s = systemInfo;
	while ( s ) {
		int cvar_flags;
		
		Info_NextPair( &s, key, value );
		if ( !key[0] ) {
			break;
		}
		
		// ehw!
		if (!Q_stricmp(key, "fs_game"))
		{
			if(FS_CheckDirTraversal(value))
			{
				Com_Printf(S_COLOR_YELLOW "WARNING: Server sent invalid fs_game value %s\n", value);
				continue;
			}
				
			gameSet = qtrue;
		}

		if((cvar_flags = Cvar_Flags(key)) == CVAR_NONEXISTENT)
			Cvar_Get(key, value, CVAR_SERVER_CREATED | CVAR_ROM);
		else
		{
			// If this cvar may not be modified by a server discard the value.
			if(!(cvar_flags & (CVAR_SYSTEMINFO | CVAR_SERVER_CREATED)))
			{
				Com_Printf(S_COLOR_YELLOW "WARNING: server is not allowed to set %s=%s\n", key, value);
				continue;
			}

			Cvar_Set(key, value);
		}
	}
	// if game folder should not be set and it is set at the client side
	if ( !gameSet && *Cvar_VariableString("fs_game") ) {
		Cvar_Set( "fs_game", "" );
	}
	// wombat: we ignore server's sv_pure for now
	//cl_connectedToPureServer = Cvar_VariableValue( "sv_pure" );
	cl_connectedToPureServer = 0;
}

/*
==================
CL_ParseServerInfo
==================
*/
static void CL_ParseServerInfo(void)
{
	const char *serverInfo;

	serverInfo = cl.gameState.stringData
		+ cl.gameState.stringOffsets[ CS_SERVERINFO ];

	clc.sv_allowDownload = atoi(Info_ValueForKey(serverInfo,
		"sv_allowDownload"));
	Q_strncpyz(clc.sv_dlURL,
		Info_ValueForKey(serverInfo, "sv_dlURL"),
		sizeof(clc.sv_dlURL));
}

/*
==================
CL_ClearStateKeepingFrames

Added in OPM
After a gamestate in the middle of a demo (a map change or restart), the
snapshots can be deltas from the last frames of the previous level, so the
frames are kept when the rest of the client state is wiped. The number of
the last one is kept too, or the next snapshot would drop them all as lost.
==================
*/
static void CL_ClearStateKeepingFrames( void ) {
	clSnapshot_t	*snapshots;
	entityState_t	*parseEntities;
	int				parseEntitiesNum;
	int				lastMessageNum;

	snapshots = (clSnapshot_t *)Z_Malloc( sizeof( cl.snapshots ) );
	parseEntities = (entityState_t *)Z_Malloc( sizeof( cl.parseEntities ) );
	Com_Memcpy( snapshots, cl.snapshots, sizeof( cl.snapshots ) );
	Com_Memcpy( parseEntities, cl.parseEntities, sizeof( cl.parseEntities ) );
	parseEntitiesNum = cl.parseEntitiesNum;
	lastMessageNum = cl.snap.messageNum;

	CL_ClearState();

	Com_Memcpy( cl.snapshots, snapshots, sizeof( cl.snapshots ) );
	Com_Memcpy( cl.parseEntities, parseEntities, sizeof( cl.parseEntities ) );
	cl.parseEntitiesNum = parseEntitiesNum;
	cl.snap.messageNum = lastMessageNum;

	Z_Free( snapshots );
	Z_Free( parseEntities );
}

/*
==================
CL_ParseGamestate
==================
*/
void CL_ParseGamestate( msg_t *msg ) {
	int				i, csNum;
	entityState_t	*es;
	int				newnum;
	entityState_t	nullstate;
	int				cmd;
	char			*s;

	UI_CloseConsole();

	clc.connectPacketCount = 0;
	if (cls.cgameStarted) {
		CL_FlushMemory();
	}

	// Added in OPM
	CL_DemoHudModelsChanged();

	// wipe local client state
	// Changed in OPM
	//  Demos keep the frames to delta from
	if ( clc.demoplaying ) {
		CL_ClearStateKeepingFrames();
	} else {
		CL_ClearState();
	}

	// a gamestate always marks a server command sequence
	clc.serverCommandSequence = MSG_ReadLong( msg );

	// parse all the configstrings and baselines
	cl.gameState.dataCount = 1;	// leave a 0 at the beginning for uninitialized configstrings
	while ( 1 ) {
		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			break;
		}
		
		if ( cmd == svc_configstring ) {
            int		len;

            i = MSG_ReadShort(msg);
            csNum = CPT_NormalizeConfigstring(i);
            if (csNum < 0 || csNum >= MAX_CONFIGSTRINGS) {
                Com_Error(ERR_DROP, "configstring > MAX_CONFIGSTRINGS");
				return;
            }
            s = MSG_ReadScrambledBigString(msg);
            len = strlen(s);

            if (len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS) {
                Com_Error(ERR_DROP, "MAX_GAMESTATE_CHARS exceeded");
            }

            // append it to the gameState string buffer
            cl.gameState.stringOffsets[csNum] = cl.gameState.dataCount;
            Com_Memcpy(cl.gameState.stringData + cl.gameState.dataCount, s, len + 1);
            cl.gameState.dataCount += len + 1;
		} else if ( cmd == svc_baseline ) {
			newnum = MSG_ReadEntityNum(msg);
			if ( newnum < 0 || newnum >= MAX_GENTITIES ) {
				Com_Error( ERR_DROP, "Baseline number out of range: %i", newnum );
			}
			//Com_Memset (&nullstate, 0, sizeof(nullstate));
			MSG_GetNullEntityState(&nullstate);
			es = &cl.entityBaselines[ newnum ];
			MSG_ReadDeltaEntity( msg, &nullstate, es, newnum, cls.serverFrameTime);
		} else {
			Com_Error( ERR_DROP, "CL_ParseGamestate: bad command byte %i", cmd );
		}
	}

	clc.clientNum = MSG_ReadLong(msg);
	// read the checksum feed
	clc.checksumFeed = MSG_ReadLong( msg );
	cls.serverFrameTime = MSG_ReadServerFrameTime(msg, &cl.gameState);

	// Added in OPM
	//  The words of cl_censor starred out of the players' names
	for ( i = CS_PLAYERS; i < CS_PLAYERS + MAX_CLIENTS; i++ ) {
		if ( cl.gameState.stringOffsets[ i ] ) {
			CL_CensorPlayerName( cl.gameState.stringData + cl.gameState.stringOffsets[ i ] );
		}
	}

	// parse serverId and other cvars
	CL_SystemInfoChanged();

	// stop recording now so the demo won't have an unnecessary level load at the end.
	if(cl_autoRecordDemo->integer && clc.demorecording)
		CL_StopRecord_f();

	if (clc.state == CA_CONNECTED && !Cvar_Get("sv_paks", "", 0)->string[0]) {
		// Added in 2.30
		FS_Restart(clc.checksumFeed);
	} else {
		// reinitialize the filesystem if the game directory has changed
		FS_ConditionalRestart(clc.checksumFeed, qfalse);
	}

	clc.state = CA_LOADING;
    if (!com_sv_running->integer)
    {
        const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
		const char *mapname = Info_ValueForKey(info, "mapname");
		// Added in 2.0
		Cvar_Set("mapname", mapname);

		UI_ClearState();
        UI_BeginLoad(mapname);
    }

	// This used to call CL_StartHunkUsers, but now we enter the download state before loading the
	// cgame
	CL_InitDownloads();

	// make sure the game starts
	Com_Unpause();
}


//=====================================================================

int CL_MSG_ReadBits( int bits )
{
	return MSG_ReadBits( cl_currentMSG, bits );
}

int CL_MSG_ReadChar( void )
{
	return MSG_ReadChar( cl_currentMSG );
}

int CL_MSG_ReadByte( void )
{
	return MSG_ReadByte( cl_currentMSG );
}

int CL_MSG_ReadSVC( void )
{
	return MSG_ReadSVC( cl_currentMSG );
}

int CL_MSG_ReadShort( void )
{
	return MSG_ReadShort( cl_currentMSG );
}

int CL_MSG_ReadLong( void )
{
	return MSG_ReadLong( cl_currentMSG );
}

float CL_MSG_ReadFloat( void )
{
	return MSG_ReadFloat( cl_currentMSG );
}

char *CL_MSG_ReadString( void )
{
	return MSG_ReadString( cl_currentMSG );
}

char *CL_MSG_ReadStringLine( void )
{
	return MSG_ReadStringLine( cl_currentMSG );
}

float CL_MSG_ReadAngle8( void )
{
	return MSG_ReadAngle8( cl_currentMSG );
}

float CL_MSG_ReadAngle16( void )
{
	return MSG_ReadAngle16( cl_currentMSG );
}

void CL_MSG_ReadData( void *data, int len )
{
	return MSG_ReadData( cl_currentMSG, data, len );
}

float CL_MSG_ReadCoord( void )
{
	return MSG_ReadCoord( cl_currentMSG );
}

void CL_MSG_ReadDir( vec3_t dir )
{
	MSG_ReadDir( cl_currentMSG, dir );
}

/*
=====================
CL_ParseDownload

A download message has been received from the server
=====================
*/
void CL_ParseDownload ( msg_t *msg ) {
	int		size;
	unsigned char data[MAX_MSGLEN];
	int block;

	if (!*clc.downloadTempName) {
		Com_Printf("Server sending download, but no download was requested\n");
		CL_AddReliableCommand( "stopdl", qfalse );
		return;
	}

	// read the data
	block = MSG_ReadShort ( msg );

	if ( !block )
	{
		// block zero is special, contains file size
		clc.downloadSize = MSG_ReadLong ( msg );

		Cvar_SetValue( "cl_downloadSize", clc.downloadSize );

		if (clc.downloadSize < 0)
		{
			Com_Error( ERR_DROP, "%s", MSG_ReadString( msg ) );
			return;
		}
	}

	size = MSG_ReadShort ( msg );
	if (size < 0 || size > sizeof(data))
	{
		Com_Error(ERR_DROP, "CL_ParseDownload: Invalid size %d for download chunk.", size);
		return;
	}
	
	MSG_ReadData(msg, data, size);

	if (clc.downloadBlock != block) {
		Com_DPrintf( "CL_ParseDownload: Expected block %d, got %d\n", clc.downloadBlock, block);
		return;
	}

	// open the file if not opened yet
	if (!clc.download)
	{
		clc.download = FS_BaseDir_FOpenFileWrite_HomeData( clc.downloadTempName );

		if (!clc.download) {
			Com_Printf( "Could not create %s\n", clc.downloadTempName );
			CL_AddReliableCommand( "stopdl", qfalse );
			CL_NextDownload();
			return;
		}
	}

	if (size)
		FS_Write( data, size, clc.download );

	CL_AddReliableCommand( va("nextdl %d", clc.downloadBlock), qfalse );
	clc.downloadBlock++;

	clc.downloadCount += size;

	// So UI gets access to it
	Cvar_SetValue( "cl_downloadCount", clc.downloadCount );

	if (!size) { // A zero length block means EOF
		if (clc.download) {
			FS_FCloseFile( clc.download );
			clc.download = 0;

			// rename the file
			FS_BaseDir_Rename_HomeData ( clc.downloadTempName, clc.downloadName, qfalse );
		}
		*clc.downloadTempName = *clc.downloadName = 0;
		Cvar_Set( "cl_downloadName", "" );

		// send intentions now
		// We need this because without it, we would hold the last nextdl and then start
		// loading right away.  If we take a while to load, the server is happily trying
		// to send us that last block over and over.
		// Write it twice to help make sure we acknowledge the download
		CL_WritePacket();
		CL_WritePacket();

		// get another file if needed
		CL_NextDownload ();
	}
}

#ifdef USE_VOIP
static
qboolean CL_ShouldIgnoreVoipSender(int sender)
{
	if (!cl_voip->integer)
		return qtrue;  // VoIP is disabled.
	else if ((sender == clc.clientNum) && (!clc.demoplaying))
		return qtrue;  // ignore own voice (unless playing back a demo).
	else if (clc.voipMuteAll)
		return qtrue;  // all channels are muted with extreme prejudice.
	else if (clc.voipIgnore[sender])
		return qtrue;  // just ignoring this guy.
	else if (clc.voipGain[sender] == 0.0f)
		return qtrue;  // too quiet to play.

	return qfalse;
}

/*
=====================
CL_PlayVoip

Play raw data
=====================
*/

static void CL_PlayVoip(int sender, int samplecnt, const byte *data, int flags)
{
	if(flags & VOIP_DIRECT)
	{
		S_RawSamples(sender + 1, samplecnt, 48000, 2, 1,
	             data, clc.voipGain[sender], -1);
	}

	if(flags & VOIP_SPATIAL)
	{
		S_RawSamples(sender + MAX_CLIENTS + 1, samplecnt, 48000, 2, 1,
	             data, 1.0f, sender);
	}
}

/*
=====================
CL_ParseVoip

A VoIP message has been received from the server
=====================
*/
static
void CL_ParseVoip ( msg_t *msg, qboolean ignoreData ) {
	static short decoded[VOIP_MAX_PACKET_SAMPLES*4]; // !!! FIXME: don't hard code

	const int sender = MSG_ReadShort(msg);
	const int generation = MSG_ReadByte(msg);
	const int sequence = MSG_ReadLong(msg);
	const int frames = MSG_ReadByte(msg);
	const int packetsize = MSG_ReadShort(msg);
	const int flags = MSG_ReadBits(msg, VOIP_FLAGCNT);
	unsigned char encoded[4000];
	int	numSamples;
	int seqdiff;
	int written = 0;
	int i;

	Com_DPrintf("VoIP: %d-byte packet from client %d\n", packetsize, sender);

	if (sender < 0)
		return;   // short/invalid packet, bail.
	else if (generation < 0)
		return;   // short/invalid packet, bail.
	else if (sequence < 0)
		return;   // short/invalid packet, bail.
	else if (frames < 0)
		return;   // short/invalid packet, bail.
	else if (packetsize < 0)
		return;   // short/invalid packet, bail.

	if (packetsize > sizeof (encoded)) {  // overlarge packet?
		int bytesleft = packetsize;
		while (bytesleft) {
			int br = bytesleft;
			if (br > sizeof (encoded))
				br = sizeof (encoded);
			MSG_ReadData(msg, encoded, br);
			bytesleft -= br;
		}
		return;   // overlarge packet, bail.
	}

	MSG_ReadData(msg, encoded, packetsize);

	if (ignoreData) {
		return; // just ignore legacy speex voip data
	} else if (!clc.voipCodecInitialized) {
		return;   // can't handle VoIP without libopus!
	} else if (sender >= MAX_CLIENTS) {
		return;   // bogus sender.
	} else if (CL_ShouldIgnoreVoipSender(sender)) {
		return;   // Channel is muted, bail.
	}

	// !!! FIXME: make sure data is narrowband? Does decoder handle this?

	Com_DPrintf("VoIP: packet accepted!\n");

	seqdiff = sequence - clc.voipIncomingSequence[sender];

	// This is a new "generation" ... a new recording started, reset the bits.
	if (generation != clc.voipIncomingGeneration[sender]) {
		Com_DPrintf("VoIP: new generation %d!\n", generation);
		opus_decoder_ctl(clc.opusDecoder[sender], OPUS_RESET_STATE);
		clc.voipIncomingGeneration[sender] = generation;
		seqdiff = 0;
	} else if (seqdiff < 0) {   // we're ahead of the sequence?!
		// This shouldn't happen unless the packet is corrupted or something.
		Com_DPrintf("VoIP: misordered sequence! %d < %d!\n",
		            sequence, clc.voipIncomingSequence[sender]);
		// reset the decoder just in case.
		opus_decoder_ctl(clc.opusDecoder[sender], OPUS_RESET_STATE);
		seqdiff = 0;
	} else if (seqdiff * VOIP_MAX_PACKET_SAMPLES*2 >= sizeof (decoded)) { // dropped more than we can handle?
		// just start over.
		Com_DPrintf("VoIP: Dropped way too many (%d) frames from client #%d\n",
		            seqdiff, sender);
		opus_decoder_ctl(clc.opusDecoder[sender], OPUS_RESET_STATE);
		seqdiff = 0;
	}

	if (seqdiff != 0) {
		Com_DPrintf("VoIP: Dropped %d frames from client #%d\n",
		            seqdiff, sender);
		// tell opus that we're missing frames...
		for (i = 0; i < seqdiff; i++) {
			assert((written + VOIP_MAX_PACKET_SAMPLES) * 2 < sizeof (decoded));
			numSamples = opus_decode(clc.opusDecoder[sender], NULL, 0, decoded + written, VOIP_MAX_PACKET_SAMPLES, 0);
			if ( numSamples <= 0 ) {
				Com_DPrintf("VoIP: Error decoding frame %d from client #%d\n", i, sender);
				continue;
			}
			written += numSamples;
		}
	}

	numSamples = opus_decode(clc.opusDecoder[sender], encoded, packetsize, decoded + written, ARRAY_LEN(decoded) - written, 0);

	if ( numSamples <= 0 ) {
		Com_DPrintf("VoIP: Error decoding voip data from client #%d\n", sender);
		numSamples = 0;
	}

	#if 0
	static FILE *encio = NULL;
	if (encio == NULL) encio = fopen("voip-incoming-encoded.bin", "wb");
	if (encio != NULL) { fwrite(encoded, packetsize, 1, encio); fflush(encio); }
	static FILE *decio = NULL;
	if (decio == NULL) decio = fopen("voip-incoming-decoded.bin", "wb");
	if (decio != NULL) { fwrite(decoded+written, numSamples*2, 1, decio); fflush(decio); }
	#endif

	written += numSamples;

	Com_DPrintf("VoIP: playback %d bytes, %d samples, %d frames\n",
	            written * 2, written, frames);

	if(written > 0)
		CL_PlayVoip(sender, written, (const byte *) decoded, flags);

	clc.voipIncomingSequence[sender] = sequence + frames;
}
#endif


/*
=====================
CL_ParseCommandString

Command strings are just saved off until cgame asks for them
when it transitions a snapshot
=====================
*/
void CL_ParseCommandString( msg_t *msg ) {
	char	*s;
	int		seq;
	int		index;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadScrambledString( msg );

	// see if we have already executed stored it off
	if ( clc.serverCommandSequence >= seq ) {
		return;
	}
	clc.serverCommandSequence = seq;

	index = seq & (MAX_RELIABLE_COMMANDS-1);
	Q_strncpyz( clc.serverCommands[ index ], s, sizeof( clc.serverCommands[ index ] ) );
}

/*
=====================
CL_ParseCGMessage

MOHAA does this inside its cgame. its some big function with a 37-switch case
but unless we properly read the CG message, we don't know when the message
hase finished
=====================
*/
void CL_ParseCGMessage( msg_t *msg ) {
	cl_currentMSG = msg;
	cge->CG_ParseCGMessage();
}

/*
=====================
CL_ParseLocationprint
=====================
*/
void CL_ParseLocationprint( msg_t *msg ) {
	int x, y;
	char *string;

	x = MSG_ReadShort( msg );
	y = MSG_ReadShort( msg );
	string = MSG_ReadScrambledString(msg);

	UI_UpdateLocationPrint( x, y, string, 1.0 );
}

/*
=====================
CL_DemoHidesCenterPrint

Added in OPM
Whether a demo doesn't show this message in the middle of the screen: all but
the round results ("Axis win!", in the languages seen), as the others are the
server's welcome and ads, "Press fire to join the battle!" and warnings to the
recorder
=====================
*/
static qboolean CL_DemoHidesCenterPrint( const char *text ) {
	static const char *const results[] = { "win!", "draw!", "gewinnen!", "Unentschieden!", "l'emporte", "Match nul" };
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( results ); i++ ) {
		if ( Q_stristr( text, results[i] ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
=====================
CL_ParseCenterprint
=====================
*/
void CL_ParseCenterprint( msg_t *msg ) {
	char *string;

	string = MSG_ReadScrambledString( msg );

	// Added in OPM
	//  None in a demo but the round results, and none a seek goes over
	if ( clc.demoplaying && ( clc.demoSeeking || CL_DemoHidesCenterPrint( string ) ) ) {
		return;
	}

	// FIXME
	UI_UpdateCenterPrint( string, 1.0 );
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( msg_t *msg ) {
	int			cmd;
//Com_Printf( "ParseServerMessage: %i\n", msg->cursize );
	if ( cl_shownet->integer == 1 ) {
		Com_Printf ("%zu ",msg->cursize);
	} else if ( cl_shownet->integer >= 2 ) {
		Com_Printf ("------------------\n");
	}

	MSG_Bitstream(msg);

	// get the reliable sequence acknowledge number
	clc.reliableAcknowledge = MSG_ReadLong( msg );
	// 
	if ( clc.reliableAcknowledge < clc.reliableSequence - MAX_RELIABLE_COMMANDS ) {
		clc.reliableAcknowledge = clc.reliableSequence;
	}

	//
	// parse the message
	//
	while ( 1 ) {
		if ( msg->readcount > msg->cursize ) {
			Com_Error (ERR_DROP,"CL_ParseServerMessage: read past end of server message");
			break;
		}

		cmd = MSG_ReadByte( msg );
	//	Com_Printf( "CL_ParseServerMessage: cmd %i\n", cmd );
		if ( cmd == svc_EOF) {
			SHOWNET( msg, "END OF MESSAGE" );
			break;
		}

		if ( cl_shownet->integer >= 2 ) {
			if ( !svc_strings[cmd] ) {
				Com_Printf( "%3i:BAD CMD %i\n", msg->readcount-1, cmd );
			} else {
				SHOWNET( msg, svc_strings[cmd] );
			}
		}
	
	// other commands
		switch ( cmd ) {
		default:
			Com_Error (ERR_DROP,"CL_ParseServerMessage: Illegible server message\n");
			break;			
		case svc_nop:
			break;
		case svc_serverCommand:
			CL_ParseCommandString( msg );
			break;
		case svc_gamestate:
			CL_ParseGamestate( msg );
			break;
		case svc_snapshot:
			CL_ParseSnapshot( msg );
			break;
		case svc_download:
			CL_ParseDownload( msg );
			break;
		case svc_centerprint:
			CL_ParseCenterprint( msg );
			break;
		case svc_locprint:
			CL_ParseLocationprint( msg );
			break;
		case svc_cgameMessage:
			if( !cge ) {
				Com_Error(ERR_DROP,"CL_ParseServerMessage: tried to parse cg message without cgame loaded\n");
			}
			CL_ParseCGMessage( msg );
			break;
		case svc_voipSpeex:
#ifdef USE_VOIP
			CL_ParseVoip( msg, qtrue );
#endif
			break;
		case svc_voipOpus:
#ifdef USE_VOIP
			CL_ParseVoip( msg, !clc.voipEnabled );
#endif
			break;
		}
	}

	// Added in OPM
	CL_DemoFollowedHitsEnd();
	CL_DemoHudEnd();
}
