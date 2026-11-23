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
// tr_draw.c -- drawing

#include "tr_local.h"

vec4_t r_colorWhite = { 1.0, 1.0, 1.0, 1.0 };

cvar_t *r_batch2D;
cvar_t *r_sortPipelines;

/*
====================================================================

2D BATCHES

The UI and cgame make each 2D pic, string and triangle a surface of its own:
a stage iteration and a draw each. A 2D draw whose shader computes the colors
and texture coordinates of each vertex from that vertex alone
(R_2DShaderBatchable) stays open in tess instead, and the 2D draws that come
next with the same shader, or another one drawing the same way from the same
atlas page (R_2DJoins), join it; it is drawn once something else is about to
run in the renderer (R_End2DBatch). What a batch keeps across draws:
- the window and the scissor: a 2D draw's vertexes are put on the screen
  (r2d.xform) and clipped to the window and the scissor (R_2DClip) here, the
  GPU's window being the screen's while in 2D; Set2DWindow and RE_Scissor
  only change where the next draws go;
- the 2D color: every 2D vertex carries it, and a global color's stage reads
  it from there (tess.colors2D);
- the shader's start time, unless the batch's shader animates once;
- the image: the UI's images (RegisterImage2D) are in atlas pages, a draw's
  texture coordinates moved into its image's place (R_2DPlace).
What ends a batch: a draw that can't join it, a full tess; whatever runs the
back end or records GPU work outside these draws (R_IssuePendingRenderCommands
or R_IssueRenderCommands: registration, the lines and additive boxes, the
front end's debug drawing, the 3D view, the end of the frame); a change of
what the batch reads without them: RE_SetRenderTime (the shader time),
RE_RefreshShaderNoMip (a shader's image), RE_UploadCinematic (an image's
contents), loading a font's shader, and RE_BeginFrame.
r_batch2D: 1 batches, 2 leaves out DrawBox's boxes that change no pixel
(alpha 0), 0 draws everything as each call comes.

====================================================================
*/

static struct {
	qboolean		batching;		// the surface in tess is a batch
	qboolean		open;			// ... that the next 2D draw may join
	int				firstVertex;	// the draw's first vertex and index in tess
	int				firstIndex;
	const image_t	*atlas;			// the draw's image when it is in an atlas page
	float			xform[4];		// the window: screen x = x * [0] + [1], y = y * [2] + [3] (pixels from the top left)
	float			clip[4];		// what the window and the scissor show: x0 y0 x1 y1, on the screen
	int				window[4];		// the window: x0 y0 x1 y1
	float			zNear, zFar;	// the window's depth range, the GPU's while in 2D
	qboolean		clipScissor;	// the GPU's scissor is the clip (lines, additive boxes), else the screen
} r2d;

/*
================
R_2DShaderBatchable

Whether consecutive 2D draws of this shader can be one surface: one stage
with one texture, no deform, the generic stage iterator, and texture
coordinates and colors that ComputeTexCoords and ComputeColors compute for
each vertex from that vertex alone (every 2D vertex has the 2D color) or
from what stays the same while a batch is open (the shader time): nothing
from the entity or the view, nothing left in tess.svars by an earlier
surface, every color channel written
================
*/
static qboolean R_2DShaderBatchable( const shader_t *shader ) {
	const shaderStage_t		*stage = shader->unfoggedStages[0];
	const textureBundle_t	*bundle;
	qboolean				rgbAlpha;	// the rgbGen writes alpha too
	int						i;

	if ( shader->numUnfoggedPasses != 1 || !stage || !stage->active || shader->numDeforms || shader->isSky
		|| shader->optimalStageIteratorFunc != RB_StageIteratorGeneric ) {
		return qfalse;
	}

	bundle = &stage->bundle[0];
	if ( stage->bundle[1].image[0] || bundle->isLightmap || bundle->vertexLightmap ) {
		return qfalse;
	}
	if ( bundle->tcGen != TCGEN_TEXTURE && bundle->tcGen != TCGEN_IDENTITY && bundle->tcGen != TCGEN_VECTOR ) {
		return qfalse;
	}
	for ( i = 0; i < bundle->numTexMods; i++ ) {
		switch ( bundle->texMods[i].type ) {
		case TMOD_TURBULENT:
		case TMOD_SCROLL:
		case TMOD_SCALE:
		case TMOD_STRETCH:
		case TMOD_TRANSFORM:
			break;
		default:
			return qfalse;
		}
	}

	switch ( stage->rgbGen ) {
	case CGEN_IDENTITY_LIGHTING:
	case CGEN_IDENTITY:
	case CGEN_CONSTANT:
	case CGEN_GLOBAL_COLOR:
	case CGEN_WAVEFORM:
	case CGEN_MULTIPLY_BY_WAVEFORM:
	case CGEN_EXACT_VERTEX:
	case CGEN_VERTEX:
		rgbAlpha = qtrue;
		break;
	case CGEN_ONE_MINUS_VERTEX:
		rgbAlpha = qfalse;
		break;
	default:
		return qfalse;
	}

	switch ( stage->alphaGen ) {
	case AGEN_IDENTITY:		// 255, or what the rgbGen wrote (identity, vertex)
	case AGEN_CONSTANT:
	case AGEN_GLOBAL_ALPHA:
	case AGEN_WAVEFORM:
	case AGEN_VERTEX:
	case AGEN_ONE_MINUS_VERTEX:
		return qtrue;
	case AGEN_SKIP:
		return rgbAlpha;
	default:
		return qfalse;
	}
}

/*
================
R_2DJoinClass

What a shader's 2D draws join (R_2DJoins): one plain stage from an atlas
page drawn a way, as a number (-1: none), the same for shaders drawn the
same way from the same page. Made once a shader and atlas generation: a
batch's draws (the 2D lists' kept ones each) are asked about, and the
stages and images it reads are rarely in the cache then
================
*/
static int r2dShadersGen;	// RegisterImage2D's calls and the atlas's resizes: what a shader's 2D info was made at

void R_2DShadersChanged( void ) {
	r2dShadersGen++;
}

static int R_2DJoinClass( shader_t *shader ) {
	static struct {
		const image_t	*atlas;
		unsigned		stateBits;
		int				rgbGen, alphaGen, tcGen, cullType, polygonOffset, once;
		void			( *iterator )( void );
	} classes[64];
	static int numClasses, classesGen = -1;
	const int gen = r2dShadersGen;
	const shaderStage_t *st = shader->unfoggedStages[0];
	image_t *image;
	int i;

	if ( shader->join2DGen == gen + 1 ) {
		return shader->join2D;
	}
	if ( classesGen != gen ) {
		classesGen = gen;
		numClasses = 0;
	}
	shader->join2DGen = gen + 1;
	shader->join2D = -1;
	image = st ? st->bundle[0].image[0] : NULL;
	shader->atlas2D = image && image->atlas ? image : NULL;
	if ( shader->numUnfoggedPasses != 1 || !st || !st->active || !image || !image->atlas || st->bundle[0].numTexMods
		|| st->bundle[0].numImageAnimations > 1 || st->bundle[1].image[0] || shader->numDeforms ) {
		return -1;
	}
	for ( i = 0; i < numClasses; i++ ) {
		if ( classes[i].atlas == image->atlas && classes[i].stateBits == st->stateBits && classes[i].rgbGen == st->rgbGen
			&& classes[i].alphaGen == st->alphaGen && classes[i].tcGen == st->bundle[0].tcGen
			&& classes[i].cullType == shader->cullType && classes[i].polygonOffset == shader->polygonOffset
			&& classes[i].once == ( shader->flags & BUNDLE_ANIMATE_ONCE ) && classes[i].iterator == shader->optimalStageIteratorFunc ) {
			break;
		}
	}
	if ( i == numClasses ) {
		if ( numClasses == ARRAY_LEN( classes ) ) {
			return -1;
		}
		classes[i].atlas = image->atlas;
		classes[i].stateBits = st->stateBits;
		classes[i].rgbGen = st->rgbGen;
		classes[i].alphaGen = st->alphaGen;
		classes[i].tcGen = st->bundle[0].tcGen;
		classes[i].cullType = shader->cullType;
		classes[i].polygonOffset = shader->polygonOffset;
		classes[i].once = shader->flags & BUNDLE_ANIMATE_ONCE;
		classes[i].iterator = shader->optimalStageIteratorFunc;
		numClasses++;
	}
	shader->join2D = i + 1;
	return shader->join2D;
}

/*
================
R_2DJoins

Whether a draw of shader b may join a batch of shader a: the same shader,
or both one plain stage from the same atlas page drawn the same way
================
*/
static qboolean R_2DJoins( shader_t *a, shader_t *b ) {
	int ca;

	if ( a == b ) {
		return qtrue;
	}
	ca = R_2DJoinClass( a );
	return ca > 0 && ca == R_2DJoinClass( b );
}

/*
================
R_2DScissor

The GPU's scissor: the screen for the batches (their draws are clipped
here), the clip for what is drawn otherwise
================
*/
void R_2DScissor( qboolean clip ) {
	int x0, y0, x1, y1;

	if ( !backEnd.in2D || ( !clip && !r2d.clipScissor ) ) {
		return;
	}
	r2d.clipScissor = clip;
	if ( !clip ) {
		RB_SetScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		return;
	}
	x0 = (int)r2d.clip[0];
	y0 = (int)r2d.clip[1];
	x1 = (int)r2d.clip[2];
	y1 = (int)r2d.clip[3];
	RB_SetScissor( x0, glConfig.vidHeight - y1, Q_max( 0, x1 - x0 ), Q_max( 0, y1 - y0 ) );
}

/*
================
R_2DTransform

A vertex of the window on the screen (immediate draws in 2D)
================
*/
void R_2DTransform( float *x, float *y ) {
	*x = *x * r2d.xform[0] + r2d.xform[1];
	*y = *y * r2d.xform[2] + r2d.xform[3];
}

/*
================
R_End2DBatch

Draws the open 2D batch
================
*/
void R_End2DBatch( void ) {
	if ( !r2d.open ) {
		return;
	}
	r2d.open = qfalse;
	r2d.batching = qfalse;
	RB_EndSurface();
}

/*
================
R_Forget2DBatch

Drops the open 2D batch without drawing it: at shutdown, after
R_IssuePendingRenderCommands drew it if the renderer was registered
================
*/
void R_Forget2DBatch( void ) {
	r2d.open = qfalse;
	r2d.batching = qfalse;
}

/*
================
R_IssuePending2DCommands

R_IssuePendingRenderCommands for a 2D draw that may join the open batch: the
queued commands, if any, run after the batch is drawn
================
*/
void R_IssuePending2DCommands( void ) {
	if ( R_OnRenderThread() ) {
		return;
	}
	if ( tr.registered && backEndData->commands.used ) {
		R_IssuePendingRenderCommands();
	}
}

/*
================
R_2DStartDraw
================
*/
static void R_2DStartDraw( shader_t *shader ) {
	R_2DJoinClass( shader );	// (and its atlas2D)
	r2d.firstVertex = tess.numVertexes;
	r2d.firstIndex = tess.numIndexes;
	r2d.atlas = shader->atlas2D;
}

/*
================
R_Begin2D

Starts the surface of a 2D draw, or keeps the open batch when the draw can
join it with room for its vertexes and indexes
================
*/
void R_Begin2D( shader_t *shader, int numVertexes, int numIndexes ) {
	if ( r2d.open ) {
		if ( R_2DJoins( tess.shader, shader ) && tess.numVertexes + numVertexes < SHADER_MAX_VERTEXES
			&& tess.numIndexes + numIndexes < SHADER_MAX_INDEXES ) {
			R_2DStartDraw( shader );
			return;
		}
		R_End2DBatch();
	}

	// "*frameblur": the frame as drawn so far blurred into it first (once a frame)
	if ( tr.frameBlurImage && shader->numUnfoggedPasses && shader->unfoggedStages[0]->bundle[0].image[0] == tr.frameBlurImage ) {
		vk_frame_blur( tr.frameBlurImage );
	}
	R_2DScissor( qfalse );
	RB_BeginSurface( shader );
	tess.colors2D = qtrue;
	tess.vertexColorValid = qtrue;
	r2d.batching = ( r_batch2D->integer & 1 ) && R_2DShaderBatchable( shader );
	R_2DStartDraw( shader );
}

/*
================
R_2DClip

Clips the draw's triangles to r2d.clip, as the scissor would, and, in an
atlas page, to its image (its texels beyond its edges taken as clear): a
triangle inside every plane kept as it is (its vertexes where they are),
one beyond a plane left out, the others cut, a polygon a triangle, their
vertexes' attributes interpolated as the rasterizer would across the screen
(an orthographic window)
================
*/
#define R2D_ATTRS 9	// x y z s t r g b a

static int R_2DClipEdge( const float (*in)[R2D_ATTRS], int n, float (*out)[R2D_ATTRS], int attr, float edge, float side ) {
	int i, k, m = 0;

	for ( i = 0; i < n; i++ ) {
		const float *a = in[i], *b = in[i + 1 < n ? i + 1 : 0];
		const float da = side * ( a[attr] - edge ), db = side * ( b[attr] - edge );

		if ( da >= 0 ) {
			Com_Memcpy( out[m++], a, sizeof( out[0] ) );
		}
		if ( ( da >= 0 ) != ( db >= 0 ) ) {
			const float t = da / ( da - db );

			for ( k = 0; k < R2D_ATTRS; k++ ) {
				out[m][k] = a[k] + ( b[k] - a[k] ) * t;
			}
			out[m++][attr] = edge;
		}
	}
	return m;
}

static void R_2DClip( qboolean image ) {
	static float		verts[SHADER_MAX_VERTEXES][R2D_ATTRS];
	static byte			beyond[SHADER_MAX_VERTEXES];	// the planes a vertex is beyond, a bit each
	static glIndex_t	indexes[SHADER_MAX_INDEXES];
	static float		poly[2][3 + 8][R2D_ATTRS];	// a triangle, a corner more at most for each plane
	const int			first = r2d.firstVertex;
	const int			numVertexes = tess.numVertexes - first;
	const int			numIndexes = tess.numIndexes - r2d.firstIndex;
	const int			numPlanes = image ? 8 : 4;
	const float			planes[8][3] = {
		{ 0, r2d.clip[0], 1 }, { 0, r2d.clip[2], -1 }, { 1, r2d.clip[1], 1 }, { 1, r2d.clip[3], -1 },
		{ 3, 0, 1 }, { 3, 1, -1 }, { 4, 0, 1 }, { 4, 1, -1 }
	};
	qboolean			kept = qtrue;	// the draw's own vertexes still in tess (nothing drawn since)
	int					i, j, k, n, p;

	for ( i = 0; i < numVertexes; i++ ) {
		const int v = first + i;

		VectorCopy( tess.xyz[v], verts[i] );
		verts[i][3] = tess.texCoords[v][0][0];
		verts[i][4] = tess.texCoords[v][0][1];
		for ( k = 0; k < 4; k++ ) {
			verts[i][5 + k] = tess.vertexColors[v][k];
		}
		beyond[i] = 0;
		for ( j = 0; j < numPlanes; j++ ) {
			if ( planes[j][2] * ( verts[i][(int)planes[j][0]] - planes[j][1] ) < 0 ) {
				beyond[i] |= 1 << j;
			}
		}
		// (kept for the triangles inside every plane, in its atlas page's place)
		if ( r2d.atlas ) {
			tess.texCoords[v][0][0] = r2d.atlas->atlasRect[0] + verts[i][3] * r2d.atlas->atlasRect[2];
			tess.texCoords[v][0][1] = r2d.atlas->atlasRect[1] + verts[i][4] * r2d.atlas->atlasRect[3];
		}
	}
	for ( i = 0; i < numIndexes; i++ ) {
		indexes[i] = tess.indexes[r2d.firstIndex + i] - first;
	}
	tess.numIndexes = r2d.firstIndex;

	for ( i = 0; i + 2 < numIndexes; i += 3 ) {
		const int a = indexes[i], b = indexes[i + 1], c = indexes[i + 2];

		if ( beyond[a] & beyond[b] & beyond[c] ) {
			continue;
		}
		if ( !( beyond[a] | beyond[b] | beyond[c] ) && kept && tess.numIndexes + 3 < SHADER_MAX_INDEXES ) {
			tess.indexes[tess.numIndexes++] = first + a;
			tess.indexes[tess.numIndexes++] = first + b;
			tess.indexes[tess.numIndexes++] = first + c;
			continue;
		}
		for ( j = 0; j < 3; j++ ) {
			Com_Memcpy( poly[0][j], verts[indexes[i + j]], sizeof( verts[0] ) );
		}
		n = 3;
		for ( j = 0, p = 0; j < numPlanes && n >= 3; j++ ) {
			const int attr = (int)planes[j][0];

			// (a plane all of it is inside leaves it as it is)
			for ( k = 0; k < n && planes[j][2] * ( poly[p][k][attr] - planes[j][1] ) >= 0; k++ ) {
			}
			if ( k < n ) {
				n = R_2DClipEdge( (const float (*)[R2D_ATTRS])poly[p], n, poly[!p], attr, planes[j][1], planes[j][2] );
				p = !p;
			}
		}
		if ( n < 3 ) {
			continue;
		}
		if ( tess.numVertexes + n >= SHADER_MAX_VERTEXES || tess.numIndexes + 3 * ( n - 2 ) >= SHADER_MAX_INDEXES ) {
			shader_t *shader = tess.shader;

			// what is clipped so far is drawn (the draw's own vertexes with it)
			RB_EndSurface();
			RB_BeginSurface( shader );
			tess.colors2D = qtrue;
			tess.vertexColorValid = qtrue;
			kept = qfalse;
		}
		for ( j = 0; j < n; j++ ) {
			const float *in = poly[p][j];
			const int v = tess.numVertexes + j;

			VectorCopy( in, tess.xyz[v] );
			tess.texCoords[v][0][0] = r2d.atlas ? r2d.atlas->atlasRect[0] + in[3] * r2d.atlas->atlasRect[2] : in[3];
			tess.texCoords[v][0][1] = r2d.atlas ? r2d.atlas->atlasRect[1] + in[4] * r2d.atlas->atlasRect[3] : in[4];
			for ( k = 0; k < 4; k++ ) {
				tess.vertexColors[v][k] = (byte)( Com_Clamp( 0, 255, in[5 + k] ) + 0.5f );
			}
			if ( j >= 2 ) {
				tess.indexes[tess.numIndexes++] = tess.numVertexes;
				tess.indexes[tess.numIndexes++] = v - 1;
				tess.indexes[tess.numIndexes++] = v;
			}
		}
		tess.numVertexes += n;
	}
}

/*
================
R_2DTiles

An atlas image drawn whole at its size on whole pixels (a texel a pixel), as
one quad, blended by its alpha (fog and the alpha test aside: neither makes
a clear texel's alpha more than 0): drawn as the rectangles covering its
texels not clear instead (R_AtlasFill), its clear texels changing no pixel
================
*/
static void R_2DTiles( void ) {
	const image_t	*image = r2d.atlas;
	const int		v = r2d.firstVertex, n = image->numAtlasRects;
	const float		*p0 = tess.xyz[v], *p3 = tess.xyz[v + 3];
	const float		x0 = p0[0], y0 = p0[1], z0 = p0[2];
	color4ub_t		color;
	int				i, k;

	if ( !image->atlasRects || tess.numVertexes - v != 4 || tess.numIndexes - r2d.firstIndex != 6
		|| ( tess.shader->unfoggedStages[0]->stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_DEPTHMASK_TRUE | GLS_POLYMODE_LINE ) )
			!= ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
		|| x0 != floorf( x0 ) || y0 != floorf( y0 ) || p3[0] - x0 != image->width || p3[1] - y0 != image->height
		|| tess.xyz[v + 1][0] != p3[0] || tess.xyz[v + 1][1] != y0 || tess.xyz[v + 2][0] != x0 || tess.xyz[v + 2][1] != p3[1]
		|| tess.texCoords[v][0][0] != 0 || tess.texCoords[v][0][1] != 0 || tess.texCoords[v + 3][0][0] != 1 || tess.texCoords[v + 3][0][1] != 1
		|| tess.texCoords[v + 1][0][0] != 1 || tess.texCoords[v + 2][0][1] != 1
		|| v + 4 * n >= SHADER_MAX_VERTEXES || r2d.firstIndex + 6 * n >= SHADER_MAX_INDEXES ) {
		return;
	}
	Com_Memcpy( color, tess.vertexColors[v], sizeof( color ) );
	tess.numVertexes = v;
	tess.numIndexes = r2d.firstIndex;
	for ( i = 0; i < n; i++ ) {
		// the rectangle's quad, its corners in the quad's order
		const short *r = image->atlasRects[i];

		for ( k = 0; k < 4; k++ ) {
			const int j = tess.numVertexes + k, x = r[( k & 1 ) ? 2 : 0], y = r[( k & 2 ) ? 3 : 1];

			tess.xyz[j][0] = x0 + x;
			tess.xyz[j][1] = y0 + y;
			tess.xyz[j][2] = z0;
			tess.texCoords[j][0][0] = x / (float)image->width;
			tess.texCoords[j][0][1] = y / (float)image->height;
			Com_Memcpy( tess.vertexColors[j], color, sizeof( color ) );
		}
		tess.indexes[tess.numIndexes++] = tess.numVertexes;
		tess.indexes[tess.numIndexes++] = tess.numVertexes + 1;
		tess.indexes[tess.numIndexes++] = tess.numVertexes + 2;
		tess.indexes[tess.numIndexes++] = tess.numVertexes + 2;
		tess.indexes[tess.numIndexes++] = tess.numVertexes + 1;
		tess.indexes[tess.numIndexes++] = tess.numVertexes + 3;
		tess.numVertexes += 4;
	}
}

/*
================
R_2DPlace

The draw's vertexes on the screen, clipped to the window and the scissor
when they go beyond them, its texture coordinates moved into its place in
its atlas page
================
*/
static void R_2DPlace( void ) {
	float	bounds[4] = { 1e30f, 1e30f, -1e30f, -1e30f };
	float	st[4] = { 0, 0, 1, 1 };
	int		i;

	for ( i = r2d.firstVertex; i < tess.numVertexes; i++ ) {
		float *v = tess.xyz[i];

		v[0] = v[0] * r2d.xform[0] + r2d.xform[1];
		v[1] = v[1] * r2d.xform[2] + r2d.xform[3];
		bounds[0] = Q_min( bounds[0], v[0] );
		bounds[1] = Q_min( bounds[1], v[1] );
		bounds[2] = Q_max( bounds[2], v[0] );
		bounds[3] = Q_max( bounds[3], v[1] );
		st[0] = Q_min( st[0], tess.texCoords[i][0][0] );
		st[1] = Q_min( st[1], tess.texCoords[i][0][1] );
		st[2] = Q_max( st[2], tess.texCoords[i][0][0] );
		st[3] = Q_max( st[3], tess.texCoords[i][0][1] );
	}
	if ( tess.numVertexes == r2d.firstVertex ) {
		return;
	}
	if ( r2d.atlas ) {
		R_2DTiles();
	}
	if ( bounds[0] >= r2d.clip[2] || bounds[2] <= r2d.clip[0] || bounds[1] >= r2d.clip[3] || bounds[3] <= r2d.clip[1] ) {
		tess.numVertexes = r2d.firstVertex;
		tess.numIndexes = r2d.firstIndex;
		return;
	}
	if ( bounds[0] < r2d.clip[0] || bounds[2] > r2d.clip[2] || bounds[1] < r2d.clip[1] || bounds[3] > r2d.clip[3]
		|| ( r2d.atlas && ( st[0] < 0 || st[1] < 0 || st[2] > 1 || st[3] > 1 ) ) ) {
		R_2DClip( r2d.atlas && ( st[0] < 0 || st[1] < 0 || st[2] > 1 || st[3] > 1 ) );
		return;
	}
	if ( r2d.atlas ) {
		for ( i = r2d.firstVertex; i < tess.numVertexes; i++ ) {
			float *tc = tess.texCoords[i][0];

			tc[0] = r2d.atlas->atlasRect[0] + tc[0] * r2d.atlas->atlasRect[2];
			tc[1] = r2d.atlas->atlasRect[1] + tc[1] * r2d.atlas->atlasRect[3];
		}
	}
}

/*
================
R_2DReserve

Room for a draw's next vertexes and indexes (the glyphs of a long string):
what the draw has so far is placed and drawn, the rest a surface of its own
================
*/
void R_2DReserve( int numVertexes, int numIndexes ) {
	shader_t *shader;

	if ( tess.numVertexes + numVertexes < SHADER_MAX_VERTEXES && tess.numIndexes + numIndexes < SHADER_MAX_INDEXES ) {
		return;
	}
	shader = tess.shader;
	R_2DPlace();
	RB_EndSurface();
	RB_BeginSurface( shader );
	tess.colors2D = qtrue;
	tess.vertexColorValid = qtrue;
	r2d.firstVertex = r2d.firstIndex = 0;
}

/*
================
R_End2D

Ends a 2D draw: placed on the screen, its surface is drawn, or stays open
when it is a batch
================
*/
void R_End2D( void ) {
	R_2DPlace();
	if ( r2d.batching ) {
		r2d.open = qtrue;
		return;
	}
	RB_EndSurface();
}

/*
================
R_End2DStream

Ends a 2D draw made with RB_Vertex2f and friends: the triangle strip
RB_StreamEnd makes of its vertexes, from its first one
================
*/
static void R_End2DStream( void ) {
	const int	firstVertex = r2d.firstVertex;
	int			numVertexes;
	int			i;

	numVertexes = tess.numVertexes - firstVertex;
	if ( numVertexes <= 2 ) {
		tess.numVertexes = firstVertex;
		R_End2D();
		return;
	}

	for ( i = 0; i < numVertexes - 2; i++ ) {
		tess.indexes[tess.numIndexes + 0] = firstVertex + ( i & 1 ) + i;
		tess.indexes[tess.numIndexes + 1] = firstVertex + i - ( ( i & 1 ) - 1 );
		tess.indexes[tess.numIndexes + 2] = firstVertex + i + 2;
		tess.numIndexes += 3;
	}

	R_End2D();
}

/*
================
Draw_SetColor

The color of the next 2D draws' vertexes (and of the immediate ones)
================
*/
void RB_Draw_SetColor(const vec4_t rgba) {
	if (!rgba) {
		rgba = r_colorWhite;
	}

	backEnd.color2D[0] = (byte)(rgba[0] * tr.identityLightByte);
	backEnd.color2D[1] = (byte)(rgba[1] * tr.identityLightByte);
	backEnd.color2D[2] = (byte)(rgba[2] * tr.identityLightByte);
	backEnd.color2D[3] = (byte)(rgba[3] * 255.0);
	RB_SetColor4ubv(backEnd.color2D);
}

/*
================
Draw_StretchPic
================
*/
void RB_Draw_StretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) {
#if 1
	shader_t* shader;

	R_IssuePending2DCommands();

	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	// draw the pic
	RB_Color4bv(backEnd.color2D);
	R_Begin2D(shader, 4, 6);

	RB_Texcoord2f(s1, t1);
	RB_Vertex2f(x, y);

	RB_Texcoord2f(s2, t1);
	RB_Vertex2f(x + w, y);

	RB_Texcoord2f(s1, t2);
	RB_Vertex2f(x, y + h);

	RB_Texcoord2f(s2, t2);
	RB_Vertex2f(x + w, y + h);

	R_End2DStream();
#else
	RE_StretchPic(x, y, w, h, s1, t1, s2, t2, hShader);
#endif
}

/*
================
Draw_StretchPic2
================
*/
void RB_Draw_StretchPic2(float x, float y, float w, float h, float s1, float t1, float s2, float t2, float sx, float sy, qhandle_t hShader) {
	shader_t* shader;
	float halfWidth, halfHeight;
	float scaledWidth1, scaledHeight1;
	float scaledWidth2, scaledHeight2;

	R_IssuePending2DCommands();

	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	halfWidth = w * 0.5f;
	halfHeight = h * 0.5f;
	scaledWidth1 = halfWidth * sy;
	scaledHeight1 = halfHeight * sx;
	scaledWidth2 = halfWidth * sx;
	scaledHeight2 = halfHeight * sy;

	// draw the pic
	RB_Color4bv(backEnd.color2D);
	R_Begin2D(shader, 4, 6);

	RB_Texcoord2f(s1, t1);
	RB_Vertex3f(x + halfWidth - (scaledWidth2 + -scaledHeight2), y + halfWidth - scaledHeight1 - scaledWidth1, 0);

	RB_Texcoord2f(t2, t1);
	RB_Vertex3f(scaledWidth2 - -scaledHeight2 + x + halfWidth, scaledWidth1 - scaledHeight1 + y + halfWidth, 0);

	RB_Texcoord2f(s1, s2);
	RB_Vertex3f(x+ halfWidth - (scaledWidth2 + scaledHeight2), scaledHeight1 - scaledWidth1 + y + halfWidth, 0);

	RB_Texcoord2f(t2, s2);
	RB_Vertex3f(scaledWidth2 - scaledHeight2 + x + halfWidth, scaledWidth1 + scaledHeight1 + y + halfWidth, 0);

	R_End2DStream();
}


/*
================
Draw_TilePic
================
*/
void RB_Draw_TilePic(float x, float y, float w, float h, qhandle_t hShader) {
	shader_t* shader;
	float		picw, pich;

	R_IssuePending2DCommands();

	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	picw = shader->unfoggedStages[0]->bundle[0].image[0]->uploadWidth;
	pich = shader->unfoggedStages[0]->bundle[0].image[0]->uploadHeight;

	// draw the pic
	RB_Color4bv(backEnd.color2D);

	R_Begin2D(shader, 4, 6);

	RB_Texcoord2f(x / picw, y / pich);
	RB_Vertex2f(x, y);

	RB_Texcoord2f((x + w) / picw, y / pich);
	RB_Vertex2f(x + w, y);

	RB_Texcoord2f(x / picw, (y + h) / pich);
	RB_Vertex2f(x, y + h);

	RB_Texcoord2f((x + w) / picw, (y + h) / pich);
	RB_Vertex2f(x + w, y + h);

	R_End2DStream();
}

/*
================
Draw_TilePicOffset
================
*/
void RB_Draw_TilePicOffset(float x, float y, float w, float h, qhandle_t hShader, int offsetX, int offsetY) {
	shader_t* shader;
	float		picw, pich;

	R_IssuePending2DCommands();

	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	picw = shader->unfoggedStages[0]->bundle[0].image[0]->uploadWidth;
	pich = shader->unfoggedStages[0]->bundle[0].image[0]->uploadHeight;

	// draw the pic
	RB_Color4bv(backEnd.color2D);

	R_Begin2D(shader, 4, 6);

	RB_Texcoord2f(x / picw, y / pich);
	RB_Vertex2f(x + offsetX, y + offsetY);

	RB_Texcoord2f((x + w) / picw, y / pich);
	RB_Vertex2f(x + offsetX + w, y + offsetY);

	RB_Texcoord2f(x / picw, (y + h) / pich);
	RB_Vertex2f(x + offsetX, y + offsetY + h);

	RB_Texcoord2f((x + w) / picw, (y + h) / pich);
	RB_Vertex2f(x + offsetX + w, y + offsetY + h);

	R_End2DStream();
}

/*
================
DrawPolygon2D

A convex polygon of count points (x, y, s, t each) as a fan, in one draw
(the Competitive HUD's radar), in the 2D color. Its edge smoothed: a band a
pixel wide across it, from the 2D color inside to clear outside (each corner
moved half a pixel in, and a copy half a pixel out, along its edges' mean
normal; s and t carried by the polygon's own mapping, affine as the
radar's). The 2D color is in each vertex (tess.colors2D): the outer copies
carry it with alpha 0
================
*/
void RB_DrawPolygon2D(const float (*points)[4], int count, qhandle_t hShader) {
	shader_t	*shader = hShader ? R_GetShaderByHandle(hShader) : tr.defaultShader;
	float		off[SHADER_MAX_VERTEXES / 4][2];	// each corner's way out in pixels, half a pixel from both its edges
	float		kx, ky;			// a window unit in pixels
	float		sx, sy, tx, ty;	// s and t a pixel right and down
	float		det, h, ax, ay;
	glIndex_t	*ix;
	int			i, v;

	if (count < 3 || 2 * count > SHADER_MAX_VERTEXES / 2 || 3 * (count - 2) + 6 * count > SHADER_MAX_INDEXES / 2) {
		return;
	}
	R_IssuePending2DCommands();	// (the window may change with them)
	kx = r2d.xform[0];
	ky = r2d.xform[2];
	if (!kx || !ky) {
		return;
	}

	// the mapping from three corners a third apart, in pixels; its sign the winding
	{
		const float *a = points[0], *b = points[count / 3], *c = points[2 * count / 3];
		const float x1 = (b[0] - a[0]) * kx, y1 = (b[1] - a[1]) * ky, x2 = (c[0] - a[0]) * kx, y2 = (c[1] - a[1]) * ky;

		det = x1 * y2 - x2 * y1;
		if (fabsf(det) < 1e-6f) {
			return;
		}
		sx = ((b[2] - a[2]) * y2 - (c[2] - a[2]) * y1) / det;
		sy = ((c[2] - a[2]) * x1 - (b[2] - a[2]) * x2) / det;
		tx = ((b[3] - a[3]) * y2 - (c[3] - a[3]) * y1) / det;
		ty = ((c[3] - a[3]) * x1 - (b[3] - a[3]) * x2) / det;
	}
	h = det > 0.0f ? 0.5f : -0.5f;

	// each edge's outward normal (det > 0: its right) half a pixel long, from the last edge's; a corner's way
	// out their mean, as far from each
	{
		const float *p = points[count - 1], *n = points[0];
		const float ex = (n[0] - p[0]) * kx, ey = (n[1] - p[1]) * ky, l = ex * ex + ey * ey;
		const float f = l > 0.0f ? h / sqrtf(l) : 0.0f;

		ax = ey * f;
		ay = -ex * f;
	}
	for (i = 0; i < count; i++) {
		const float *p = points[i], *n = points[i + 1 < count ? i + 1 : 0];
		const float ex = (n[0] - p[0]) * kx, ey = (n[1] - p[1]) * ky, l = ex * ex + ey * ey;
		const float f = l > 0.0f ? h / sqrtf(l) : 0.0f;
		const float bx = ey * f, by = -ex * f, mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;
		const float m = h * h / Q_max(mx * mx + my * my, h * h * 0.01f);

		off[i][0] = mx * m;
		off[i][1] = my * m;
		ax = bx;
		ay = by;
	}

	R_Begin2D(shader, 2 * count, 3 * (count - 2) + 6 * count);
	v = r2d.firstVertex;

	// the corners half a pixel in (v on), then out (v + count on: clear)
	for (i = 0; i < count; i++) {
		const float *p = points[i];
		const float ox = off[i][0], oy = off[i][1], os = sx * ox + sy * oy, ot = tx * ox + ty * oy;
		const int	in = v + i, out = v + count + i;

		VectorSet(tess.xyz[in], p[0] - ox / kx, p[1] - oy / ky, 0);
		VectorSet(tess.xyz[out], p[0] + ox / kx, p[1] + oy / ky, 0);
		tess.texCoords[in][0][0] = p[2] - os;
		tess.texCoords[in][0][1] = p[3] - ot;
		tess.texCoords[out][0][0] = p[2] + os;
		tess.texCoords[out][0][1] = p[3] + ot;
		Com_Memcpy(tess.vertexColors[in], backEnd.color2D, 4);
		Com_Memcpy(tess.vertexColors[out], backEnd.color2D, 3);
		tess.vertexColors[out][3] = 0;
	}
	tess.numVertexes = v + 2 * count;

	ix = tess.indexes + tess.numIndexes;
	for (i = 2; i < count; i++) {
		ix[0] = v;
		ix[1] = v + i - 1;
		ix[2] = v + i;
		ix += 3;
	}
	for (i = 0; i < count; i++) {
		const int j = i + 1 < count ? i + 1 : 0;

		ix[0] = v + i;
		ix[1] = v + j;
		ix[2] = v + count + j;
		ix[3] = v + i;
		ix[4] = v + count + j;
		ix[5] = v + count + i;
		ix += 6;
	}
	tess.numIndexes = (int)(ix - tess.indexes);
	R_End2D();
}

/*
================
Draw_TrianglePic
================
*/
void RB_Draw_TrianglePic(const vec2_t vPoints[3], const vec2_t vTexCoords[3], qhandle_t hShader) {
	int			i;
	shader_t* shader;

	R_IssuePending2DCommands();

	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	// draw the pic
	RB_Color4bv(backEnd.color2D);

	R_Begin2D(shader, 3, 3);

	for (i = 0; i < 3; i++) {
		RB_Texcoord2f(vTexCoords[i][0], vTexCoords[i][1]);
		RB_Vertex2f(vPoints[i][0], vPoints[i][1]);
	}

	R_End2DStream();
}

/*
================
RE_DrawBackground_TexSubImage
================
*/
void RE_DrawBackground_TexSubImage(int cols, int rows, int bgr, byte* data) {
	int		w, h;
	int		i;
	byte	*rgba;

	w = glConfig.vidWidth;
	h = glConfig.vidHeight;

	R_IssuePendingRenderCommands();
	R_2DScissor(qtrue);
	vk_finish();

	// expand the 24-bit RGB or BGR data to RGBA
	rgba = ri.Hunk_AllocateTempMemory(cols * rows * 4);
	for (i = 0; i < cols * rows; i++) {
		if (bgr) {
			rgba[i * 4 + 0] = data[i * 3 + 2];
			rgba[i * 4 + 2] = data[i * 3 + 0];
		} else {
			rgba[i * 4 + 0] = data[i * 3 + 0];
			rgba[i * 4 + 2] = data[i * 3 + 2];
		}
		rgba[i * 4 + 1] = data[i * 3 + 1];
		rgba[i * 4 + 3] = 255;
	}

	R_Bind(tr.scratchImage);
	R_UploadScratchImage(tr.scratchImage, cols, rows, rgba, qtrue);
	ri.Hunk_FreeTempMemory(rgba);

	// bypasses the tracked state, like the original
	vk_set_cull(CULL_NONE);
	vk_set_state(glState.glStateBits | GLS_DEPTHTEST_DISABLE);
	R_EnableTexture(qtrue);

	RB_ImmBegin(IMM_QUADS);

	RB_ImmTexCoord2f(0.5 / (float)cols, ((float)rows - 0.5) / rows);
	RB_ImmVertex2f(0, 0);

	RB_ImmTexCoord2f(((float)cols - 0.5) / cols, ((float)rows - 0.5) / rows);
	RB_ImmVertex2f(w, 0);

	RB_ImmTexCoord2f(((float)cols - 0.5) / cols, 0.5 / (float)rows);
	RB_ImmVertex2f(w, h);

	RB_ImmTexCoord2f(0.5 / (float)rows, 0.5 / (float)rows);
	RB_ImmVertex2f(0, h);

	RB_ImmEnd();
}

/*
================
RE_DrawBackground_DrawPixels

There is no pixel drawing path, the image is drawn as a textured quad
================
*/
void RE_DrawBackground_DrawPixels(int cols, int rows, int bgr, byte* data) {
	RE_DrawBackground_TexSubImage(cols, rows, bgr, data);
}

/*
================
AddBox
================
*/
void RB_AddBox(float x, float y, float w, float h) {
	R_IssuePendingRenderCommands();
	R_2DScissor(qtrue);

	RB_SetColor4ubv(backEnd.color2D);
	R_EnableTexture(qfalse);
	R_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

	RB_ImmBegin(IMM_QUADS);

	RB_ImmVertex2f(x, y);
	RB_ImmVertex2f(x + w, y);
	RB_ImmVertex2f(x + w, y + h);
	RB_ImmVertex2f(x, y + h);

	RB_ImmEnd();

	R_EnableTexture(qtrue);
}

/*
================
DrawBox

A quad of the atlas's white texel (R_White2D): it joins the 2D batch. With
alpha 0 the box blends to what is there (the UI's widgets draw their
backgrounds and borders this way, most of them transparent: 37 of 40 boxes
a frame in matches); r_measureOverdraw counts its fragments
================
*/
static void R_2DQuad(float x, float y, float w, float h) {
	R_Begin2D(tr.white2D, 4, 6);
	RB_Texcoord2f(0.5f, 0.5f);
	RB_Vertex2f(x, y);
	RB_Vertex2f(x + w, y);
	RB_Vertex2f(x, y + h);
	RB_Vertex2f(x + w, y + h);
	R_End2DStream();
}

void RB_DrawBox(float x, float y, float w, float h) {
	if ((r_batch2D->integer & 2) && !backEnd.color2D[3] && !r_measureOverdraw->integer) {
		return;
	}
	if (tr.white2D && !r_measureOverdraw->integer) {
		R_IssuePending2DCommands();
		RB_Color4bv(backEnd.color2D);
		R_2DQuad(x, y, w, h);
		return;
	}

	R_IssuePendingRenderCommands();
	R_2DScissor(qtrue);
	RB_SetColor4ubv(backEnd.color2D);
	R_EnableTexture(qfalse);
	R_State(GLS_DEPTHTEST_DISABLE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_SRCBLEND_SRC_ALPHA);

	RB_ImmBegin(IMM_QUADS);

	RB_ImmVertex2f(x, y);
	RB_ImmVertex2f(x + w, y);
	RB_ImmVertex2f(x + w, y + h);
	RB_ImmVertex2f(x, y + h);

	RB_ImmEnd();

	R_EnableTexture(qtrue);
}

/*
================
DrawBoxes

DrawBox for count boxes (x, y, width, height), in the color given (taken as
Draw_SetColor takes it); the 2D color stays (the crosshair)
================
*/
void RB_DrawBoxes(const float (*boxes)[4], int count, const float *color) {
	byte	rgba[4];
	int		i;

	rgba[0] = (byte)(color[0] * tr.identityLightByte);
	rgba[1] = (byte)(color[1] * tr.identityLightByte);
	rgba[2] = (byte)(color[2] * tr.identityLightByte);
	rgba[3] = (byte)(color[3] * 255.0);

	if (tr.white2D) {
		R_IssuePending2DCommands();
		RB_Color4bv(rgba);
		for (i = 0; i < count; i++) {
			R_2DQuad(boxes[i][0], boxes[i][1], boxes[i][2], boxes[i][3]);
		}
		return;
	}

	R_IssuePendingRenderCommands();
	R_2DScissor(qtrue);
	RB_SetColor4ubv(rgba);
	R_EnableTexture(qfalse);
	R_State(GLS_DEPTHTEST_DISABLE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_SRCBLEND_SRC_ALPHA);

	RB_ImmBegin(IMM_QUADS);

	for (i = 0; i < count; i++) {
		const float *b = boxes[i];

		RB_ImmVertex2f(b[0], b[1]);
		RB_ImmVertex2f(b[0] + b[2], b[1]);
		RB_ImmVertex2f(b[0] + b[2], b[1] + b[3]);
		RB_ImmVertex2f(b[0], b[1] + b[3]);
	}

	RB_ImmEnd();

	RB_SetColor4ubv(backEnd.color2D);
	R_EnableTexture(qtrue);
}

/*
================
DrawLineLoop
================
*/
void RB_DrawLineLoop(const vec2_t* points, int count, int stipple_factor, int stipple_mask) {
	int		i;

	R_IssuePendingRenderCommands();
	R_2DScissor(qtrue);

	R_EnableTexture(qfalse);

	if (stipple_factor) {
		vk_set_line_stipple(stipple_factor, stipple_mask);
	}

	RB_ImmBegin(IMM_LINE_LOOP);

	for (i = 0; i < count; i++) {
		RB_ImmVertex2f(points[i][0], points[i][1]);
	}

	RB_ImmEnd();

	R_EnableTexture(qtrue);

	if (stipple_factor) {
		vk_set_line_stipple(0, 0xFFFF);
	}
}

/*
================
Set2DWindow

Where the next 2D draws go on the screen and what of it they may cover; the
GPU's window stays the screen's while in 2D (R_2DPlace)
================
*/
static void R_2DClipTo(float x0, float y0, float x1, float y1) {
	r2d.clip[0] = Q_max(x0, Q_max(r2d.window[0], 0));
	r2d.clip[1] = Q_max(y0, Q_max(r2d.window[1], 0));
	r2d.clip[2] = Q_min(x1, Q_min(r2d.window[2], glConfig.vidWidth));
	r2d.clip[3] = Q_min(y1, Q_min(r2d.window[3], glConfig.vidHeight));
}

void RB_Set2DWindow(int x, int y, int w, int h, float left, float right, float bottom, float top, float n, float f) {
	float	matrix[16];

	R_IssuePending2DCommands();
	r2d.xform[0] = right != left ? w / (right - left) : 1.0f;
	r2d.xform[1] = x - left * r2d.xform[0];
	r2d.xform[2] = bottom != top ? h / (bottom - top) : 1.0f;
	r2d.xform[3] = glConfig.vidHeight - y - bottom * r2d.xform[2];
	r2d.window[0] = x;
	r2d.window[1] = glConfig.vidHeight - y - h;
	r2d.window[2] = x + w;
	r2d.window[3] = glConfig.vidHeight - y;
	R_2DClipTo(r2d.window[0], r2d.window[1], r2d.window[2], r2d.window[3]);
	if (backEnd.in2D && n == r2d.zNear && f == r2d.zFar) {
		return;
	}

	R_IssuePendingRenderCommands();
	// the temporal path's scene goes into the frame before the 2D drawing
	RB_TemporalEndScene();
	RB_SetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RB_SetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	r2d.clipScissor = qfalse;
	RB_OrthoMatrix(matrix, 0, glConfig.vidWidth, glConfig.vidHeight, 0, n, f);
	RB_SetProjection(matrix);
	r2d.zNear = n;
	r2d.zFar = f;

	RB_LoadIdentity(matrix);
	RB_SetModelview(matrix);
	R_State(GLS_DEPTHTEST_DISABLE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_SRCBLEND_SRC_ALPHA);
	vk_set_cull(CULL_NONE);
	vk_set_clip_plane(NULL);
	// Make sure to disable the fog to avoid messing up with the UI
	vk_set_fog(qfalse);
	// R_State can enable it again for 2D stages; with a start of 0 the
	// fog has no effect at the 2D depth
	RB_SetFogRange(0, glState.fogEnd);

	if (r_reset_tc_array->integer) {
		RB_EnableTexCoordArray(qfalse);
	}

	if (!backEnd.in2D)
	{
		backEnd.refdef.time = ri.Milliseconds();
		backEnd.in2D = qtrue;
		backEnd.refdef.floatTime = backEnd.refdef.time / 1000.0;
        backEnd.shaderStartTime = 0; 
	}
}

/*
================
RE_Scissor

What of the window the next 2D draws may cover (GL's pixels: from the bottom)
================
*/
void RB_Scissor2D(int x, int y, int width, int height) {
	R_2DClipTo(x, glConfig.vidHeight - y - height, x + width, glConfig.vidHeight - y);
}

/*
================
Set2DInitialShaderTime
================
*/
void RB_Set2DInitialShaderTime(float startTime)
{
	if (backEnd.in2D)
	{
		// what a shader animating once draws from it
		if (backEnd.shaderStartTime != startTime && r2d.open && (tess.shader->flags & BUNDLE_ANIMATE_ONCE)) {
			R_End2DBatch();
		}
		backEnd.shaderStartTime = startTime;
	}
}

/*
====================================================================

THE EXPORTS

Without the render thread each runs its body (RB_*) at once, as before.
With it (r_smp, tr_smp.c) each records a command the thread replays by
running the same body, in the same order with the same state, so the same
pixels; what the body reads of the back end's state (the 2D color, the
shader time, the batch) is then the thread's, as of that point in the
list. The bodies' R_IssuePending*Commands do nothing on the thread.

====================================================================
*/

#define R_2D_COMMAND( type, id ) \
	type *cmd = R_2DCommandBuffer( sizeof( *cmd ) ); \
	if ( !cmd ) { \
		return; \
	} \
	cmd->commandId = id;

/*
====================================================================

2D LISTS

What a part of the UI draws, kept to be drawn again: the 2D exports between
Begin2DList and End2DList go into the list as the render thread's commands
(RB_2DCommand) instead of running or being queued, and the list is drawn at
its end; Draw2DList draws it again as one command (a list drawn while
another is made is copied into it). A list made again or freed keeps its
old commands until no queued frame can still run them.

A handle is its list's slot and a serial no other list ever had, the
renderer's restarts too: a widget keeps its handle across a vid_restart,
which frees every list, and one freed is taken by the next new list. A
handle that no longer names its list (Begin2DList makes a new one, with a
new handle) can't name another's: before, a widget given a new list after
a vid_restart could take a slot another still held, both then drew from
it, and one drew what the other had made (the Esc menu over the game after
it closed, until the next vid_restart)

====================================================================
*/

#define MAX_2D_LISTS	4096	// (a handle's low 12 bits its slot)
#define R2D_SLOTBITS	12

typedef struct r2dList_s {
	byte				*data;
	int					used, size;
	int					retired;	// the frame it was replaced at
	struct r2dList_s	*next;		// the next retired one
} r2dList_t;

static struct {
	r2dList_t	*lists[MAX_2D_LISTS];
	int			serials[MAX_2D_LISTS];	// each list's serial (its handle's high bits)
	r2dList_t	*recording;		// the list being made
	int			handle, slot;	// ... its handle and slot
	r2dList_t	*retired;
	int			frame;
} r2dl;

static int r2dlSerial;	// the last list's serial (kept by the shutdown: none is used twice)

// the slot of the list a handle names, -1 none (0, a list freed, one from before a restart)
static int R_2DListSlot( int handle ) {
	const int slot = handle & ( MAX_2D_LISTS - 1 );

	if ( handle <= 0 || !r2dl.lists[slot] || r2dl.serials[slot] != handle >> R2D_SLOTBITS ) {
		return -1;
	}
	return slot;
}

qboolean R_2DQueued( void ) {
	return !R_OnRenderThread() && ( glConfig.smpActive || r2dl.recording );
}

// room for a 2D command in the list being made, else in the frame's
void *R_2DCommandBuffer( int bytes ) {
	r2dList_t *l = r2dl.recording;

	if ( !l ) {
		return R_GetCommandBuffer( bytes );
	}
	bytes = PAD( bytes, sizeof( void * ) );
	if ( l->used + bytes > l->size ) {
		const int	size = Q_max( l->size * 2, l->used + bytes + 1024 );
		byte		*data = ri.Malloc( size );

		Com_Memcpy( data, l->data, l->used );
		ri.Free( l->data );
		l->data = data;
		l->size = size;
	}
	l->used += bytes;
	return l->data + l->used - bytes;
}

static void R_Retire2DList( r2dList_t *l ) {
	l->retired = r2dl.frame;
	l->next = r2dl.retired;
	r2dl.retired = l;
}

static void R_Free2DListData( r2dList_t *l ) {
	ri.Free( l->data );
	ri.Free( l );
}

static void RB_Run2DList( const r2dList_t *l ) {
	const byte *data, *end = l->data + l->used;

	for ( data = PADP( l->data, sizeof( void * ) ); data < end; data = PADP( data, sizeof( void * ) ) ) {
		data = RB_2DCommand( data );
	}
}

// a list's handle (a new one for 0, or for a handle that names no list): the 2D exports go into it until End2DList
int RE_Begin2DList( int handle ) {
	int slot;

	if ( R_OnRenderThread() || r2dl.recording ) {
		return 0;
	}
	slot = R_2DListSlot( handle );
	if ( slot < 0 ) {
		for ( slot = 0; slot < MAX_2D_LISTS && r2dl.lists[slot]; slot++ ) {
		}
		if ( slot == MAX_2D_LISTS ) {
			return 0;
		}
		// (never 0: a handle is never 0)
		r2dlSerial = r2dlSerial >= ( INT_MAX >> R2D_SLOTBITS ) ? 1 : r2dlSerial + 1;
		r2dl.serials[slot] = r2dlSerial;
		handle = ( r2dlSerial << R2D_SLOTBITS ) | slot;
	}
	r2dl.recording = ri.Malloc( sizeof( r2dList_t ) );
	Com_Memset( r2dl.recording, 0, sizeof( r2dList_t ) );
	r2dl.recording->size = 1024;
	r2dl.recording->data = ri.Malloc( r2dl.recording->size );
	r2dl.handle = handle;
	r2dl.slot = slot;
	// held while made, so no other Begin2DList takes it
	if ( !r2dl.lists[slot] ) {
		r2dl.lists[slot] = r2dl.recording;
	}
	return handle;
}

void RE_Draw2DList( int handle ) {
	const int		slot = R_2DListSlot( handle );
	const r2dList_t	*l = slot >= 0 ? r2dl.lists[slot] : NULL;

	if ( !l || l == r2dl.recording ) {
		return;
	}
	if ( r2dl.recording ) {
		Com_Memcpy( R_2DCommandBuffer( l->used ), l->data, l->used );
		return;
	}
	if ( R_2DQueued() ) {
		R_2D_COMMAND( list2DCommand_t, RC_2D_LIST );
		cmd->list = l;
		return;
	}
	RB_Run2DList( l );
}

// the list made: kept, and drawn
void RE_End2DList( void ) {
	r2dList_t	*l = r2dl.recording;
	r2dList_t	*old;

	if ( !l ) {
		return;
	}
	r2dl.recording = NULL;
	old = r2dl.lists[r2dl.slot];
	r2dl.lists[r2dl.slot] = l;
	if ( old && old != l ) {
		R_Retire2DList( old );
	}
	RE_Draw2DList( r2dl.handle );
}

void RE_Free2DList( int handle ) {
	const int slot = R_2DListSlot( handle );

	if ( slot < 0 || r2dl.lists[slot] == r2dl.recording ) {
		return;
	}
	R_Retire2DList( r2dl.lists[slot] );
	r2dl.lists[slot] = NULL;
}

// the front end's frame: lists retired three frames ago are freed
void R_2DListsEndFrame( void ) {
	r2dList_t **p = &r2dl.retired;

	r2dl.frame++;
	while ( *p ) {
		r2dList_t *l = *p;

		if ( r2dl.frame - l->retired >= 3 ) {
			*p = l->next;
			R_Free2DListData( l );
		} else {
			p = &l->next;
		}
	}
}

// the renderer's shutdown (no queued frame left): every list
void R_2DListsShutdown( void ) {
	int i;

	if ( r2dl.recording && r2dl.lists[r2dl.slot] != r2dl.recording ) {
		R_Free2DListData( r2dl.recording );
	}
	for ( i = 0; i < MAX_2D_LISTS; i++ ) {
		if ( r2dl.lists[i] ) {
			R_Free2DListData( r2dl.lists[i] );
		}
	}
	while ( r2dl.retired ) {
		r2dList_t *l = r2dl.retired;

		r2dl.retired = l->next;
		R_Free2DListData( l );
	}
	Com_Memset( &r2dl, 0, sizeof( r2dl ) );
}

void Draw_SetColor(const vec4_t rgba) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( set2DColorCommand_t, RC_2D_SET_COLOR );
		cmd->isNull = rgba == NULL;
		if ( rgba ) {
			Vector4Copy( rgba, cmd->rgba );
		}
		return;
	}
	RB_Draw_SetColor( rgba );
}

void Draw_StretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( pic2DCommand_t, RC_2D_STRETCH_PIC );
		cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
		cmd->s1 = s1; cmd->t1 = t1; cmd->s2 = s2; cmd->t2 = t2;
		cmd->hShader = hShader;
		return;
	}
	RB_Draw_StretchPic( x, y, w, h, s1, t1, s2, t2, hShader );
}

void Draw_StretchPic2(float x, float y, float w, float h, float s1, float t1, float s2, float t2, float sx, float sy, qhandle_t hShader) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( pic2DCommand_t, RC_2D_STRETCH_PIC2 );
		cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
		cmd->s1 = s1; cmd->t1 = t1; cmd->s2 = s2; cmd->t2 = t2;
		cmd->sx = sx; cmd->sy = sy;
		cmd->hShader = hShader;
		return;
	}
	RB_Draw_StretchPic2( x, y, w, h, s1, t1, s2, t2, sx, sy, hShader );
}

void Draw_TilePic(float x, float y, float w, float h, qhandle_t hShader) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( pic2DCommand_t, RC_2D_TILE_PIC );
		cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
		cmd->hShader = hShader;
		return;
	}
	RB_Draw_TilePic( x, y, w, h, hShader );
}

void Draw_TilePicOffset(float x, float y, float w, float h, qhandle_t hShader, int offsetX, int offsetY) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( pic2DCommand_t, RC_2D_TILE_PIC_OFFSET );
		cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
		cmd->hShader = hShader;
		cmd->offsetX = offsetX; cmd->offsetY = offsetY;
		return;
	}
	RB_Draw_TilePicOffset( x, y, w, h, hShader, offsetX, offsetY );
}

void Draw_TrianglePic(const vec2_t vPoints[3], const vec2_t vTexCoords[3], qhandle_t hShader) {
	if ( R_2DQueued() ) {
		int i;
		R_2D_COMMAND( trianglePic2DCommand_t, RC_2D_TRIANGLE_PIC );
		for ( i = 0; i < 3; i++ ) {
			cmd->points[i][0] = vPoints[i][0]; cmd->points[i][1] = vPoints[i][1];
			cmd->texCoords[i][0] = vTexCoords[i][0]; cmd->texCoords[i][1] = vTexCoords[i][1];
		}
		cmd->hShader = hShader;
		return;
	}
	RB_Draw_TrianglePic( vPoints, vTexCoords, hShader );
}

void AddBox(float x, float y, float w, float h) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( box2DCommand_t, RC_2D_ADD_BOX );
		cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
		return;
	}
	RB_AddBox( x, y, w, h );
}

void DrawBox(float x, float y, float w, float h) {
	R_White2D();
	if ( R_2DQueued() ) {
		R_2D_COMMAND( box2DCommand_t, RC_2D_DRAW_BOX );
		cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
		return;
	}
	RB_DrawBox( x, y, w, h );
}

void DrawBoxes(const float (*boxes)[4], int count, const float *color) {
	R_White2D();
	if ( R_2DQueued() ) {
		boxes2DCommand_t *cmd;

		if ( count <= 0 ) {
			return;
		}
		cmd = R_2DCommandBuffer( sizeof( *cmd ) + PAD( sizeof( float ) * 4 * count, sizeof( void * ) ) );
		if ( !cmd ) {
			return;
		}
		cmd->commandId = RC_2D_DRAW_BOXES;
		cmd->count = count;
		Vector4Copy( color, cmd->color );
		Com_Memcpy( cmd + 1, boxes, sizeof( float ) * 4 * count );
		return;
	}
	RB_DrawBoxes( boxes, count, color );
}

void DrawPolygon2D(const float (*points)[4], int count, qhandle_t hShader) {
	if ( count < 3 ) {
		return;
	}
	if ( R_2DQueued() ) {
		polygon2DCommand_t *cmd = R_2DCommandBuffer( sizeof( *cmd ) + PAD( sizeof( float ) * 4 * count, sizeof( void * ) ) );

		if ( !cmd ) {
			return;
		}
		cmd->commandId = RC_2D_POLYGON;
		cmd->count = count;
		cmd->hShader = hShader;
		Com_Memcpy( cmd + 1, points, sizeof( float ) * 4 * count );
		return;
	}
	RB_DrawPolygon2D( points, count, hShader );
}

/*
================
RE_Radar2DHeight

The radar's picture (RE_RegisterRadar2D) for height z, for the draws after
this: its texels made on the GPU in order with the draws (vk_radar_height),
the render thread's in its list, never in a 2D list being made (made once,
not each time the list is drawn)
================
*/
static void RB_Radar2DHeight( qhandle_t hShader, float z ) {
	const shader_t *shader = R_GetShaderByHandle( hShader );

	if ( !shader->numUnfoggedPasses || !shader->unfoggedStages[0] ) {
		return;
	}
	R_IssuePending2DCommands();
	R_End2DBatch();
	vk_radar_height( shader->unfoggedStages[0]->bundle[0].image[0], z );
}

void RE_Radar2DHeight( qhandle_t hShader, float z ) {
	if ( !R_OnRenderThread() && glConfig.smpActive ) {
		radarHeight2DCommand_t *cmd = R_GetCommandBuffer( sizeof( *cmd ) );

		if ( !cmd ) {
			return;
		}
		cmd->commandId = RC_2D_RADAR_HEIGHT;
		cmd->hShader = hShader;
		cmd->z = z;
		return;
	}
	RB_Radar2DHeight( hShader, z );
}

void DrawLineLoop(const vec2_t* points, int count, int stipple_factor, int stipple_mask) {
	if ( R_2DQueued() ) {
		lineLoop2DCommand_t *cmd;

		if ( count <= 0 ) {
			return;
		}
		cmd = R_2DCommandBuffer( sizeof( *cmd ) + PAD( sizeof( vec2_t ) * count, sizeof( void * ) ) );
		if ( !cmd ) {
			return;
		}
		cmd->commandId = RC_2D_LINE_LOOP;
		cmd->count = count;
		cmd->stippleFactor = stipple_factor;
		cmd->stippleMask = stipple_mask;
		Com_Memcpy( cmd + 1, points, sizeof( vec2_t ) * count );
		return;
	}
	RB_DrawLineLoop( points, count, stipple_factor, stipple_mask );
}

void Set2DWindow(int x, int y, int w, int h, float left, float right, float bottom, float top, float n, float f) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( window2DCommand_t, RC_2D_WINDOW );
		cmd->x = x; cmd->y = y; cmd->w = w; cmd->h = h;
		cmd->left = left; cmd->right = right; cmd->bottom = bottom; cmd->top = top;
		cmd->n = n; cmd->f = f;
		return;
	}
	RB_Set2DWindow( x, y, w, h, left, right, bottom, top, n, f );
}

void RE_Scissor(int x, int y, int width, int height) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( scissor2DCommand_t, RC_2D_SCISSOR );
		cmd->x = x; cmd->y = y; cmd->width = width; cmd->height = height;
		return;
	}
	RB_Scissor2D( x, y, width, height );
}

void Set2DInitialShaderTime(float startTime) {
	if ( R_2DQueued() ) {
		R_2D_COMMAND( shaderTime2DCommand_t, RC_2D_SHADER_TIME );
		cmd->startTime = startTime;
		return;
	}
	RB_Set2DInitialShaderTime( startTime );
}

/*
===============
RB_2DCommand

The render thread: a recorded 2D export, run as its body; returns the next
command
===============
*/
#define R_2D_NEXT( type ) ( (const void *)( (const type *)data + 1 ) )
#define R_2D_NEXT_VAR( type, bytes ) ( (const void *)( (const byte *)( (const type *)data + 1 ) + PAD( ( bytes ), sizeof( void * ) ) ) )

const void *RB_2DCommand( const void *data ) {
	switch ( *(const int *)data ) {
	case RC_END_2D_BATCH:
		R_End2DBatch();
		return R_2D_NEXT( end2DBatchCommand_t );
	case RC_2D_SET_COLOR: {
		const set2DColorCommand_t *c = data;
		RB_Draw_SetColor( c->isNull ? NULL : c->rgba );
		return R_2D_NEXT( set2DColorCommand_t );
	}
	case RC_2D_STRETCH_PIC: {
		const pic2DCommand_t *c = data;
		RB_Draw_StretchPic( c->x, c->y, c->w, c->h, c->s1, c->t1, c->s2, c->t2, c->hShader );
		return R_2D_NEXT( pic2DCommand_t );
	}
	case RC_2D_STRETCH_PIC2: {
		const pic2DCommand_t *c = data;
		RB_Draw_StretchPic2( c->x, c->y, c->w, c->h, c->s1, c->t1, c->s2, c->t2, c->sx, c->sy, c->hShader );
		return R_2D_NEXT( pic2DCommand_t );
	}
	case RC_2D_TILE_PIC: {
		const pic2DCommand_t *c = data;
		RB_Draw_TilePic( c->x, c->y, c->w, c->h, c->hShader );
		return R_2D_NEXT( pic2DCommand_t );
	}
	case RC_2D_TILE_PIC_OFFSET: {
		const pic2DCommand_t *c = data;
		RB_Draw_TilePicOffset( c->x, c->y, c->w, c->h, c->hShader, c->offsetX, c->offsetY );
		return R_2D_NEXT( pic2DCommand_t );
	}
	case RC_2D_TRIANGLE_PIC: {
		const trianglePic2DCommand_t *c = data;
		RB_Draw_TrianglePic( c->points, c->texCoords, c->hShader );
		return R_2D_NEXT( trianglePic2DCommand_t );
	}
	case RC_2D_STRING: {
		const string2DCommand_t *c = data;
		RB_Draw_String( (fontheader_t *)c->font, (const char *)( c + 1 ), c->x, c->y, c->maxlen, c->hasVirtual ? c->virtualScreen : NULL );
		return R_2D_NEXT_VAR( string2DCommand_t, c->length + 1 );
	}
	case RC_2D_ADD_BOX: {
		const box2DCommand_t *c = data;
		RB_AddBox( c->x, c->y, c->w, c->h );
		return R_2D_NEXT( box2DCommand_t );
	}
	case RC_2D_DRAW_BOX: {
		const box2DCommand_t *c = data;
		RB_DrawBox( c->x, c->y, c->w, c->h );
		return R_2D_NEXT( box2DCommand_t );
	}
	case RC_2D_DRAW_BOXES: {
		const boxes2DCommand_t *c = data;
		RB_DrawBoxes( (const float (*)[4])( c + 1 ), c->count, c->color );
		return R_2D_NEXT_VAR( boxes2DCommand_t, sizeof( float ) * 4 * c->count );
	}
	case RC_2D_LINE_LOOP: {
		const lineLoop2DCommand_t *c = data;
		RB_DrawLineLoop( (const vec2_t *)( c + 1 ), c->count, c->stippleFactor, c->stippleMask );
		return R_2D_NEXT_VAR( lineLoop2DCommand_t, sizeof( vec2_t ) * c->count );
	}
	case RC_2D_WINDOW: {
		const window2DCommand_t *c = data;
		RB_Set2DWindow( c->x, c->y, c->w, c->h, c->left, c->right, c->bottom, c->top, c->n, c->f );
		return R_2D_NEXT( window2DCommand_t );
	}
	case RC_2D_SCISSOR: {
		const scissor2DCommand_t *c = data;
		RB_Scissor2D( c->x, c->y, c->width, c->height );
		return R_2D_NEXT( scissor2DCommand_t );
	}
	case RC_2D_SHADER_TIME: {
		const shaderTime2DCommand_t *c = data;
		RB_Set2DInitialShaderTime( c->startTime );
		return R_2D_NEXT( shaderTime2DCommand_t );
	}
	case RC_2D_POLYGON: {
		const polygon2DCommand_t *c = data;
		RB_DrawPolygon2D( (const float (*)[4])( c + 1 ), c->count, c->hShader );
		return R_2D_NEXT_VAR( polygon2DCommand_t, sizeof( float ) * 4 * c->count );
	}
	case RC_2D_LIST: {
		const list2DCommand_t *c = data;
		RB_Run2DList( c->list );
		return R_2D_NEXT( list2DCommand_t );
	}
	case RC_SET_RENDER_TIME: {
		const renderTimeCommand_t *c = data;
		RB_SetRenderTime( c->t, c->refdefTime );
		return R_2D_NEXT( renderTimeCommand_t );
	}
	case RC_2D_RADAR_HEIGHT: {
		const radarHeight2DCommand_t *c = data;
		RB_Radar2DHeight( c->hShader, c->z );
		return R_2D_NEXT( radarHeight2DCommand_t );
	}
	default:
		ri.Error( ERR_FATAL, "RB_2DCommand: bad command %d", *(const int *)data );
		return NULL;
	}
}

