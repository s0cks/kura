#ifndef KURA_TESTS_ABI_PRINT_H
#define KURA_TESTS_ABI_PRINT_H

// So that a failed EXPECT_EQ on a status says `ERR_NOT_IN_FRAME`, not `2`. It's
// in the global namespace because that's where KuraRendererStatus is, which is
// where GTest looks for it.

#include <ostream>

#include "renderer/renderer_abi.h"

inline auto operator<<(std::ostream& out, const KuraRendererStatus status) -> std::ostream& {
  switch (status) {
    case KURA_RENDERER_OK:
      return out << "OK";
    case KURA_RENDERER_ERR_INVALID_ARG:
      return out << "ERR_INVALID_ARG";
    case KURA_RENDERER_ERR_NOT_IN_FRAME:
      return out << "ERR_NOT_IN_FRAME";
    case KURA_RENDERER_ERR_IN_FRAME:
      return out << "ERR_IN_FRAME";
    case KURA_RENDERER_ERR_CLIP_UNDERFLOW:
      return out << "ERR_CLIP_UNDERFLOW";
    case KURA_RENDERER_ERR_CLIP_UNBALANCED:
      return out << "ERR_CLIP_UNBALANCED";
    case KURA_RENDERER_ERR_OUT_OF_RESOURCES:
      return out << "ERR_OUT_OF_RESOURCES";
  }
  return out << "status(" << static_cast<int>(status) << ")";
}

#endif  // KURA_TESTS_ABI_PRINT_H
