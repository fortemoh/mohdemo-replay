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

// vk_blur.c -- the Reloaded menus' backdrop ("*frameblur", vk_frame_blur in
// vk.c): the frame as drawn so far, blurred
//
// The color target is halved twice by linear blits (a 2 x 2 box each) to a
// quarter of its size, blurred there by a Gaussian (shaders/frame_blur.comp:
// across into a half-float image, then down back into the quarter), and the
// quarter scaled into the image (about a quarter of the screen too:
// tr_image.c), which the menus draw stretched over it. Its sigma is
// VK_BLUR_SIGMA pixels at 1080 lines, in proportion to the target's height,
// three sigmas each side. (Halved on down to a 32nd and stretched up, as it
// was, the boxes' grid showed on bright edges as steps.)

#include "vk_local.h"

#define VK_BLUR_SIGMA		20.0f	// pixels at 1080 lines
#define VK_BLUR_QUARTER		4		// the blur's size: the target's over this

// frame_blur.comp's push constants
typedef struct {
	int32_t		w, h;
	float		sigma;
	int32_t		radius;
} vkBlurPush_t;

static struct {
	qboolean				enabled;		// the pipelines are there and the formats can
	VkDescriptorSetLayout	setLayout;
	VkPipelineLayout		layout;
	VkPipeline				acrossPipeline, downPipeline;
	VkDescriptorPool		pool;
	VkDescriptorSet			acrossSet, downSet;	// the quarter into the across image, and back

	// made at the first blur, the target's size: the half (the target's
	// format), the quarter (rgba8: blitted to and from, and the passes' source
	// and destination), the across pass's result (half-float)
	VkImage					half, quarter, across;
	VkDeviceMemory			halfMemory, quarterMemory, acrossMemory;
	VkImageView				halfView, quarterView, acrossView;
	int						qw, qh;
} vkfb;

static qboolean VK_FormatCan( VkFormat format, VkFormatFeatureFlags want ) {
	VkFormatProperties props;

	qvkGetPhysicalDeviceFormatProperties( vk.physicalDevice, format, &props );
	return ( props.optimalTilingFeatures & want ) == want;
}

/*
===============
VK_FrameBlurInit

The pipelines and their two sets; without them (a queue without compute, a
format that can't) there is no "*frameblur" and the menus dim the game
alone
===============
*/
void VK_FrameBlurInit( void ) {
	const VkFormatFeatureFlags blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	VkDescriptorSetLayoutBinding bindings[2];
	VkDescriptorSetLayoutCreateInfo setLayoutInfo;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo layoutInfo;
	VkDescriptorPoolSize poolSize;
	VkDescriptorPoolCreateInfo poolInfo;
	VkDescriptorSetLayout setLayouts[2];
	VkDescriptorSetAllocateInfo allocInfo;
	VkDescriptorSet sets[2];
	VkComputePipelineCreateInfo info;
	VkQueueFamilyProperties families[32];
	uint32_t numFamilies = ARRAY_LEN( families );
	VkShaderModule modules[2];
	VkPipeline *pipelines[2];
	int i;

	Com_Memset( &vkfb, 0, sizeof( vkfb ) );
	qvkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &numFamilies, families );
	if ( vk.queueFamily >= numFamilies || !( families[vk.queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT ) ) {
		return;
	}
	if ( !VK_FormatCan( vk.colorFormat, blit ) || !VK_FormatCan( VK_FORMAT_R8G8B8A8_UNORM, blit | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT )
		|| !VK_FormatCan( VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT ) ) {
		return;
	}

	Com_Memset( bindings, 0, sizeof( bindings ) );
	for ( i = 0; i < 2; i++ ) {
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	Com_Memset( &setLayoutInfo, 0, sizeof( setLayoutInfo ) );
	setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutInfo.bindingCount = 2;
	setLayoutInfo.pBindings = bindings;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &setLayoutInfo, NULL, &vkfb.setLayout ) );

	range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	range.offset = 0;
	range.size = sizeof( vkBlurPush_t );
	Com_Memset( &layoutInfo, 0, sizeof( layoutInfo ) );
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &vkfb.setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layoutInfo, NULL, &vkfb.layout ) );

	// (the sets written once, when the images are made, before either is bound)
	poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	poolSize.descriptorCount = 4;
	Com_Memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 2;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	VK_CHECK( qvkCreateDescriptorPool( vk.device, &poolInfo, NULL, &vkfb.pool ) );
	setLayouts[0] = setLayouts[1] = vkfb.setLayout;
	Com_Memset( &allocInfo, 0, sizeof( allocInfo ) );
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = vkfb.pool;
	allocInfo.descriptorSetCount = 2;
	allocInfo.pSetLayouts = setLayouts;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &allocInfo, sets ) );
	vkfb.acrossSet = sets[0];
	vkfb.downSet = sets[1];

	modules[0] = VK_CreateShaderModule( vk_spirv_frame_blur_comp, vk_spirv_frame_blur_comp_size );
	modules[1] = VK_CreateShaderModule( vk_spirv_frame_blur_vertical_comp, vk_spirv_frame_blur_vertical_comp_size );
	pipelines[0] = &vkfb.acrossPipeline;
	pipelines[1] = &vkfb.downPipeline;
	for ( i = 0; i < 2; i++ ) {
		Com_Memset( &info, 0, sizeof( info ) );
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		info.stage.module = modules[i];
		info.stage.pName = "main";
		info.layout = vkfb.layout;
		VK_CHECK( qvkCreateComputePipelines( vk.device, vk.pipelineCache, 1, &info, NULL, pipelines[i] ) );
		qvkDestroyShaderModule( vk.device, modules[i], NULL );
	}
	vkfb.enabled = qtrue;
}

// once the device is idle: the images, the pipelines, the pool and its sets
void VK_FrameBlurShutdown( void ) {
	VK_DestroyTargetImage( &vkfb.half, &vkfb.halfMemory, &vkfb.halfView );
	VK_DestroyTargetImage( &vkfb.quarter, &vkfb.quarterMemory, &vkfb.quarterView );
	VK_DestroyTargetImage( &vkfb.across, &vkfb.acrossMemory, &vkfb.acrossView );
	if ( vkfb.acrossPipeline ) {
		qvkDestroyPipeline( vk.device, vkfb.acrossPipeline, NULL );
	}
	if ( vkfb.downPipeline ) {
		qvkDestroyPipeline( vk.device, vkfb.downPipeline, NULL );
	}
	if ( vkfb.layout ) {
		qvkDestroyPipelineLayout( vk.device, vkfb.layout, NULL );
	}
	if ( vkfb.setLayout ) {
		qvkDestroyDescriptorSetLayout( vk.device, vkfb.setLayout, NULL );
	}
	if ( vkfb.pool ) {
		qvkDestroyDescriptorPool( vk.device, vkfb.pool, NULL );
	}
	Com_Memset( &vkfb, 0, sizeof( vkfb ) );
}

qboolean vk_frame_blur_supported( void ) {
	return vk.device && vkfb.enabled;
}

// the images at the target's size (their contents only the blur's), and the
// two sets written with them
static void VK_FrameBlurImages( void ) {
	VkDescriptorImageInfo images[4];
	VkWriteDescriptorSet writes[4];
	int i;

	vkfb.qw = Q_max( vk.width / VK_BLUR_QUARTER, 1 );
	vkfb.qh = Q_max( vk.height / VK_BLUR_QUARTER, 1 );
	// (the half sampled: what its view needs; never sampled)
	VK_CreateImage( vk.colorFormat, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, Q_max( vk.width / 2, 1 ), Q_max( vk.height / 2, 1 ), 1, &vkfb.half, &vkfb.halfMemory, &vkfb.halfView );
	VK_CreateImage( VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, vkfb.qw, vkfb.qh,
		1, &vkfb.quarter, &vkfb.quarterMemory, &vkfb.quarterView );
	VK_CreateImage( VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, vkfb.qw,
		vkfb.qh, 1, &vkfb.across, &vkfb.acrossMemory, &vkfb.acrossView );

	// across: the quarter (0) into the across image (1); down: back
	for ( i = 0; i < 4; i++ ) {
		images[i].sampler = VK_NULL_HANDLE;
		images[i].imageView = ( i == 0 || i == 3 ) ? vkfb.quarterView : vkfb.acrossView;
		images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		Com_Memset( &writes[i], 0, sizeof( writes[i] ) );
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = i < 2 ? vkfb.acrossSet : vkfb.downSet;
		writes[i].dstBinding = i & 1;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[i].pImageInfo = &images[i];
	}
	qvkUpdateDescriptorSets( vk.device, 4, writes, 0, NULL );
}

// a blit of src's whole (its size w h) to dst's (dw dh), filtered
static void VK_FrameBlurBlit( VkCommandBuffer cmd, VkImage src, int w, int h, VkImage dst, int dw, int dh ) {
	VkImageBlit blit;

	Com_Memset( &blit, 0, sizeof( blit ) );
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.srcOffsets[1].x = w;
	blit.srcOffsets[1].y = h;
	blit.srcOffsets[1].z = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;
	blit.dstOffsets[1].x = dw;
	blit.dstOffsets[1].y = dh;
	blit.dstOffsets[1].z = 1;
	qvkCmdBlitImage( cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );
}

/*
===============
VK_FrameBlurRecord

The blur's commands, outside a render pass: the target (its layout the
transfer source's) to dst (its layout the shaders' before and after)
===============
*/
void VK_FrameBlurRecord( VkCommandBuffer cmd, VkImage target, const vkTexture_t *dst ) {
	const int hw = Q_max( vk.width / 2, 1 ), hh = Q_max( vk.height / 2, 1 );
	VkMemoryBarrier barrier;
	vkBlurPush_t push;

	if ( !vkfb.half ) {
		VK_FrameBlurImages();
	}

	// the target halved twice (the last frame's blits and passes done with them)
	VK_ImageBarrier( cmd, vkfb.half, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT );
	VK_ImageBarrier( cmd, vkfb.quarter, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT );
	VK_FrameBlurBlit( cmd, target, vk.width, vk.height, vkfb.half, hw, hh );
	VK_ImageBarrier( cmd, vkfb.half, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
	VK_FrameBlurBlit( cmd, vkfb.half, hw, hh, vkfb.quarter, vkfb.qw, vkfb.qh );

	// the Gaussian: across into the across image, down back into the quarter
	VK_ImageBarrier( cmd, vkfb.quarter, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
	VK_ImageBarrier( cmd, vkfb.across, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT );
	push.w = vkfb.qw;
	push.h = vkfb.qh;
	push.sigma = VK_BLUR_SIGMA / VK_BLUR_QUARTER * vk.height / 1080.0f;
	push.radius = (int32_t)ceilf( 3.0f * push.sigma );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkfb.acrossPipeline );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkfb.layout, 0, 1, &vkfb.acrossSet, 0, NULL );
	qvkCmdPushConstants( cmd, vkfb.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
	qvkCmdDispatch( cmd, ( vkfb.qw + 7 ) / 8, ( vkfb.qh + 7 ) / 8, 1 );
	// (the across image's writes before the down pass reads it, its reads of
	// the quarter before it writes it)
	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkfb.downPipeline );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkfb.layout, 0, 1, &vkfb.downSet, 0, NULL );
	qvkCmdDispatch( cmd, ( vkfb.qw + 7 ) / 8, ( vkfb.qh + 7 ) / 8, 1 );

	// the quarter scaled into the image
	VK_ImageBarrier( cmd, vkfb.quarter, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
	VK_ImageBarrier( cmd, dst->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT );
	VK_FrameBlurBlit( cmd, vkfb.quarter, vkfb.qw, vkfb.qh, dst->image, dst->width, dst->height );
	VK_ImageBarrier( cmd, dst->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
}
