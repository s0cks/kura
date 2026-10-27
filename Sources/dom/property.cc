#include "dom/property.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "object.h"

namespace kura::dom {
namespace {
constexpr auto kInfinity = std::numeric_limits<double>::infinity();

constexpr KindMask kElements = (KindMask{1} << static_cast<unsigned>(Kind::kDynamic)) - 1;
constexpr KindMask kBoxes = kElements & ~MaskOf(Kind::kFragment);
constexpr KindMask kContainers =
    MaskOf(Kind::kBlock) | MaskOf(Kind::kSurface) | MaskOf(Kind::kList) | MaskOf(Kind::kScroll) | MaskOf(Kind::kButton);
constexpr KindMask kFields = MaskOf(Kind::kText) | MaskOf(Kind::kInput);

constexpr TypeMask kBool = TypeBit(CompileType::kBool);
constexpr TypeMask kString = TypeBit(CompileType::kString);
constexpr TypeMask kColor = TypeBit(CompileType::kColor);
constexpr TypeMask kPx = TypeBit(CompileType::kPx);
constexpr TypeMask kLengths = TypeBit(CompileType::kPx) | TypeBit(CompileType::kPct);
constexpr TypeMask kSizes = kLengths | TypeBit(CompileType::kFr);
constexpr TypeMask kNumbers = TypeBit(CompileType::kInt) | TypeBit(CompileType::kFloat) | TypeBit(CompileType::kNumber);
constexpr TypeMask kMessages =
    TypeBit(CompileType::kVariant) | TypeBit(CompileType::kRecord) | TypeBit(CompileType::kFunction);

constexpr std::string_view kAuto[] = {"auto"};
constexpr std::string_view kDirections[] = {"row", "column", "row-reverse", "column-reverse"};
constexpr std::string_view kAligns[] = {"start", "center", "end", "stretch"};
constexpr std::string_view kJustifies[] = {"start", "center", "end", "space-between", "space-around", "space-evenly"};
constexpr std::string_view kTextAligns[] = {"left", "center", "right"};
constexpr std::string_view kFits[] = {"contain", "cover", "fill", "none"};

auto BuildSchema() -> std::vector<PropertyDef> {
  std::vector<PropertyDef> defs{};
  const auto add = [&](const std::string_view name, const KindMask kinds, const TypeMask accepts,
                       const std::span<const std::string_view> words = {}, const double min = -kInfinity,
                       const double max = kInfinity, const bool event = false) {
    PropertyDef def{};
    def.index = static_cast<uint16_t>(defs.size());
    def.name = name;
    def.kinds = kinds;
    def.accepts = accepts;
    def.words = words;
    def.min = min;
    def.max = max;
    def.event = event;
    defs.push_back(def);
  };

  add("width", kBoxes, kSizes | kString, kAuto, 0);
  add("height", kBoxes, kSizes | kString, kAuto, 0);
  add("min-width", kBoxes, kLengths, {}, 0);
  add("min-height", kBoxes, kLengths, {}, 0);
  add("max-width", kBoxes, kLengths, {}, 0);
  add("max-height", kBoxes, kLengths, {}, 0);
  add("padding", kBoxes, kLengths, {}, 0);
  add("margin", kBoxes, kLengths);
  add("gap", kContainers, kLengths, {}, 0);
  add("direction", kContainers, kString, kDirections);
  add("align", kContainers, kString, kAligns);
  add("justify", kContainers, kString, kJustifies);
  add("wrap", kContainers, kBool);
  add("grow", kBoxes, kNumbers, {}, 0);
  add("shrink", kBoxes, kNumbers, {}, 0);

  add("fill", kBoxes, kColor);
  add("border-color", kBoxes, kColor);
  add("border-width", kBoxes, kPx, {}, 0);
  add("radius", kBoxes, kPx, {}, 0);
  add("opacity", kBoxes, kNumbers, {}, 0, 1);

  add("value", MaskOf(Kind::kText), kString);
  add("value", MaskOf(Kind::kInput), kString);
  add("value", MaskOf(Kind::kProgress), kNumbers, {}, 0, 1);
  add("color", kFields, kColor);
  add("size", kFields, kPx, {}, 0);
  add("text-align", kFields, kString, kTextAligns);
  add("placeholder", MaskOf(Kind::kInput), kString);

  add("source", MaskOf(Kind::kImage), kString);
  add("fit", MaskOf(Kind::kImage), kString, kFits);

  add("on-click", kBoxes, kMessages, {}, -kInfinity, kInfinity, true);
  add("on-input", MaskOf(Kind::kInput), kMessages, {}, -kInfinity, kInfinity, true);
  add("on-scroll", MaskOf(Kind::kScroll) | MaskOf(Kind::kList), kMessages, {}, -kInfinity, kInfinity, true);
  return defs;
}

auto Schema() -> const std::vector<PropertyDef>& {
  static const std::vector<PropertyDef> kSchema = BuildSchema();
  return kSchema;
}

auto TypeName(const CompileType::Kind kind) -> std::string_view {
  switch (kind) {
    case CompileType::kBool:
      return "Bool";
    case CompileType::kInt:
      return "Int";
    case CompileType::kFloat:
      return "Float";
    case CompileType::kNumber:
      return "Number";
    case CompileType::kString:
      return "String";
    case CompileType::kColor:
      return "Color";
    case CompileType::kPx:
      return "Px";
    case CompileType::kPct:
      return "Pct";
    case CompileType::kFr:
      return "Fr";
    default:
      return "?";
  }
}

auto Join(const std::vector<std::string>& items) -> std::string {
  std::string out{};
  for (size_t idx = 0; idx < items.size(); idx++) {
    if (idx > 0)
      out += idx + 1 == items.size() ? " or " : ", ";
    out += items[idx];
  }
  return out;
}

auto Quote(const std::string_view word) -> std::string {
  return "\"" + std::string(word) + "\"";
}

auto FormatNumber(const double value) -> std::string {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", value);
  return buffer;
}

auto Mismatch(const PropertyDef& def, const std::string& got) -> std::string {
  return "expected " + DescribeAccepted(def) + ", got " + got;
}

auto ConstantKind(Object* value) -> std::optional<CompileType::Kind> {
  switch (value->GetType()) {
    case kBoolType:
      return CompileType::kBool;
    case kNoneType:
      return CompileType::kNone;
    case kNumberType:
      return CompileType::kNumber;
    case kIntType:
      return CompileType::kInt;
    case kFloatType:
      return CompileType::kFloat;
    case kStringType:
      return CompileType::kString;
    case kColorType:
      return CompileType::kColor;
    case kPxType:
      return CompileType::kPx;
    case kPctType:
      return CompileType::kPct;
    case kFrType:
      return CompileType::kFr;
    default:
      return std::nullopt;
  }
}

auto NumberOf(Object* value, double* out) -> bool {
  if (const auto v = dynamic_cast<Int*>(value)) {
    *out = static_cast<double>(v->GetValue());
  } else if (const auto v = dynamic_cast<Float*>(value)) {
    *out = v->GetValue();
  } else if (const auto v = dynamic_cast<Number*>(value)) {
    *out = v->GetValue();
  } else if (const auto v = dynamic_cast<Px*>(value)) {
    *out = v->GetValue();
  } else if (const auto v = dynamic_cast<Pct*>(value)) {
    *out = v->GetValue();
  } else if (const auto v = dynamic_cast<Fr*>(value)) {
    *out = v->GetValue();
  } else {
    return false;
  }

  return true;
}

auto Distance(const std::string_view a, const std::string_view b) -> size_t {
  std::vector<size_t> row(b.size() + 1);
  for (size_t j = 0; j <= b.size(); j++)
    row[j] = j;

  for (size_t i = 1; i <= a.size(); i++) {
    size_t diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); j++) {
      const auto above = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
      diagonal = above;
    }
  }

  return row[b.size()];
}
}  // namespace

auto AllProperties() -> std::span<const PropertyDef> {
  return Schema();
}

auto FindProperty(const Kind kind, const std::string_view name) -> const PropertyDef* {
  for (const auto& def : Schema()) {
    if (def.name == name && def.AppliesTo(kind))
      return &def;
  }
  return nullptr;
}

auto PropertiesOf(const Kind kind) -> std::vector<const PropertyDef*> {
  std::vector<const PropertyDef*> defs{};
  for (const auto& def : Schema()) {
    if (def.AppliesTo(kind))
      defs.push_back(&def);
  }
  return defs;
}

auto SuggestProperty(const Kind kind, const std::string_view name) -> const PropertyDef* {
  const PropertyDef* best = nullptr;
  size_t best_distance = 3;
  for (const auto& def : Schema()) {
    if (!def.AppliesTo(kind))
      continue;

    const auto distance = Distance(name, def.name);
    if (distance < best_distance) {
      best = &def;
      best_distance = distance;
    }
  }

  return best;
}

auto DescribeAccepted(const PropertyDef& def) -> std::string {
  std::vector<std::string> items{};
  constexpr CompileType::Kind kScalars[] = {CompileType::kBool,   CompileType::kInt,    CompileType::kFloat,
                                            CompileType::kNumber, CompileType::kString, CompileType::kColor,
                                            CompileType::kPx,     CompileType::kPct,    CompileType::kFr};
  for (const auto kind : kScalars) {
    if (!def.Accepts(kind))
      continue;

    if (kind == CompileType::kString && !def.words.empty())
      continue;

    items.emplace_back(TypeName(kind));
  }

  if (def.Accepts(CompileType::kString)) {
    for (const auto word : def.words)
      items.push_back(Quote(word));
  }

  if ((def.accepts & kMessages) != 0)
    items.emplace_back("a Message");

  return Join(items);
}

auto CheckConstant(const PropertyDef& def, Object* value) -> std::optional<std::string> {
  if (!value)
    return "missing value";

  const auto kind = ConstantKind(value);
  if (!kind || !def.Accepts(*kind))
    return Mismatch(def, value->ToString());

  if (*kind == CompileType::kString && !def.words.empty()) {
    const auto& text = dynamic_cast<String*>(value)->GetValue();
    if (std::find(def.words.begin(), def.words.end(), std::string_view(text)) == def.words.end())
      return Quote(text) + " isn't one of " + DescribeAccepted(def);
  }

  double number = 0;
  if (NumberOf(value, &number)) {
    if (!std::isfinite(number))
      return "must be a finite number";

    if (number < def.min || number > def.max) {
      if (def.min == 0 && def.max == kInfinity)
        return "can't be negative, got " + FormatNumber(number);

      if (def.min > -kInfinity && def.max < kInfinity)
        return "must be between " + FormatNumber(def.min) + " and " + FormatNumber(def.max) + ", got " +
               FormatNumber(number);

      if (def.min > -kInfinity)
        return "must be at least " + FormatNumber(def.min) + ", got " + FormatNumber(number);

      return "must be at most " + FormatNumber(def.max) + ", got " + FormatNumber(number);
    }
  }

  return std::nullopt;
}

auto CheckType(const PropertyDef& def, CompileType* type) -> std::optional<std::string> {
  if (!type || type->IsAny() || type->IsNever())
    return std::nullopt;

  for (const auto alternative : type->GetAlternatives()) {
    if (alternative->IsAny() || alternative->IsNever())
      continue;

    if (!def.Accepts(alternative->GetKind()))
      return Mismatch(def, type->ToString());
  }

  return std::nullopt;
}
}  // namespace kura::dom
