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

#ifndef RMLUI_CORE_RENDERINTERFACE_H
#define RMLUI_CORE_RENDERINTERFACE_H

#include "Traits.h"
#include "Header.h"
#include "Texture.h"
#include "Vertex.h"
#include "Types.h"

namespace Rml {

class Context;

enum class ClipMaskOperation { Clip, ClipIntersect, ClipOut };
enum class RenderClear { None, Clear, Clone };
enum class RenderTarget { Layer, MaskImage, RenderTexture };
enum class BlendMode { Blend, Replace };

using RenderCommandUserData = uintptr_t;
using FilterHandleList = Vector<CompiledFilterHandle>;

struct RenderCommand {
	enum class Type {
		RenderGeometry,
		
		EnableClipMask,
		DisableClipMask,
		RenderClipMask,

		PushLayer,
		PopLayer,

		RenderShader,

		AttachFilter,
	};
	Type type;

	// -- Geometry, RenderClipMask, RenderShader
	int vertices_offset;
	int indices_offset;
	int num_elements;

	int translation_offset;
	int transform_offset;
	
	int scissor_offset;

	TextureHandle texture; // Texture to attach to the geometry. PopLayer: Render texture target.

	// -- RenderClipMask
	ClipMaskOperation clip_mask_operation;

	// -- RenderShader
	CompiledShaderHandle shader;

	// -- PushLayer
	RenderClear clear_new_layer;

	// -- PopLayer
	RenderTarget render_target;
	BlendMode blend_mode;
	int filter_lists_offset;

	// -- All
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

/**
	The abstract base class for application-specific rendering implementation. Your application must provide a concrete
	implementation of this class and install it through Rml::SetRenderInterface() in order for anything to be rendered.

	@author Peter Curry
 */

class RMLUICORE_API RenderInterface : public NonCopyMoveable
{
public:
	RenderInterface();
	virtual ~RenderInterface();

	/// Called by RmlUi when a texture is required by the library.
	/// @param[out] texture_handle The handle to write the texture handle for the loaded texture to.
	/// @param[out] texture_dimensions The variable to write the dimensions of the loaded texture.
	/// @param[in] source The application-defined image source, joined with the path of the referencing document.
	/// @return True if the load attempt succeeded and the handle and dimensions are valid, false if not.
	virtual bool LoadTexture(TextureHandle& texture_handle, Vector2i& texture_dimensions, const String& source);
	/// Called by RmlUi when a texture is required to be built from an internally-generated sequence of pixels.
	/// @param[out] texture_handle The handle to write the texture handle for the generated texture to.
	/// @param[in] source The raw 8-bit texture data. Each pixel is made up of four 8-bit values, indicating red, green, blue and alpha in that order.
	/// @param[in] source_dimensions The dimensions, in pixels, of the source data.
	/// @return True if the texture generation succeeded and the handle is valid, false if not.
	virtual bool GenerateTexture(TextureHandle& texture_handle, const byte* source, const Vector2i& source_dimensions);
	/// Called by RmlUi when it wants to create a render texture it can use as a render target, and subsequently render as a normal texture.
	virtual bool GenerateRenderTexture(TextureHandle& texture_handle, Vector2i dimensions);
	/// Called by RmlUi when a loaded texture or render texture is no longer required.
	/// @param texture The texture handle to release.
	virtual void ReleaseTexture(TextureHandle texture);

	/// Called by RmlUi when...
	virtual CompiledShaderHandle CompileShader(const String& name, const Dictionary& parameters);
	/// Called by RmlUi when...
	virtual void ReleaseCompiledShader(CompiledShaderHandle shader);

	/// Called by RmlUi when...
	virtual CompiledFilterHandle CompileFilter(const String& name, const Dictionary& parameters);
	/// Called by RmlUi when...
	virtual void ReleaseCompiledFilter(CompiledFilterHandle filter);

	/// Get the context currently being rendered. This is only valid during RenderGeometry,
	/// CompileGeometry, RenderCompiledGeometry, EnableScissorRegion and SetScissorRegion.
	Context* GetContext() const;

private:
	Context* context;

	friend class Rml::Context;
};

} // namespace Rml
#endif
