#ifndef KURA_COLOR_H
#define KURA_COLOR_H

#include "heap/color_model.h"
#include "heap/color_storage.h"
#include "object.h"
#include "type_id.h"

namespace kura {
template <const TypeId T>
class TemplateColor : public TemplateObject<T> {
 private:
  ColorStorage storage_;

 public:
  TemplateColor(const ColorStorage storage) :
    storage_(std::move(storage)) {}
  ~TemplateColor() override = default;

  auto GetStorage() -> ColorStorage& {
    return storage_;
  }

  auto GetStorage() const -> const ColorStorage& {
    return storage_;
  }

  auto GetC0() const -> float {
    return storage_.c0;
  }

  auto GetC1() const -> float {
    return storage_.c1;
  }

  auto GetC2() const -> float {
    return storage_.c2;
  }

  inline auto GetAlpha() const -> float {
    return storage_.a;
  }

  inline auto IsTransparent() const -> bool {
    return GetAlpha() <= 0.0f;
  }
};

#define DEFINE_COLOR_TYPE(Name)                                         \
  class Name : public TemplateColor<BuiltinTypes::k##Name##Type> {      \
   public:                                                              \
    Name(const ColorStorage storage = {}) :                             \
      TemplateColor<BuiltinTypes::k##Name##Type>(std::move(storage)) {} \
    ~Name() override = default;                                         \
    DECLARE_TYPE(Name);                                                 \
                                                                        \
   public:                                                              \
    static inline auto New(const ColorStorage rhs) -> Name* {           \
      return new Name(std::move(rhs));                                  \
    }                                                                   \
  };

FOR_EACH_COLOR_MODEL(DEFINE_COLOR_TYPE)
#undef DEFINE_COLOR_TYPE
}  // namespace kura

#endif  // KURA_COLOR_H
