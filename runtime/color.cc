#include "runtime/color.h"

#include <sstream>

namespace kura {
void RgbaColor::Init() {}

auto RgbaColor::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "RgbaColor(";
  ss << "c0=" << GetC0() << ", ";
  ss << "c1=" << GetC1() << ", ";
  ss << "c2=" << GetC2() << ", ";
  ss << "alpha=" << GetAlpha();
  ss << ")";
  return ss.str();
}

void HslaColor::Init() {}

auto HslaColor::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "HslaColor(";
  ss << "c0=" << GetC0() << ", ";
  ss << "c1=" << GetC1() << ", ";
  ss << "c2=" << GetC2() << ", ";
  ss << "alpha=" << GetAlpha();
  ss << ")";
  return ss.str();
}

void HsbaColor::Init() {}

auto HsbaColor::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "HsbaColor(";
  ss << "c0=" << GetC0() << ", ";
  ss << "c1=" << GetC1() << ", ";
  ss << "c2=" << GetC2() << ", ";
  ss << "alpha=" << GetAlpha();
  ss << ")";
  return ss.str();
}

void SrgbaColor::Init() {}

auto SrgbaColor::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "SrgbaColor(";
  ss << "c0=" << GetC0() << ", ";
  ss << "c1=" << GetC1() << ", ";
  ss << "c2=" << GetC2() << ", ";
  ss << "alpha=" << GetAlpha();
  ss << ")";
  return ss.str();
}
}  // namespace kura
