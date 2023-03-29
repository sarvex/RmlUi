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

#ifndef RMLUI_BACKENDS_RENDERER_GL3_H
#define RMLUI_BACKENDS_RENDERER_GL3_H

#include <RmlUi/Core/RenderInterface.h>
#include <bitset>

struct CompiledFilter;
namespace Gfx {
struct FramebufferData;
}

class RenderInterface_GL3 : public Rml::RenderInterface {
public:
	RenderInterface_GL3();
	~RenderInterface_GL3();

	// -- Inherited from Rml::Interface

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture_handle) override;

	Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void ReleaseCompiledShader(Rml::CompiledShaderHandle shader) override;

	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void ReleaseCompiledFilter(Rml::CompiledFilterHandle filter) override;

	void Render(Rml::RenderCommandList& commands) override;

	// -- Public methods

	void BeginFrame();
	void EndFrame();
	
	bool Initialize();
	void Shutdown();

	void Clear();

	void SetViewport(int width, int height);

	// -- Constants
	static const Rml::TextureHandle TextureIgnoreBinding = Rml::TextureHandle(-1);
	static const Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

private:
	// -- Render helpers
	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
		const Rml::Vector2f& translation);

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture);
	void RenderCompiledGeometry(Rml::CompiledGeometryHandle geometry, const Rml::Vector2f& translation);
	void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle geometry);

	void EnableScissorRegion(bool enable);
	void SetScissorRegion(int x, int y, int width, int height);
	void SetScissorRegion(Rml::Rectanglei region);

	bool EnableClipMask(bool enable);
	void RenderToClipMask(Rml::ClipMaskOperation mask_operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation);
	void SetTransform(const Rml::Matrix4f* transform);

	void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation);
	void AttachFilter(Rml::CompiledFilterHandle filter);
	void PushLayer(Rml::RenderClear clear_new_layer);
	Rml::TextureHandle PopLayer(Rml::RenderTarget render_target, Rml::BlendMode blend_mode);

	void SubmitTransformUniform(Rml::Vector2f translation);

	void RenderFilters();

	void DrawFullscreenQuad(Rml::Vector2f uv_offset = {}, Rml::Vector2f uv_scaling = Rml::Vector2f(1.f));

	void RenderBlurPass(const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp);
	void RenderBlur(float sigma, const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp, Rml::Vector2i position,
		Rml::Vector2i size);

	// -- State
	Rml::Matrix4f transform;

	static constexpr size_t MaxNumPrograms = 32;
	std::bitset<MaxNumPrograms> program_transform_dirty;

	struct ScissorState {
		bool enabled;
		int x, y, width, height;
	};
	ScissorState scissor_state = {};

	Rml::Vector<CompiledFilter*> attached_filters;
	bool has_mask = false;

	int viewport_width = 0;
	int viewport_height = 0;
};

#endif
