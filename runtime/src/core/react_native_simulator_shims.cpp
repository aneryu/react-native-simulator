#include <react/renderer/mounting/internal/CullingContext.h>

// Platform shims owned by react-native-simulator.

namespace facebook::react {

bool CullingContext::shouldConsiderCulling() const {
  return false;
}

CullingContext CullingContext::adjustCullingContextIfNeeded(
    const ShadowViewNodePair&) const {
  return *this;
}

} // namespace facebook::react
