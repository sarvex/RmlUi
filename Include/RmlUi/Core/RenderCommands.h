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

#ifndef RMLUI_CORE_RENDERCOMMANDS_H
#define RMLUI_CORE_RENDERCOMMANDS_H

#include "Types.h"
#include "Vertex.h"

namespace Rml {

enum class ClipMaskOperation { Clip, ClipIntersect, ClipOut };
enum class RenderClear { None, Clear, Clone };
enum class RenderTarget { Layer, MaskImage, RenderTexture };
enum class BlendMode { Blend, Replace };

using RenderCommandUserData = uintptr_t;
using FilterHandleList = Vector<CompiledFilterHandle>;

enum class RenderCommandType {
	RenderGeometry,

	EnableClipMask,
	DisableClipMask,
	RenderClipMask,

	PushLayer,
	PopLayer,

	RenderShader,
};

struct RenderCommand {
	RenderCommandType type;

	// -- Geometry (RenderGeometry, RenderClipMask, RenderShader)
	struct Geometry {
		int vertices_offset;
		int indices_offset;
		int num_elements;

		int translation_offset;
		int transform_offset;

		int scissor_offset;
	} geometry;

	// -- Render commands: Texture to attach to the geometry. PopLayer: Render texture target.
	TextureHandle texture;

	struct RenderGeometry {
		int filter_lists_offset;
	} render_geometry;

	struct RenderClipMask {
		ClipMaskOperation operation;
	} render_clip_mask;

	struct RenderShader {
		CompiledShaderHandle handle;
	} render_shader;

	struct PushLayer {
		RenderClear clear_new_layer;
	} push_layer;

	struct PopLayer {
		RenderTarget render_target;
		BlendMode blend_mode;
		int filter_lists_offset;
	} pop_layer;

	RenderCommandUserData user_data;
};

struct RenderCommandList {
	Vector<Vertex> vertices;
	Vector<int> indices;

	Vector<Vector2f> translations;
	Vector<Matrix4f> transforms;

	Vector<Rectanglei> scissor_regions;

	Vector<FilterHandleList> filter_lists;

	Vector<RenderCommand> commands;
};

} // namespace Rml
#endif
