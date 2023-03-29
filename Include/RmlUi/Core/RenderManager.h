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
#include "RenderCommands.h"

namespace Rml {

class RenderManager {
public:
	RenderManager() { Reset(); }

	RenderCommand& PushGeometry(const Vertex* vertices, int num_vertices, const int* indices, int num_indices, Vector2f translation)
	{
		list.commands.push_back(RenderCommand{});
		RenderCommand& command = list.commands.back();

		command.type = RenderCommandType::RenderGeometry;

		auto& geometry = command.geometry;
		geometry.vertices_offset = (int)list.vertices.size();
		list.vertices.insert(list.vertices.end(), vertices, vertices + num_vertices);
		geometry.indices_offset = (int)list.indices.size();
		list.indices.insert(list.indices.end(), indices, indices + num_indices);
		geometry.num_elements = num_indices;

		geometry.translation_offset = (int)list.translations.size();
		list.translations.push_back(translation);

		geometry.scissor_offset = active_scissor;
		geometry.transform_offset = active_transform;

		return command;
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

	void Reset()
	{
		// @performance Clear the vectors in the command list instead of re-initializing it, so that they retain their capacity buffers.
		list = {};
		active_transform = 0;
		active_scissor = 0;

		list.transforms.push_back(Matrix4f::Identity());
		list.translations.push_back(Vector2f(0.f));
		list.scissor_regions.push_back(Rectanglei::CreateInvalid());
	}

	RenderCommandList& GetList() { return list; }

private:
	RenderCommandList list;

	int active_scissor = 0;
	int active_transform = 0;
};

} // namespace Rml
#endif
