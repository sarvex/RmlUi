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

class RenderInterface_GL3 : public Rml::RenderInterface {
public:
	RenderInterface_GL3();
	~RenderInterface_GL3();

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
		const Rml::Vector2f& translation) override;

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices,
		Rml::TextureHandle texture) override;
	void RenderCompiledGeometry(Rml::CompiledGeometryHandle geometry, const Rml::Vector2f& translation) override;
	void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle geometry) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(int x, int y, int width, int height) override;
	bool ExecuteStencilCommand(Rml::StencilCommand command, int value, int mask) override;

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture_handle) override;

	void SetTransform(const Rml::Matrix4f* transform) override;

	Rml::TextureHandle ExecuteRenderCommand(Rml::RenderCommand command, Rml::Vector2i offset, Rml::Vector2i dimensions) override;

	Rml::CompiledEffectHandle CompileEffect(const Rml::String& name, const Rml::Dictionary& parameters) override;
	Rml::TextureHandle RenderEffect(Rml::CompiledEffectHandle effect, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
	void ReleaseCompiledEffect(Rml::CompiledEffectHandle effect) override;

	static const Rml::TextureHandle TextureIgnoreBinding = Rml::TextureHandle(-1);
	static const Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

private:
	enum class ProgramId { None, Texture = 1, Color = 2, LinearGradient = 4, All = (Texture | Color | LinearGradient) };
	void SubmitTransformUniform(ProgramId program_id, int uniform_location);

	Rml::Matrix4f transform;
	ProgramId transform_dirty_state = ProgramId::All;

	struct ScissorState {
		bool enabled;
		int x, y, width, height;
	};
	ScissorState scissor_state = {};
	ScissorState pre_filter_scissor_state = {};
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
