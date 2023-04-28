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

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
		const Rml::Vector2f& translation) override;

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices) override;
	void RenderCompiledGeometry(Rml::CompiledGeometryHandle geometry, const Rml::Vector2f& translation, Rml::TextureHandle texture) override;
	void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle geometry) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(int x, int y, int width, int height) override;

	bool EnableClipMask(bool enable) override;
	void RenderToClipMask(Rml::ClipMaskOperation mask_operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture_handle) override;

	void SetTransform(const Rml::Matrix4f* transform) override;

	Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
		Rml::TextureHandle texture) override;
	void ReleaseCompiledShader(Rml::CompiledShaderHandle shader) override;

	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void ReleaseCompiledFilter(Rml::CompiledFilterHandle filter) override;

	void PushLayer(Rml::RenderClear clear_new_layer) override;
	void PopLayer(Rml::BlendMode blend_mode, const Rml::FilterHandleList& filters) override;
		
	Rml::TextureHandle SaveLayerAsTexture(Rml::Vector2i dimensions) override;

	void SaveLayerAsMaskImage() override;
	void ClearMaskImage() override;


	// -- Public methods

	bool Initialize();
	void Shutdown();

	void SetViewport(int width, int height);

	void BeginFrame();
	void EndFrame();

	void Clear();

	// -- Constants
	static constexpr Rml::TextureHandle TextureIgnoreBinding = Rml::TextureHandle(-1);
	static constexpr Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

private:
	void SubmitTransformUniform(Rml::Vector2f translation);

	void BlitTopLayerToPostprocessPrimary();
	void RenderFilters(const Rml::FilterHandleList& filter_handles);

	void SetScissor(Rml::Rectanglei region, bool vertically_flip = false);

	void DrawFullscreenQuad(Rml::Vector2f uv_offset = {}, Rml::Vector2f uv_scaling = Rml::Vector2f(1.f));
	void RenderBlurPass(const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp);
	void RenderBlur(float sigma, const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp, Rml::Rectanglei window_flipped);

	Rml::Matrix4f transform;

	static constexpr size_t MaxNumPrograms = 32;
	std::bitset<MaxNumPrograms> program_transform_dirty;

	Rml::Rectanglei scissor_state;

	bool has_mask = false;

	int viewport_width = 0;
	int viewport_height = 0;
};

#endif
