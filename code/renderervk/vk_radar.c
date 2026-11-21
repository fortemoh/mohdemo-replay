/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// vk_radar.c -- the Competitive HUD radar's picture made on the GPU
//
// The UI lists each cell's floors once a map and walk (radar2D_t, RM_Floors
// in cl_uicomp.cpp) and gives them to the renderer (RE_RegisterRadar2D in
// tr_image.c); a storey's change is then a number (RE_Radar2DHeight). The
// list is copied to a storage buffer on the GPU (through the staging buffer,
// once), and shaders/radar.comp makes the picture for a height from it into
// the room buffer: the picture as its place in the 2D atlas page holds it
// (its edge texels copied once around it, as R_AtlasFill's), or as a texture
// of its own holds it when it is too big for the page. The room is copied to
// that place as an upload is (VK_UploadTextureGPU: in the upload command
// buffer, or in the main one after the draws that read the old texels), so
// the picture is drawn exactly as the UI's own one, from the same place.
// The whole picture: a thread a room texel (the load, a new list). A
// storey's change: a thread a cell of those that can change (more than one
// floor, a stair, or no floor and a corner filled by a cell of more than
// one: the edges are smoothed, radar.comp), writing only its texels that
// do, the room copied whole after. Byte for byte the texels of the UI's own
// picture (RM_Paint): see radar.glsl.

#include "vk_local.h"

// radar.comp's push constants
typedef struct {
	uint32_t	cw, ch, t;
	uint32_t	floorsWord, changingWord, numChanging;
	uint32_t	border;
	float		z, zOld;
} vkRadarPush_t;

#define VK_RADAR_SETS	16	// a set a list; the old ones freed once their frames are done

static struct {
	qboolean				enabled;		// the pipelines are there
	VkDescriptorSetLayout	setLayout;
	VkPipelineLayout		layout;
	VkPipeline				fullPipeline, changingPipeline;
	VkDescriptorPool		pool;

	// the image that is the radar's picture, NULL: none
	const image_t			*image;
	vkBuffer_t				floors;			// the list on the GPU
	vkBuffer_t				room;			// the picture's texels as its place holds them
	VkDescriptorSet			set;
	int						roomWidth, roomHeight;
	vkRadarPush_t			push;			// z: the height the room's texels are for
} vkr;

/*
===============
VK_RadarInit

The pipelines; without them (a queue without compute, an error) the
radar's picture stays the UI's (RE_RegisterRadar2D returns 0)
===============
*/
void VK_RadarInit( void ) {
	VkDescriptorSetLayoutBinding bindings[2];
	VkDescriptorSetLayoutCreateInfo setLayoutInfo;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo layoutInfo;
	VkDescriptorPoolSize poolSize;
	VkDescriptorPoolCreateInfo poolInfo;
	VkComputePipelineCreateInfo info;
	VkQueueFamilyProperties families[32];
	uint32_t numFamilies = ARRAY_LEN( families );
	VkShaderModule modules[2];
	VkPipeline *pipelines[2];
	int i;

	Com_Memset( &vkr, 0, sizeof( vkr ) );
	qvkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &numFamilies, families );
	if ( vk.queueFamily >= numFamilies || !( families[vk.queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT ) ) {
		return;
	}

	Com_Memset( bindings, 0, sizeof( bindings ) );
	for ( i = 0; i < 2; i++ ) {
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	Com_Memset( &setLayoutInfo, 0, sizeof( setLayoutInfo ) );
	setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutInfo.bindingCount = 2;
	setLayoutInfo.pBindings = bindings;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &setLayoutInfo, NULL, &vkr.setLayout ) );

	range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	range.offset = 0;
	range.size = sizeof( vkRadarPush_t );
	Com_Memset( &layoutInfo, 0, sizeof( layoutInfo ) );
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &vkr.setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layoutInfo, NULL, &vkr.layout ) );

	poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSize.descriptorCount = 2 * VK_RADAR_SETS;
	Com_Memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = VK_RADAR_SETS;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	VK_CHECK( qvkCreateDescriptorPool( vk.device, &poolInfo, NULL, &vkr.pool ) );

	modules[0] = VK_CreateShaderModule( vk_spirv_radar_comp, vk_spirv_radar_comp_size );
	modules[1] = VK_CreateShaderModule( vk_spirv_radar_changing_comp, vk_spirv_radar_changing_comp_size );
	pipelines[0] = &vkr.fullPipeline;
	pipelines[1] = &vkr.changingPipeline;
	for ( i = 0; i < 2; i++ ) {
		Com_Memset( &info, 0, sizeof( info ) );
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		info.stage.module = modules[i];
		info.stage.pName = "main";
		info.layout = vkr.layout;
		VK_CHECK( qvkCreateComputePipelines( vk.device, vk.pipelineCache, 1, &info, NULL, pipelines[i] ) );
		qvkDestroyShaderModule( vk.device, modules[i], NULL );
	}
	vkr.enabled = qtrue;
}

// the radar's list and picture given up: freed once the frames that use them are done
static void VK_RadarRelease( void ) {
	VK_DeferDestroy( VK_DESTROY_DESCRIPTOR_SET, (uint64_t)vkr.set, (uint64_t)vkr.pool );
	VK_DeferDestroy( VK_DESTROY_BUFFER, (uint64_t)vkr.floors.buffer, 0 );
	VK_DeferDestroy( VK_DESTROY_MEMORY, (uint64_t)vkr.floors.memory, 0 );
	VK_DeferDestroy( VK_DESTROY_BUFFER, (uint64_t)vkr.room.buffer, 0 );
	VK_DeferDestroy( VK_DESTROY_MEMORY, (uint64_t)vkr.room.memory, 0 );
	Com_Memset( &vkr.floors, 0, sizeof( vkr.floors ) );
	Com_Memset( &vkr.room, 0, sizeof( vkr.room ) );
	vkr.set = VK_NULL_HANDLE;
	vkr.image = NULL;
}

/*
===============
VK_RadarShutdown

Called once the device is idle: the list and the picture go to the deferred
destruction lists, which the caller processes before VK_RadarShutdownObjects
===============
*/
void VK_RadarShutdown( void ) {
	if ( vkr.image ) {
		VK_RadarRelease();
	}
}

void VK_RadarShutdownObjects( void ) {
	if ( vkr.fullPipeline ) {
		qvkDestroyPipeline( vk.device, vkr.fullPipeline, NULL );
	}
	if ( vkr.changingPipeline ) {
		qvkDestroyPipeline( vk.device, vkr.changingPipeline, NULL );
	}
	if ( vkr.layout ) {
		qvkDestroyPipelineLayout( vk.device, vkr.layout, NULL );
	}
	if ( vkr.setLayout ) {
		qvkDestroyDescriptorSetLayout( vk.device, vkr.setLayout, NULL );
	}
	if ( vkr.pool ) {
		qvkDestroyDescriptorPool( vk.device, vkr.pool, NULL );
	}
	Com_Memset( &vkr, 0, sizeof( vkr ) );
}

// the paint's commands, before the copy of the room into its place (VK_UploadTextureGPU)
static void VK_RadarRecord( VkCommandBuffer cmd, const void *data ) {
	const vkRadarPush_t *push = data;
	VkMemoryBarrier barrier;

	// after the list's copy to the GPU, the last paint's writes and the
	// room's last copy into its place
	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &barrier, 0, NULL, 0, NULL );

	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, push->numChanging ? vkr.changingPipeline : vkr.fullPipeline );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkr.layout, 0, 1, &vkr.set, 0, NULL );
	qvkCmdPushConstants( cmd, vkr.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( *push ), push );
	if ( push->numChanging ) {
		qvkCmdDispatch( cmd, ( push->numChanging + 63 ) / 64, 1, 1 );
	} else {
		qvkCmdDispatch( cmd, ( vkr.roomWidth + 7 ) / 8, ( vkr.roomHeight + 7 ) / 8, 1 );
	}

	// the room's texels to the copy
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
}

// the room's texels for height z: all of them (full), else those of the
// changing cells that change; then the room to its place
static void VK_RadarPaint( float z, qboolean full ) {
	image_t *target = vkr.image->atlas ? vkr.image->atlas : (image_t *)vkr.image;
	vkRadarPush_t push = vkr.push;

	push.zOld = vkr.push.z;
	push.z = z;
	vkr.push.z = z;
	if ( !full ) {
		// (no cell can change: a map of one storey without walls)
		if ( !push.numChanging ) {
			return;
		}
	} else {
		push.numChanging = 0;	// (the whole picture)
	}
	VK_UploadTextureGPU( target, vkr.image->atlas ? vkr.image->atlasRoom[0] : 0, vkr.image->atlas ? vkr.image->atlasRoom[1] : 0,
		vkr.roomWidth, vkr.roomHeight, vkr.room.buffer, 0, VK_RadarRecord, &push );
}

/*
===============
vk_radar_image

The radar's picture (see vk.h); the front end's, after R_FrontEndSync
===============
*/
qboolean vk_radar_image( const image_t *image, const radar2D_t *radar, float z ) {
	const int width = radar->cw * radar->texels, height = radar->ch * radar->texels;
	const int border = image->atlas ? 1 : 0;
	const VkDeviceSize roomBytes = (VkDeviceSize)( width + 2 * border ) * ( height + 2 * border ) * 4;
	const VkDeviceSize floorsBytes = (VkDeviceSize)( (const byte *)( radar->changing + radar->numChanging ) - (const byte *)radar->memory );
	VkDescriptorSetAllocateInfo allocInfo;
	VkDescriptorBufferInfo bufferInfo[2];
	VkWriteDescriptorSet writes[2];
	VkDescriptorSet set = VK_NULL_HANDLE;
	VkBufferCopy copy;
	VkBuffer staging;
	VkDeviceSize offset;
	vkBuffer_t floors, room;
	int i;

	if ( !vk.device || !vkr.enabled || roomBytes > vk.deviceProperties.limits.maxStorageBufferRange
		|| floorsBytes > vk.deviceProperties.limits.maxStorageBufferRange ) {
		radar->release( radar->memory );
		return qfalse;
	}
	if ( vkr.image ) {
		VK_RadarRelease();	// (the last list's)
	}

	Com_Memset( &allocInfo, 0, sizeof( allocInfo ) );
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = vkr.pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &vkr.setLayout;
	if ( qvkAllocateDescriptorSets( vk.device, &allocInfo, &set ) != VK_SUCCESS ) {
		radar->release( radar->memory );
		return qfalse;
	}

	vkr.push.cw = radar->cw;
	vkr.push.ch = radar->ch;
	vkr.push.t = radar->texels;
	vkr.push.numChanging = radar->numChanging;
	vkr.push.floorsWord = (uint32_t)( ( (const byte *)radar->floors - (const byte *)radar->memory ) / 4 );
	vkr.push.changingWord = (uint32_t)( ( (const byte *)radar->changing - (const byte *)radar->memory ) / 4 );

	// the list: through the staging buffer to the GPU (the UI's memory given back)
	VK_CreateBuffer( &floors, floorsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 );
	VK_CreateBuffer( &room, roomBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 );
	Com_Memcpy( VK_StagingAlloc( floorsBytes, 16, &staging, &offset ), radar->memory, floorsBytes );
	radar->release( radar->memory );
	copy.srcOffset = offset;
	copy.dstOffset = 0;
	copy.size = floorsBytes;
	qvkCmdCopyBuffer( VK_GetUploadCommandBuffer(), staging, floors.buffer, 1, &copy );

	for ( i = 0; i < 2; i++ ) {
		bufferInfo[i].buffer = i ? room.buffer : floors.buffer;
		bufferInfo[i].offset = 0;
		bufferInfo[i].range = VK_WHOLE_SIZE;
		Com_Memset( &writes[i], 0, sizeof( writes[i] ) );
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = set;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].pBufferInfo = &bufferInfo[i];
	}
	qvkUpdateDescriptorSets( vk.device, 2, writes, 0, NULL );

	vkr.image = image;
	vkr.floors = floors;
	vkr.room = room;
	vkr.set = set;
	vkr.roomWidth = width + 2 * border;
	vkr.roomHeight = height + 2 * border;
	vkr.push.border = border;
	VK_RadarPaint( z, qtrue );
	return qtrue;
}

/*
===============
vk_radar_height

The radar picture's texels for height z (see vk.h); the render thread's, or
the front end's without it
===============
*/
void vk_radar_height( const image_t *image, float z ) {
	if ( image && image == vkr.image && z != vkr.push.z ) {
		VK_RadarPaint( z, qfalse );
	}
}

/*
===============
vk_radar_forget

The image is no longer the radar's picture (made again from the CPU's
texels, or freed); after R_FrontEndSync
===============
*/
void vk_radar_forget( const image_t *image ) {
	if ( image && image == vkr.image ) {
		VK_RadarRelease();
	}
}
