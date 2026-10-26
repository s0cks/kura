#ifndef KURA_RENDERER_H
#define KURA_RENDERER_H

#ifdef KURA_RENDER_TARGET_OPENGL
#include "renderer/renderer_gl.h"
#elif KURA_RENDER_TARGET_VULKAN
#include "renderer/renderer_vk.h"
#endif

#endif  // KURA_RENDERER_H
