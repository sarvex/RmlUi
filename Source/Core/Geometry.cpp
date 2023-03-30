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

#include "../../Include/RmlUi/Core/Geometry.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/Core.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/Profiling.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "GeometryDatabase.h"
#include <utility>

namespace Rml {

Geometry::Geometry(Element* host_element) : host_element(host_element)
{
	database_handle = GeometryDatabase::Insert(this);
}

Geometry::Geometry(RenderInterface* render_interface) : render_interface(render_interface)
{
	database_handle = GeometryDatabase::Insert(this);
}

Geometry::Geometry(Geometry&& other)
{
	MoveFrom(other);
	database_handle = GeometryDatabase::Insert(this);
}

Geometry& Geometry::operator=(Geometry&& other)
{
	MoveFrom(other);
	// Keep the database handles from construction unchanged, they are tied to the *this* pointer and should not change.
	return *this;
}

void Geometry::MoveFrom(Geometry& other)
{
	render_interface = std::exchange(other.render_interface, nullptr);
	host_element = std::exchange(other.host_element, nullptr);

	vertices = std::move(other.vertices);
	indices = std::move(other.indices);

	texture = std::exchange(other.texture, nullptr);
}

Geometry::~Geometry()
{
	GeometryDatabase::Erase(database_handle);

	Release();
}

// Set the host element for this geometry; this should be passed in the constructor if possible.
void Geometry::SetHostElement(Element* _host_element)
{
	if (host_element == _host_element)
		return;

	if (host_element)
	{
		Release();
		render_interface = nullptr;
	}

	host_element = _host_element;
}

void Geometry::Render(Vector2f translation)
{
	RenderInterface* render_interface = GetRenderInterface();
	if (!render_interface || indices.empty())
		return;

	translation = translation.Round();

	// Note that Texture::GetHandle can invalidate command pointers due to callbacks.
	TextureHandle texture_handle = (texture ? texture->GetHandle(render_interface) : 0);

	RenderCommand& command =
		render_interface->manager.PushGeometry(&vertices[0], (int)vertices.size(), &indices[0], (int)indices.size(), translation);
	command.texture = texture_handle;
}

void Geometry::Render(CompiledShaderHandle shader_handle, Vector2f translation)
{
	RenderInterface* render_interface = GetRenderInterface();
	if (!render_interface || indices.empty())
		return;

	translation = translation.Round();
	TextureHandle texture_handle = (texture ? texture->GetHandle(render_interface) : 0);

	RenderCommand& command =
		render_interface->manager.PushGeometry(&vertices[0], (int)vertices.size(), &indices[0], (int)indices.size(), translation);

	command.type = RenderCommandType::RenderShader;
	command.render_shader.handle = shader_handle;
	command.texture = texture_handle;
}

void Geometry::RenderToClipMask(ClipMaskOperation clip_mask, Vector2f translation)
{
	RenderInterface* render_interface = GetRenderInterface();
	if (!render_interface || indices.empty())
		return;

	translation = translation.Round();
	TextureHandle texture_handle = (texture ? texture->GetHandle(render_interface) : 0);
	RenderCommand& command =
		render_interface->manager.PushGeometry(&vertices[0], (int)vertices.size(), &indices[0], (int)indices.size(), translation);

	command.type = RenderCommandType::RenderClipMask;
	command.render_clip_mask.operation = clip_mask;
	command.texture = texture_handle;
}

// Returns the geometry's vertices. If these are written to, Release() should be called to force a recompile.
Vector<Vertex>& Geometry::GetVertices()
{
	return vertices;
}

// Returns the geometry's indices. If these are written to, Release() should be called to force a recompile.
Vector<int>& Geometry::GetIndices()
{
	return indices;
}

// Gets the geometry's texture.
const Texture* Geometry::GetTexture() const
{
	return texture;
}

// Sets the geometry's texture.
void Geometry::SetTexture(const Texture* _texture)
{
	texture = _texture;
	Release();
}

void Geometry::Release(bool clear_buffers)
{
	if (clear_buffers)
	{
		vertices.clear();
		indices.clear();
	}
}

Geometry::operator bool() const
{
	return !indices.empty();
}

RenderInterface* Geometry::GetRenderInterface()
{
	if (!render_interface)
	{
		if (host_element)
		{
			if (Context* host_context = host_element->GetContext())
				render_interface = host_context->GetRenderInterface();
		}

		if (!render_interface)
			render_interface = ::Rml::GetRenderInterface();
	}

	return render_interface;
}

} // namespace Rml
