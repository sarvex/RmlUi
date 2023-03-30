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

#include "Header.h"
#include "RenderCommands.h"
#include "RenderManager.h"
#include "Texture.h"
#include "Traits.h"
#include "Types.h"
#include "Vertex.h"

namespace Rml {

class Context;
class RenderManager; // TODO remove

/**
    The abstract base class for application-specific rendering implementation. Your application must provide a concrete
    implementation of this class and install it through Rml::SetRenderInterface() in order for anything to be rendered.

    @author Peter Curry
 */

class RMLUICORE_API RenderInterface : public NonCopyMoveable {
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

	/// Called by RmlUi when...
	virtual CompiledShaderHandle CompileShader(const String& name, const Dictionary& parameters);
	/// Called by RmlUi when...
	virtual CompiledFilterHandle CompileFilter(const String& name, const Dictionary& parameters);

	/// Called by RmlUi when...
	virtual void Render(RenderData& render_data);

	// TODO: Replace Release... methods with this
	// virtual void ReleaseResources(const RenderResourceList& resources);

	/// Get the context currently being rendered. This is only valid during RenderGeometry,
	/// CompileGeometry, RenderCompiledGeometry, EnableScissorRegion and SetScissorRegion.
	Context* GetContext() const;

	// TODO: Move to context
	RenderManager manager = {};

	/// TODO Remove
	virtual void ReleaseTexture(TextureHandle texture);

private:
	/// TODO Remove
	virtual void ReleaseCompiledShader(CompiledShaderHandle shader);
	/// TODO Remove
	virtual void ReleaseCompiledFilter(CompiledFilterHandle filter);

	friend class Rml::RenderManager; // TODO Temporary, to gain access to release.

private:
	Context* context;

	friend class Rml::Context;
};

} // namespace Rml
#endif
