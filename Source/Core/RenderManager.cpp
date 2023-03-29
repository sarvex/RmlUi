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

#include "../../Include/RmlUi/Core/RenderManager.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"

namespace Rml {

void RenderManager::Reset(RenderInterface* render_interface)
{
	// @performance Clear the vectors in the command list instead of re-initializing it, so that they retain their capacity buffers.
	list = {};
	active_transform = 0;
	active_scissor = 0;

	list.translations.push_back(Vector2f(0.f));
	list.transforms.push_back(Matrix4f::Identity());

	list.scissor_regions.push_back(Rectanglei::CreateInvalid());
	list.filter_lists.push_back(FilterHandleList{});

	attached_filters.clear();

	if (render_interface)
	{
		for (CompiledFilterHandle handle : release_queue_filters)
			render_interface->ReleaseCompiledFilter(handle);
		for (CompiledShaderHandle handle : release_queue_shaders)
			render_interface->ReleaseCompiledShader(handle);
		for (TextureHandle handle : release_queue_textures)
			render_interface->ReleaseTexture(handle);

		release_queue_filters.clear();
		release_queue_shaders.clear();
		release_queue_textures.clear();
	}
	else
	{
		RMLUI_ASSERT(release_queue_filters.empty() && release_queue_shaders.empty() && release_queue_textures.empty());
	}
}

} // namespace Rml
