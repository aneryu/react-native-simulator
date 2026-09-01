#include <react-native-simulator/Interaction.h>
#include <react-native-simulator/SceneTransform.h>

#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReactNativeSimulator {
namespace {

struct Point {
  float x{0};
  float y{0};
};

bool contains(
    const Point& origin,
    float width,
    float height,
    float x,
    float y) {
  return width > 0 && height > 0 && x >= origin.x && y >= origin.y &&
      x <= origin.x + width && y <= origin.y + height;
}

bool underModalHost(
    const SceneNode& node,
    const std::unordered_map<int, const SceneNode*>& byTag) {
  const SceneNode* current = &node;
  std::unordered_set<int> visited;
  while (true) {
    if (!visited.emplace(current->tag).second) {
      return false;
    }
    if (current->modalHost) {
      return true;
    }
    if (!current->parentTag) {
      return false;
    }
    const auto parent = byTag.find(*current->parentTag);
    if (parent == byTag.end()) {
      return false;
    }
    current = parent->second;
  }
}

std::optional<Point> presentationOrigin(
    const SceneNode& node,
    const std::unordered_map<int, const SceneNode*>& byTag,
    int rootTag) {
  Point origin{node.absoluteX, node.absoluteY};
  const SceneNode* current = &node;
  std::unordered_set<int> visited;
  const bool modalWindow = underModalHost(node, byTag);
  while (current->parentTag) {
    if (!visited.emplace(current->tag).second) {
      return std::nullopt;
    }
    if (current->modalHost) {
      break;
    }
    const auto parent = byTag.find(*current->parentTag);
    if (parent == byTag.end()) {
      return std::nullopt;
    }
    if (parent->second->scrollable) {
      origin.x -= parent->second->scrollOffsetX;
      origin.y -= parent->second->scrollOffsetY;
    }
    current = parent->second;
    if (current->modalHost) {
      break;
    }
  }
  if (modalWindow) {
    return origin;
  }
  return current->tag == rootTag ? std::optional<Point>(origin)
                                 : std::nullopt;
}

bool inverseMapNodePoint(
    const SceneNode& node,
    const std::unordered_map<int, const SceneNode*>& byTag,
    int rootTag,
    float& x,
    float& y) {
  if (!node.hasTransform) {
    return true;
  }
  const auto origin = presentationOrigin(node, byTag, rootTag);
  if (!origin) {
    return false;
  }
  const auto inverse = nodePivotTransform(node, origin->x, origin->y).inverted();
  if (!inverse) {
    return false;
  }
  inverse->map(x, y, x, y);
  return true;
}

} // namespace

std::optional<SceneHitTarget> hitTestScene(
    const SceneSnapshot& scene,
    float x,
    float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || scene.nodes.empty()) {
    return std::nullopt;
  }
  std::unordered_map<int, const SceneNode*> byTag;
  byTag.reserve(scene.nodes.size());
  for (const auto& node : scene.nodes) {
    if (!byTag.emplace(node.tag, &node).second) {
      return std::nullopt;
    }
  }
  const auto root = byTag.find(scene.rootTag);
  if (root == byTag.end() || root->second->parentTag ||
      !contains(
          Point{root->second->absoluteX, root->second->absoluteY},
          root->second->width,
          root->second->height,
          x,
          y)) {
    return std::nullopt;
  }
  std::unordered_map<int, std::vector<const SceneNode*>> children;
  for (const auto& node : scene.nodes) {
    if (node.parentTag) {
      children[*node.parentTag].push_back(&node);
    }
  }
  for (auto& [_, siblings] : children) {
    std::stable_sort(
        siblings.begin(), siblings.end(),
        [](const SceneNode* left, const SceneNode* right) {
          const auto leftZ = left->zIndex.value_or(0);
          const auto rightZ = right->zIndex.value_or(0);
          return leftZ == rightZ ? left->childIndex < right->childIndex
                                 : leftZ < rightZ;
        });
  }
  std::vector<const SceneNode*> modalHosts;
  for (const auto& node : scene.nodes) {
    if (node.modalHost && node.layoutable && node.display != "none") {
      modalHosts.push_back(&node);
    }
  }

  const auto visit = [&](const auto& self,
                         const SceneNode& node,
                         float hitX,
                         float hitY) -> std::optional<SceneHitTarget> {
    if (!node.layoutable || node.display == "none" ||
        node.pointerEvents == "none") {
      return std::nullopt;
    }
    if (!inverseMapNodePoint(node, byTag, scene.rootTag, hitX, hitY)) {
      return std::nullopt;
    }
    const auto origin = presentationOrigin(node, byTag, scene.rootTag);
    if (!origin) {
      return std::nullopt;
    }
    const bool clipsChildren =
        node.scrollable || node.clipsContentToBounds;
    const bool insideBounds = contains(
        *origin, node.width, node.height, hitX, hitY);
    const bool childrenCanBeTargets =
        node.pointerEvents == "auto" || node.pointerEvents == "box-none";
    if (childrenCanBeTargets && (!clipsChildren || insideBounds)) {
      const auto foundChildren = children.find(node.tag);
      if (foundChildren != children.end()) {
        for (auto iterator = foundChildren->second.rbegin();
             iterator != foundChildren->second.rend(); ++iterator) {
          if (auto result = self(self, **iterator, hitX, hitY)) {
            return result;
          }
        }
      }
    }
    const bool nodeCanBeTarget =
        node.pointerEvents == "auto" || node.pointerEvents == "box-only";
    const Point hitOrigin{
        origin->x - node.hitSlopLeft,
        origin->y - node.hitSlopTop};
    if (nodeCanBeTarget && contains(
            hitOrigin,
            node.width + node.hitSlopLeft + node.hitSlopRight,
            node.height + node.hitSlopTop + node.hitSlopBottom,
            hitX,
            hitY)) {
      return SceneHitTarget{
          .tag = node.tag,
          .localX = hitX - origin->x,
          .localY = hitY - origin->y};
    }
    return std::nullopt;
  };
  for (auto iterator = modalHosts.rbegin(); iterator != modalHosts.rend();
       ++iterator) {
    if (auto result = visit(visit, **iterator, x, y)) {
      return result;
    }
  }
  return visit(visit, *root->second, x, y);
}

} // namespace ReactNativeSimulator
