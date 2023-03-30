/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef RMLUI_CORE_RENDERMANAGER_H
#define RMLUI_CORE_RENDERMANAGER_H

#include "Header.h"
#include "RenderData.h"

namespace Rml {

class RenderInterface;

class RenderManager {
public:
	RenderManager() { Reset(nullptr); }

	RenderCommandGeometry PushGeometry(const Vertex* vertices, int num_vertices, const int* indices, int num_indices, Vector2f translation)
	{
		RenderCommandGeometry geometry;
		geometry.vertices_offset = (int)list.vertices.size();
		list.vertices.insert(list.vertices.end(), vertices, vertices + num_vertices);
		geometry.indices_offset = (int)list.indices.size();
		list.indices.insert(list.indices.end(), indices, indices + num_indices);
		geometry.num_elements = num_indices;

		geometry.translation_offset = (int)list.translations.size();
		list.translations.push_back(translation);

		geometry.transform_offset = active_transform;

		return geometry;
	}

	void SetScissor(Rectanglei scissor)
	{
		active_scissor = (int)list.scissor_regions.size();
		list.scissor_regions.push_back(scissor);
	}
	void DisableScissor() { active_scissor = 0; }

	void SetTransform(const Matrix4f& transform)
	{
		active_transform = (int)list.transforms.size();
		list.transforms.push_back(transform);
	}
	void DisableTransform() { active_transform = 0; }

	void PushLayer(RenderClear clear_new_layer)
	{
		RenderCommand& command = PushCommand(RenderCommandType::PushLayer);
		command.push_layer.clear_new_layer = clear_new_layer;
	}
	void PopLayer(RenderTarget render_target, BlendMode blend_mode, TextureHandle render_texture = {})
	{
		RenderCommand& command = PushCommand(RenderCommandType::PopLayer);
		command.pop_layer.render_target = render_target;
		command.pop_layer.blend_mode = blend_mode;
		command.pop_layer.render_texture = render_texture;
		if (!attached_filters.empty())
		{
			command.pop_layer.filter_lists_offset = (int)list.filter_lists.size();
			list.filter_lists.push_back(std::move(attached_filters));
			attached_filters.clear();
		}
	}

	void RenderGeometry(RenderCommandGeometry geometry, TextureHandle texture_handle)
	{
		RenderCommand& command = PushCommand(RenderCommandType::RenderGeometry);
		command.render_geometry.geometry = geometry;
		command.render_geometry.texture = texture_handle;
	}
	void RenderClipMask(RenderCommandGeometry geometry, TextureHandle texture_handle, ClipMaskOperation operation)
	{
		RenderCommand& command = PushCommand(RenderCommandType::RenderClipMask);
		command.render_clip_mask.geometry = geometry;
		command.render_clip_mask.texture = texture_handle;
		command.render_clip_mask.operation = operation;
	}
	void DisableClipMask() { PushCommand(RenderCommandType::DisableClipMask); }

	void RenderShader(RenderCommandGeometry geometry, TextureHandle texture_handle, CompiledShaderHandle shader_handle)
	{
		RenderCommand& command = PushCommand(RenderCommandType::RenderShader);
		command.render_shader.geometry = geometry;
		command.render_shader.texture = texture_handle;
		command.render_shader.handle = shader_handle;
	}

	void AttachFilter(CompiledFilterHandle handle) { attached_filters.push_back(handle); }

	void QueueReleaseFilter(CompiledFilterHandle handle) { release_queue.filters.push_back(handle); }
	void QueueReleaseShader(CompiledShaderHandle handle) { release_queue.shaders.push_back(handle); }
	void QueueReleaseTexture(TextureHandle handle) { release_queue.textures.push_back(handle); }

	void Reset(RenderInterface* render_interface);

	RenderData& GetList() { return list; }

private:
	RenderCommand& PushCommand(RenderCommandType command_type)
	{
		list.commands.push_back(RenderCommand{});
		RenderCommand& command = list.commands.back();
		command.type = command_type;
		command.scissor_offset = active_scissor;
		return command;
	}

	RenderData list;

	RenderResourceList release_queue;

	Vector<CompiledFilterHandle> attached_filters;

	int active_scissor = 0;
	int active_transform = 0;
};

} // namespace Rml
#endif
