## RmlUi Backends

RmlUi backends provide support for various renderers and platforms. The following explains the terms used here:

- ***Renderer***: Implements the [render interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render) for a given rendering API, and provides initialization code when necessary.
- ***Platform***: Implements the [system interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/system.html) for a given platform (operating system or window library), and adds procedures for opening a window and providing input to the RmlUi context.
- ***Backend***: Combines a renderer and a platform for a complete solution sample, implementing the [Backend interface](RmlUi_Backend.h).

The provided renderers and platforms are intended to be usable as-is by client projects without modifications. We encourage users to only make changes here when they are useful to all users, and then contribute back to the project. Feedback is welcome, and finding the proper abstraction level is a work-in-progress. The provided interfaces are designed such that they can be derived from and further customized by the backend.

The provided backends on the other hand are not intended to be used directly by client projects, but rather copied and modified as needed. They implement the desired feature set for the [included samples](../Samples/), and should mainly be considered as examples.

### Renderers

| Renderer features | Basic rendering | Stencil | Transforms | Built-in image support                                                          |
|-------------------|:---------------:|---------|:----------:|---------------------------------------------------------------------------------|
| GL2               |        ✔️       |    ✔️    |      ✔️    | Uncompressed TGA                                                                |
| SDLrenderer       |        ✔️       |    ❌    |      ❌    | Based on [SDL_image](https://www.libsdl.org/projects/SDL_image/docs/index.html) |

**Basic rendering**: Render geometry with colors, textures, and rectangular clipping (scissoring). Sufficient for basic 2d-layouts.\
**Stencil**: Enables proper clipping when the `border-radius` property is set, and when transforms are enabled.\
**Transforms**: Enables the `transform` and `perspective` properties to take effect.\
**Built-in image support**: Image support is typically extended by the backend, this only shows the supported formats built-in to the renderer.

### Platforms

| Platform support | Basic windowing | Clipboard | High DPI | Comments                                          |
|------------------|:---------------:|:---------:|:----------:|---------------------------------------------------|
| Win32            |        ✔️        |     ✔️     |     ✔️    | High DPI only supported on Windows 10 and newer.  |
| X11              |        ✔️        |     ✔️     |     ❌    |                                                   |
| GLFW             |        ✔️        |     ✔️     |     ✔️    |                                                   |
| SDL              |        ✔️        |     ✔️     |     ❌    |                                                   |
| SFML             |        ✔️        |     ⚠️     |     ❌    | Some issues with Unicode characters in clipboard. |

**Basic windowing**: Open windows, react to resize events, submit inputs to the RmlUi context.\
**Clipboard**: Read from and write to the system clipboard.\
**High DPI**: Scale the [dp-ratio](https://mikke89.github.io/RmlUiDoc/pages/rcss/syntax#dp-unit) of RmlUi contexts based on the monitor's DPI settings. React to DPI-changes, either because of changed settings or when moving the window to another monitor.

### Backends

| Platform \ Renderer | OpengGL 2 | SDLrenderer |
|---------------------|:---------:|:-----------:|
| Win32               |     ✔️     |             |
| X11                 |     ✔️     |             |
| GLFW                |     ✔️     |             |
| SDL                 |     ✔️     |      ✔️      |
| SFML                |     ✔️     |             |
