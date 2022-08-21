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
enum class ProgramId { None, Texture, Color, Gradient, Creation, Passthrough, ColorMatrix, Blur, Dropshadow, BlendMask, Count };

class RenderInterface_GL3 : public Rml::RenderInterface {
public:
	RenderInterface_GL3();
	~RenderInterface_GL3();

	void BeginFrame();

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
		const Rml::Vector2f& translation) override;

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices,
		Rml::TextureHandle texture) override;
	void RenderCompiledGeometry(Rml::CompiledGeometryHandle geometry, const Rml::Vector2f& translation) override;
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
	void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
	void ReleaseCompiledShader(Rml::CompiledShaderHandle shader) override;

	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void AttachFilter(Rml::CompiledFilterHandle filter) override;
	void ReleaseCompiledFilter(Rml::CompiledFilterHandle filter) override;

	void PushLayer(Rml::RenderClear clear_new_layer) override;
	Rml::TextureHandle PopLayer(Rml::RenderTarget render_target, Rml::BlendMode blend_mode) override;

	static const Rml::TextureHandle TextureIgnoreBinding = Rml::TextureHandle(-1);
	static const Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

private:
	void SubmitTransformUniform(Rml::Vector2f translation);

	void RenderFilters();

	Rml::Matrix4f transform;
	std::bitset<(size_t)ProgramId::Count> program_transform_dirty;

	struct ScissorState {
		bool enabled;
		int x, y, width, height;
	};
	ScissorState scissor_state = {};

	Rml::Vector<CompiledFilter*> attached_filters;
	bool has_mask = false;
};

namespace RmlGL3 {

bool Initialize();
void Shutdown();

void SetViewport(int width, int height);

void BeginFrame();
void EndFrame();

void Clear();

} // namespace RmlGL3

#endif
