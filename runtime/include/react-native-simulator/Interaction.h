#pragma once

#include <optional>
#include <cstdint>
#include <string>

#include <react-native-simulator/Scene.h>

namespace ReactNativeSimulator {

struct SceneHitTarget {
  int tag{0};
  float localX{0};
  float localY{0};
};

enum class InteractionActionType {
  PointerDown,
  PointerMove,
  PointerUp,
  PointerCancel,
  Scroll,
  TextInput,
  KeyDown,
  HardwareBackPress,
};

struct InteractionAction {
  InteractionActionType type{InteractionActionType::PointerMove};
  float x{0};
  float y{0};
  float deltaX{0};
  float deltaY{0};
  int pointerId{1};
  int button{0};
  int buttons{0};
  bool ctrlKey{false};
  bool shiftKey{false};
  bool altKey{false};
  bool metaKey{false};
  std::string text;
  std::string key;
};

struct InteractionResult {
  std::uint64_t sequence{0};
  std::optional<int> targetTag;
  std::int64_t sceneRevision{0};
  std::string error;

  explicit operator bool() const noexcept {
    return error.empty();
  }
};

// Returns the front-most mounted node containing a logical-point coordinate.
// The calculation matches retained rendering: later mounted nodes are on top,
// ScrollView ancestors clip descendants, and scroll offsets affect only their
// descendants. Invalid or disconnected ancestry is never hit.
std::optional<SceneHitTarget> hitTestScene(
    const SceneSnapshot& scene,
    float x,
    float y);

} // namespace ReactNativeSimulator
