#ifndef KURA_COMMON_H
#define KURA_COMMON_H

#define DEFINE_NON_COPYABLE_TYPE(Name)             \
 public:                                           \
  Name(const Name& rhs) = delete;                  \
  Name(Name&& rhs) = delete;                       \
  auto operator=(const Name& rhs)->Name& = delete; \
  auto operator=(Name&& rhs)->Name& = delete;

#define DEFINE_DEFAULT_COPYABLE_TYPE(Name)          \
 public:                                            \
  Name(const Name& rhs) = default;                  \
  Name(Name&& rhs) = default;                       \
  auto operator=(const Name& rhs)->Name& = default; \
  auto operator=(Name&& rhs)->Name& = default;

#endif  // KURA_COMMON_H
