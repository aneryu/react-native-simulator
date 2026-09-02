#include "InteractiveFrontend.h"

#include "HostUi.h"
#include "MacWindowChrome.h"
#include "SimulatorImGuiTheme.h"
#include "SkiaMountedTreeRenderer.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReactNativeSimulator {
namespace {

struct FrontendState {
  std::mutex mutex;
  std::shared_ptr<const SceneSnapshot> scene;
  std::optional<InteractionResult> action;
  std::optional<EngineResult> result;
  std::atomic<bool> cancelRequested{false};
  std::atomic<bool> runtimeFinished{false};
};

// Interact is always the canvas input path. Select is an inspector overlay
// drawn on top of the live Skia frame; it does not replace pointer events.

struct CanvasMapping {
  bool hovered{false};
  ImVec2 topLeft{};
  ImVec2 size{};
  float logicalWidth{0};
  float logicalHeight{0};
  ImVec2 navMin{};
  ImVec2 navMax{};
  bool navVisible{false};

  std::optional<std::pair<float, float>> map(
      float x,
      float y,
      bool requireHover = true) const {
    if (size.x <= 0 || size.y <= 0 || logicalWidth <= 0 ||
        logicalHeight <= 0) {
      return std::nullopt;
    }
    if (requireHover && !hovered) {
      return std::nullopt;
    }
    return std::pair{
        std::clamp((x - topLeft.x) / size.x, 0.0f, 1.0f) * logicalWidth,
        std::clamp((y - topLeft.y) / size.y, 0.0f, 1.0f) * logicalHeight};
  }

  // 0 none, 1 back, 2 home, 3 recents
  int navButtonAt(float x, float y) const {
    if (!navVisible || x < navMin.x || x >= navMax.x || y < navMin.y ||
        y >= navMax.y) {
      return 0;
    }
    const float width = navMax.x - navMin.x;
    if (width <= 0.0f) {
      return 0;
    }
    const float t = (x - navMin.x) / width;
    if (t < 1.0f / 3.0f) {
      return 1;
    }
    if (t < 2.0f / 3.0f) {
      return 2;
    }
    return 3;
  }
};

void enqueueIgnoringStopped(Engine& engine, InteractionAction action) {
  try {
    engine.enqueueAction(std::move(action));
  } catch (const std::logic_error&) {
  }
}

void appendDiagnostic(
    std::string& log,
    std::string& lastLogged,
    const char* heading,
    const std::string& text) {
  if (text.empty() || text == lastLogged) {
    return;
  }
  lastLogged = text;
  if (!log.empty()) {
    log.append("\n\n");
  }
  log.append(heading);
  log.append(":\n");
  log.append(text);
}

const SceneNode* findNodeByTag(const std::vector<SceneNode>& nodes, int tag) {
  for (const auto& node : nodes) {
    if (node.tag == tag) {
      return &node;
    }
  }
  return nullptr;
}

bool sceneHasAnimatingIndicator(const SceneSnapshot& scene) {
  if (!scene.toastMessage.empty() && scene.toastUntilMs != 0) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    if (now < scene.toastUntilMs + 50) {
      return true;
    }
  }
  for (const auto& node : scene.nodes) {
    if (node.activityIndicator && node.activityIndicatorAnimating) {
      return true;
    }
  }
  return false;
}

std::optional<ImVec2> nodePresentationOrigin(
    const SceneNode& node,
    const std::vector<SceneNode>& nodes,
    int rootTag) {
  std::unordered_map<int, const SceneNode*> byTag;
  byTag.reserve(nodes.size());
  for (const auto& candidate : nodes) {
    byTag.emplace(candidate.tag, &candidate);
  }
  ImVec2 origin{node.absoluteX, node.absoluteY};
  const SceneNode* current = &node;
  std::unordered_set<int> visited;
  while (current->parentTag) {
    if (!visited.emplace(current->tag).second) {
      return std::nullopt;
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
  }
  if (current->tag != rootTag) {
    return std::nullopt;
  }
  return origin;
}

void drawNodeHighlight(
    const CanvasMapping& mapping,
    const SceneSnapshot& scene,
    int tag,
    ImU32 fill,
    ImU32 stroke,
    float thickness,
    bool drawLabel) {
  if (tag == 0 || mapping.size.x <= 0 || mapping.logicalWidth <= 0 ||
      mapping.logicalHeight <= 0) {
    return;
  }
  const std::vector<SceneNode>* nodes = &scene.nodes;
  int rootTag = scene.rootTag;
  const SceneNode* node = findNodeByTag(scene.nodes, tag);
  if (node == nullptr) {
    node = findNodeByTag(scene.shadowNodes, tag);
    nodes = &scene.shadowNodes;
    rootTag = scene.shadowRootTag;
  }
  if (node == nullptr || node->width <= 0 || node->height <= 0) {
    return;
  }
  auto origin = nodePresentationOrigin(*node, *nodes, rootTag);
  if (!origin) {
    origin = ImVec2{node->absoluteX, node->absoluteY};
  }
  const float scaleX = mapping.size.x / mapping.logicalWidth;
  const float scaleY = mapping.size.y / mapping.logicalHeight;
  const ImVec2 topLeft{
      mapping.topLeft.x + origin->x * scaleX,
      mapping.topLeft.y + origin->y * scaleY};
  const ImVec2 bottomRight{
      topLeft.x + node->width * scaleX,
      topLeft.y + node->height * scaleY};
  auto* drawList = ImGui::GetWindowDrawList();
  drawList->PushClipRect(
      mapping.topLeft,
      {mapping.topLeft.x + mapping.size.x, mapping.topLeft.y + mapping.size.y},
      true);
  drawList->AddRectFilled(topLeft, bottomRight, fill);
  drawList->AddRect(topLeft, bottomRight, stroke, 0.0f, 0, thickness);
  if (drawLabel) {
    char label[160];
    std::snprintf(
        label,
        sizeof(label),
        "%s #%d",
        node->componentName.empty() ? "Unknown" : node->componentName.c_str(),
        node->tag);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos{topLeft.x + 4.0f, topLeft.y + 4.0f};
    if (textPos.y + textSize.y + 6.0f > bottomRight.y &&
        topLeft.y - textSize.y - 8.0f >= mapping.topLeft.y) {
      textPos.y = topLeft.y - textSize.y - 6.0f;
    }
    drawList->AddRectFilled(
        textPos,
        {textPos.x + textSize.x + 8.0f, textPos.y + textSize.y + 4.0f},
        imgui_theme::wash(imgui_theme::palette().bg, 236),
        4.0f);
    drawList->AddText(
        {textPos.x + 4.0f, textPos.y + 2.0f},
        imgui_theme::vecToU32(imgui_theme::palette().inspect),
        label);
  }
  drawList->PopClipRect();
}

void drawStatusBarChrome(ImDrawList* drawList, ImVec2 min, ImVec2 max, float dp) {
  drawList->AddRectFilled(min, max, IM_COL32(247, 247, 247, 255));
  const float width = max.x - min.x;
  const float height = max.y - min.y;
  const float holeR = std::max(3.5f, 5.5f * dp);
  drawList->AddCircleFilled(
      {min.x + width * 0.5f, min.y + holeR + 3.0f * dp},
      holeR,
      IM_COL32(10, 10, 12, 255));
  char clock[16] = "9:41";
  std::time_t now = std::time(nullptr);
  std::tm local{};
  if (localtime_r(&now, &local) != nullptr) {
    std::snprintf(clock, sizeof(clock), "%d:%02d", local.tm_hour, local.tm_min);
  }
  const float iconY = min.y + height * 0.58f;
  const ImVec2 clockSize = ImGui::CalcTextSize(clock);
  drawList->AddText(
      {min.x + 14.0f * dp, iconY - clockSize.y * 0.5f},
      IM_COL32(32, 32, 36, 255),
      clock);
  const float icon = std::max(6.0f, 8.0f * dp);
  float iconX = max.x - 14.0f * dp;
  drawList->AddRect(
      {iconX - icon * 1.7f, iconY - icon * 0.45f},
      {iconX, iconY + icon * 0.45f},
      IM_COL32(32, 32, 36, 255),
      1.5f,
      0,
      1.2f);
  drawList->AddRectFilled(
      {iconX - icon * 1.35f, iconY - icon * 0.22f},
      {iconX - icon * 0.22f, iconY + icon * 0.22f},
      IM_COL32(32, 32, 36, 255),
      0.8f);
  iconX -= icon * 2.4f;
  drawList->AddCircleFilled({iconX, iconY}, icon * 0.18f, IM_COL32(32, 32, 36, 255));
  drawList->AddCircle(
      {iconX, iconY}, icon * 0.42f, IM_COL32(32, 32, 36, 255), 0, 1.2f);
  iconX -= icon * 1.6f;
  drawList->AddTriangleFilled(
      {iconX, iconY - icon * 0.45f},
      {iconX - icon * 0.38f, iconY + icon * 0.35f},
      {iconX + icon * 0.38f, iconY + icon * 0.35f},
      IM_COL32(32, 32, 36, 255));
}

void drawNavBarChrome(
    ImDrawList* drawList,
    ImVec2 min,
    ImVec2 max,
    float dp,
    Engine* engine,
    bool enableInput) {
  drawList->AddRectFilled(min, max, IM_COL32(254, 254, 254, 255));
  const float width = max.x - min.x;
  const float height = std::max(1.0f, max.y - min.y);
  const float third = width / 3.0f;
  const float cy = (min.y + max.y) * 0.5f;
  const float icon = std::max(7.0f, 11.0f * dp);
  const ImU32 color = IM_COL32(95, 99, 104, 255);
  const float x0 = min.x + width * 0.25f;
  const float x1 = min.x + width * 0.5f;
  const float x2 = min.x + width * 0.75f;
  const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(min);
  ImGui::InvisibleButton("nav-back", {third, height});
  const bool backHovered = ImGui::IsItemHovered();
  const bool backPressed = ImGui::IsItemActive();
  if (enableInput && engine != nullptr && ImGui::IsItemClicked()) {
    enqueueIgnoringStopped(*engine, {
        .type = InteractionActionType::HardwareBackPress});
  }
  ImGui::SetCursorScreenPos({min.x + third, min.y});
  ImGui::InvisibleButton("nav-home", {third, height});
  const bool homeHovered = ImGui::IsItemHovered();
  ImGui::SetCursorScreenPos({min.x + 2.0f * third, min.y});
  ImGui::InvisibleButton("nav-recents", {third, height});
  const bool recentsHovered = ImGui::IsItemHovered();
  ImGui::SetCursorScreenPos(savedCursor);
  const auto wash = [&](float cx, bool hovered, bool pressed) {
    if (!hovered) {
      return;
    }
    drawList->AddCircleFilled(
        {cx, cy},
        icon * 1.35f,
        pressed ? IM_COL32(95, 99, 104, 36) : IM_COL32(95, 99, 104, 22));
  };
  wash(x0 + icon * 0.45f, backHovered, backPressed);
  wash(x1, homeHovered, false);
  wash(x2, recentsHovered, false);
  if (backPressed) {
    drawList->AddTriangleFilled(
        {x0 + icon * 0.15f, cy},
        {x0 + icon, cy - icon * 0.55f},
        {x0 + icon, cy + icon * 0.55f},
        color);
  } else {
    drawList->AddTriangle(
        {x0 + icon * 0.15f, cy},
        {x0 + icon, cy - icon * 0.55f},
        {x0 + icon, cy + icon * 0.55f},
        color,
        1.6f);
  }
  drawList->AddCircle({x1, cy}, icon * 0.55f, color, 0, 1.6f);
  drawList->AddRect(
      {x2 - icon * 0.5f, cy - icon * 0.5f},
      {x2 + icon * 0.5f, cy + icon * 0.5f},
      color,
      1.2f,
      0,
      1.6f);
}

void drawDeviceCanvas(
    SDL_Texture* texture,
    const SceneSnapshot* scene,
    CanvasMapping& mapping,
    int selectedTag,
    int& hoveredTag,
    bool selectOverlay,
    Engine& engine,
    bool enableInput) {
  mapping = {};
  constexpr float kBezel = 12.0f;
  char subtitle[96] = {};
  const char* subtitlePtr = nullptr;
  const float insetTop = scene != nullptr ? std::max(0.0f, scene->insetTop) : 0.0f;
  const float insetBottom =
      scene != nullptr ? std::max(0.0f, scene->insetBottom) : 0.0f;
  if (scene != nullptr && scene->viewportWidth > 0 &&
      scene->viewportHeight > 0) {
    const float screenH = scene->viewportHeight + insetTop + insetBottom;
    std::snprintf(
        subtitle,
        sizeof(subtitle),
        "%.0f × %.0f  ·  status %.0fdp",
        scene->viewportWidth,
        screenH,
        insetTop);
    subtitlePtr = subtitle;
  } else if (selectOverlay) {
    subtitlePtr = "Select overlay  ·  Alt-click inspects";
  }
  imgui_theme::panelHeader("Device", subtitlePtr);
  const ImVec2 available = ImGui::GetContentRegionAvail();
  if (texture == nullptr || scene == nullptr || scene->viewportWidth <= 0 ||
      scene->viewportHeight <= 0 || available.x <= 1 || available.y <= 1) {
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    const ImVec2 wellSize{rest.x, std::max(80.0f, rest.y - 8.0f)};
    const ImVec2 wellMin = ImGui::GetCursorScreenPos();
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        wellMin,
        {wellMin.x + wellSize.x, wellMin.y + wellSize.y},
        imgui_theme::vecToU32(imgui_theme::palette().bg),
        16.0f);
    drawList->AddRect(
        wellMin,
        {wellMin.x + wellSize.x, wellMin.y + wellSize.y},
        imgui_theme::vecToU32(imgui_theme::palette().border),
        16.0f);
    ImGui::Dummy(wellSize);
    const char* waiting = "Select a page, then Run";
    const ImVec2 text = ImGui::CalcTextSize(waiting);
    drawList->AddText(
        {wellMin.x + (wellSize.x - text.x) * 0.5f,
         wellMin.y + (wellSize.y - text.y) * 0.5f},
        imgui_theme::vecToU32(imgui_theme::palette().muted),
        waiting);
    return;
  }
  const ImVec2 content = ImGui::GetContentRegionAvail();
  const float screenH = scene->viewportHeight + insetTop + insetBottom;
  const float maxWidth = std::max(32.0f, content.x - kBezel * 2.0f);
  const float maxHeight = std::max(32.0f, content.y - kBezel * 2.0f);
  const float scale = std::min(
      maxWidth / scene->viewportWidth, maxHeight / screenH);
  const ImVec2 size{
      scene->viewportWidth * scale, scene->viewportHeight * scale};
  const float statusH = insetTop * scale;
  const float navH = insetBottom * scale;
  const ImVec2 frameSize{
      size.x + kBezel * 2.0f, size.y + statusH + navH + kBezel * 2.0f};
  ImGui::SetCursorPosX(
      ImGui::GetCursorPosX() +
      std::max(0.0f, (content.x - frameSize.x) * 0.5f));
  ImGui::SetCursorPosY(
      ImGui::GetCursorPosY() +
      std::max(0.0f, (content.y - frameSize.y) * 0.5f));
  const ImVec2 frameMin = ImGui::GetCursorScreenPos();
  auto* drawList = ImGui::GetWindowDrawList();
  const ImVec2 frameMax{
      frameMin.x + frameSize.x, frameMin.y + frameSize.y};
  drawList->AddRectFilled(
      {frameMin.x - 4.0f, frameMin.y - 1.0f + 4.0f},
      {frameMax.x + 4.0f, frameMax.y + 8.0f},
      imgui_theme::u32(0, 0, 0, 72),
      18.0f);
  drawList->AddRectFilled(
      frameMin, frameMax, imgui_theme::vecToU32(imgui_theme::palette().elevated), 16.0f);
  drawList->AddRect(
      frameMin,
      frameMax,
      imgui_theme::vecToU32(imgui_theme::palette().hairlineStrong),
      16.0f,
      0,
      1.0f);
  const ImVec2 innerMin{frameMin.x + kBezel, frameMin.y + kBezel};
  if (statusH > 0.5f) {
    drawStatusBarChrome(
        drawList,
        innerMin,
        {innerMin.x + size.x, innerMin.y + statusH},
        scale);
  }
  ImGui::SetCursorScreenPos({innerMin.x, innerMin.y + statusH});
  ImGui::Image(reinterpret_cast<ImTextureID>(texture), size, {0, 0}, {1, 1});
  mapping.hovered = ImGui::IsItemHovered();
  mapping.topLeft = ImGui::GetItemRectMin();
  mapping.size = size;
  mapping.logicalWidth = scene->viewportWidth;
  mapping.logicalHeight = scene->viewportHeight;
  if (navH > 0.5f) {
    mapping.navMin = {innerMin.x, innerMin.y + statusH + size.y};
    mapping.navMax = {
        innerMin.x + size.x, innerMin.y + statusH + size.y + navH};
    mapping.navVisible = true;
    drawNavBarChrome(
        drawList,
        mapping.navMin,
        mapping.navMax,
        scale,
        &engine,
        enableInput);
  } else {
    mapping.navVisible = false;
  }
  if (selectOverlay && mapping.hovered) {
    const auto point = mapping.map(
        ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, false);
    if (point) {
      if (const auto hit =
              hitTestScene(*scene, point->first, point->second)) {
        hoveredTag = hit->tag;
      }
    }
  }
  drawList->AddRect(
      mapping.topLeft,
      {mapping.topLeft.x + size.x, mapping.topLeft.y + size.y},
      imgui_theme::wash(imgui_theme::palette().bg, 90),
      2.0f);
  if (selectOverlay && hoveredTag != 0 && hoveredTag != selectedTag) {
    drawNodeHighlight(
        mapping,
        *scene,
        hoveredTag,
        imgui_theme::wash(imgui_theme::palette().accent, 42),
        imgui_theme::vecToU32(imgui_theme::palette().accentHover),
        1.5f,
        false);
  }
  if (selectOverlay && selectedTag != 0) {
    drawNodeHighlight(
        mapping,
        *scene,
        selectedTag,
        imgui_theme::wash(imgui_theme::palette().inspect, 48),
        imgui_theme::vecToU32(imgui_theme::palette().inspect),
        2.0f,
        true);
  }
}

std::string shadowNodeLabel(const SceneNode& node) {
  char buffer[256];
  if (node.layoutable) {
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s #%d  [%.0f, %.0f, %.0f x %.0f]",
        node.componentName.empty() ? "Unknown" : node.componentName.c_str(),
        node.tag,
        node.x,
        node.y,
        node.width,
        node.height);
  } else {
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s #%d",
        node.componentName.empty() ? "Unknown" : node.componentName.c_str(),
        node.tag);
  }
  std::string label = buffer;
  if (!node.nativeId.empty()) {
    label.append("  ");
    label.append(node.nativeId);
  }
  if (!node.text.empty()) {
    label.append("  \"");
    if (node.text.size() > 24) {
      label.append(node.text, 0, 24);
      label.append("...");
    } else {
      label.append(node.text);
    }
    label.append("\"");
  }
  return label;
}

bool matchesFilter(const SceneNode& node, const char* filter) {
  if (filter == nullptr || filter[0] == '\0') {
    return true;
  }
  std::string needle = filter;
  for (char& character : needle) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  const auto contains = [&needle](std::string hay) {
    for (char& character : hay) {
      character = static_cast<char>(
          std::tolower(static_cast<unsigned char>(character)));
    }
    return hay.find(needle) != std::string::npos;
  };
  return contains(node.componentName) || contains(node.nativeId) ||
      contains(node.text) ||
      contains(std::to_string(node.tag));
}

bool subtreeMatches(
    const SceneNode& node,
    const std::unordered_map<int, std::vector<const SceneNode*>>& children,
    const char* filter) {
  if (matchesFilter(node, filter)) {
    return true;
  }
  const auto found = children.find(node.tag);
  if (found == children.end()) {
    return false;
  }
  for (const auto* child : found->second) {
    if (subtreeMatches(*child, children, filter)) {
      return true;
    }
  }
  return false;
}

void drawShadowNodeRow(
    const SceneNode& node,
    const std::unordered_map<int, std::vector<const SceneNode*>>& children,
    const std::unordered_set<int>& openPath,
    int& selectedTag,
    int& hoveredTag,
    bool& scrollToSelected,
    const char* filter) {
  if (!subtreeMatches(node, children, filter)) {
    return;
  }
  const auto found = children.find(node.tag);
  const bool hasChildren =
      found != children.end() && !found->second.empty();
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow |
      ImGuiTreeNodeFlags_SpanAvailWidth |
      ImGuiTreeNodeFlags_DrawLinesToNodes;
  // TextExample is ~600 nodes. Opening every row froze Interact. Keep the
  // root and the path to the selected node expanded; the rest stay closed.
  if (hasChildren &&
      (!node.parentTag || openPath.count(node.tag) != 0)) {
    ImGui::SetNextItemOpen(
        true, scrollToSelected ? ImGuiCond_Always : ImGuiCond_Once);
  }
  if (!hasChildren) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (selectedTag == node.tag) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  const auto label = shadowNodeLabel(node);
  const bool open = ImGui::TreeNodeEx(
      reinterpret_cast<void*>(static_cast<std::intptr_t>(node.tag)),
      flags,
      "%s",
      label.c_str());
  if (ImGui::IsItemHovered()) {
    hoveredTag = node.tag;
  }
  if (ImGui::IsItemClicked()) {
    selectedTag = node.tag;
  }
  if (selectedTag == node.tag && scrollToSelected) {
    ImGui::SetScrollHereY(0.25f);
    scrollToSelected = false;
  }
  if (hasChildren && open) {
    for (const auto* child : found->second) {
      drawShadowNodeRow(
          *child,
          children,
          openPath,
          selectedTag,
          hoveredTag,
          scrollToSelected,
          filter);
    }
    ImGui::TreePop();
  }
}

void drawSelectedShadowNode(
    const std::vector<SceneNode>& nodes,
    int selectedTag) {
  const auto selected = std::find_if(
      nodes.begin(), nodes.end(),
      [selectedTag](const SceneNode& node) { return node.tag == selectedTag; });
  if (selected == nodes.end()) {
    return;
  }
  ImGui::SeparatorText("Selected");
  imgui_theme::propertyRow("component", selected->componentName.c_str());
  imgui_theme::propertyRow("tag", std::to_string(selected->tag).c_str());
  if (selected->parentTag) {
    imgui_theme::propertyRow(
        "parent", std::to_string(*selected->parentTag).c_str());
  } else {
    imgui_theme::propertyRow("parent", "(root)");
  }
  if (!selected->nativeId.empty()) {
    imgui_theme::propertyRow("nativeId", selected->nativeId.c_str());
  }
  char frame[96];
  std::snprintf(
      frame,
      sizeof(frame),
      "%.1f, %.1f  %.1f × %.1f",
      selected->x,
      selected->y,
      selected->width,
      selected->height);
  imgui_theme::propertyRow("frame", frame);
  char absolute[64];
  std::snprintf(
      absolute,
      sizeof(absolute),
      "%.1f, %.1f",
      selected->absoluteX,
      selected->absoluteY);
  imgui_theme::propertyRow("absolute", absolute);
  char display[96];
  std::snprintf(
      display,
      sizeof(display),
      "%s  ·  %s",
      selected->display.c_str(),
      selected->position.c_str());
  imgui_theme::propertyRow("layout", display);
  imgui_theme::propertyRow("pointerEvents", selected->pointerEvents.c_str());
  if (!selected->text.empty()) {
    imgui_theme::propertyRow("text", selected->text.c_str());
  }
  if (selected->scrollable) {
    char scroll[96];
    std::snprintf(
        scroll,
        sizeof(scroll),
        "%.1f, %.1f  ·  %.1f × %.1f",
        selected->scrollOffsetX,
        selected->scrollOffsetY,
        selected->scrollContentWidth,
        selected->scrollContentHeight);
    imgui_theme::propertyRow("scroll", scroll);
  }
}

void drawShadowTree(
    const SceneSnapshot* scene,
    int& selectedTag,
    int& hoveredTag,
    bool& scrollToSelected,
    char* filter,
    std::size_t filterSize) {
  if (scene == nullptr || scene->shadowNodes.empty()) {
    imgui_theme::panelHeader("ShadowTree");
    imgui_theme::mutedHint("No React ShadowTree snapshot yet.");
    return;
  }
  char subtitle[64];
  std::snprintf(
      subtitle,
      sizeof(subtitle),
      "rev %lld  ·  %zu nodes",
      static_cast<long long>(scene->shadowRevision),
      scene->shadowNodes.size());
  imgui_theme::panelHeader("ShadowTree", subtitle);
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint(
      "##shadow-filter", "Filter name, tag, text", filter, filterSize);
  ImGui::Dummy({0.0f, 4.0f});
  bool selectedExists = false;
  for (const auto& node : scene->shadowNodes) {
    if (node.tag == selectedTag) {
      selectedExists = true;
      break;
    }
  }
  if (selectedTag == 0 || !selectedExists) {
    selectedTag = scene->shadowRootTag;
  }
  std::unordered_map<int, std::vector<const SceneNode*>> children;
  for (const auto& node : scene->shadowNodes) {
    if (node.parentTag) {
      children[*node.parentTag].push_back(&node);
    }
  }
  for (auto& [_, siblings] : children) {
    std::stable_sort(
        siblings.begin(), siblings.end(),
        [](const SceneNode* left, const SceneNode* right) {
          return left->childIndex < right->childIndex;
        });
  }
  const float detailsReserve = 220.0f;
  const float treeHeight = std::max(
      80.0f, ImGui::GetContentRegionAvail().y - detailsReserve);
  ImGui::PushStyleColor(
      ImGuiCol_ChildBg, imgui_theme::palette().panelAlt);
  std::unordered_set<int> openPath;
  if (selectedTag != 0) {
    int tag = selectedTag;
    while (openPath.emplace(tag).second) {
      const SceneNode* current = nullptr;
      for (const auto& node : scene->shadowNodes) {
        if (node.tag == tag) {
          current = &node;
          break;
        }
      }
      if (current == nullptr || !current->parentTag) {
        break;
      }
      tag = *current->parentTag;
    }
  }
  ImGui::BeginChild(
      "shadow-tree-hierarchy",
      ImVec2(0, treeHeight),
      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
  for (const auto& node : scene->shadowNodes) {
    if (!node.parentTag) {
      drawShadowNodeRow(
          node,
          children,
          openPath,
          selectedTag,
          hoveredTag,
          scrollToSelected,
          filter);
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::BeginChild("shadow-selected", ImVec2(0, 0));
  drawSelectedShadowNode(scene->shadowNodes, selectedTag);
  ImGui::EndChild();
}

struct PagesUi {
  std::string selectedKey;
  std::vector<char> json = std::vector<char>(32 * 1024, '\0');
  std::string localError;
  bool jsonSeeded{false};
};

std::vector<std::string> selectableAppKeys(
    const std::vector<std::string>& keys,
    const std::optional<std::string>& configured) {
  std::vector<std::string> result;
  for (const auto& key : keys) {
    if (key != "LogBox" || (configured && *configured == key)) {
      result.push_back(key);
    }
  }
  return result;
}

bool matchesPageFilter(const std::string& key, const char* filter) {
  if (filter == nullptr || filter[0] == '\0') {
    return true;
  }
  std::string needle = filter;
  for (char& character : needle) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  std::string hay = key;
  for (char& character : hay) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return hay.find(needle) != std::string::npos;
}

void seedPagesUi(PagesUi& ui, const ApplicationLaunchState& launch) {
  if (!ui.jsonSeeded) {
    std::snprintf(
        ui.json.data(),
        ui.json.size(),
        "%s",
        launch.configuredInitialPropsJson.c_str());
    ui.jsonSeeded = true;
  }
  if (!ui.selectedKey.empty()) {
    return;
  }
  const auto keys =
      selectableAppKeys(launch.appKeys, launch.configuredAppKey);
  if (launch.configuredAppKey) {
    for (const auto& key : keys) {
      if (key == *launch.configuredAppKey) {
        ui.selectedKey = key;
        return;
      }
    }
  }
  if (!keys.empty()) {
    ui.selectedKey = keys.front();
  }
}

void drawPagesLog(std::string& log) {
  if (log.empty()) {
    return;
  }
  ImGui::Dummy({0.0f, 6.0f});
  ImGui::Separator();
  ImGui::Dummy({0.0f, 4.0f});
  if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (imgui_theme::button(
            "Copy", imgui_theme::ButtonKind::Ghost, {56.0f, 0.0f})) {
      ImGui::SetClipboardText(log.c_str());
    }
    ImGui::SameLine();
    if (imgui_theme::button(
            "Clear", imgui_theme::ButtonKind::Ghost, {56.0f, 0.0f})) {
      log.clear();
      return;
    }
    ImGui::Dummy({0.0f, 4.0f});
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_theme::palette().danger);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, imgui_theme::palette().panelAlt);
    ImGui::InputTextMultiline(
        "##pages-log",
        log.data(),
        log.size() + 1,
        ImVec2(-FLT_MIN, std::max(72.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
    ImGui::PopStyleColor(2);
  }
}

void drawPages(
    Engine& engine,
    PagesUi& ui,
    std::string& log,
    bool runtimeFinished,
    char* filter,
    std::size_t filterSize) {
  const auto launch = engine.applicationLaunchState();
  seedPagesUi(ui, launch);
  const auto keys =
      selectableAppKeys(launch.appKeys, launch.configuredAppKey);
  char subtitle[96];
  if (!launch.initialBundlesLoaded) {
    std::snprintf(subtitle, sizeof(subtitle), "loading");
  } else {
    std::snprintf(
        subtitle,
        sizeof(subtitle),
        "%zu registered",
        keys.size());
  }
  imgui_theme::panelHeader("Pages", subtitle);

  const auto tryRun = [&] {
    try {
      engine.runApplication(ui.selectedKey, ui.json.data());
      ui.localError.clear();
    } catch (const std::exception& error) {
      ui.localError = error.what();
    }
  };
  const bool canRun = launch.appRegistryReady && !keys.empty() &&
      !ui.selectedKey.empty() && !launch.pending && !runtimeFinished;

  const bool showFilter = keys.size() > 8;
  if (showFilter) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint(
        "##page-filter", "Filter pages", filter, filterSize);
    ImGui::Dummy({0.0f, 4.0f});
  }

  int visibleKeys = 0;
  for (const auto& key : keys) {
    if (matchesPageFilter(key, filter)) {
      ++visibleKeys;
    }
  }
  const bool showLog = !log.empty();
  const float logReserve = showLog ? 120.0f : 0.0f;
  const float jsonHeight = 72.0f;
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const float footer = 6.0f + spacing + ImGui::GetTextLineHeight() + spacing +
      jsonHeight + spacing + 8.0f + spacing + 32.0f + 8.0f + logReserve;
  const float rowHeight =
      ImGui::GetFrameHeight() + spacing;
  const float listContent =
      static_cast<float>(std::max(1, visibleKeys)) * rowHeight + 16.0f;
  const float listMax = std::max(
      44.0f, ImGui::GetContentRegionAvail().y - footer);
  const float listHeight = std::clamp(listContent, 44.0f, listMax);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, imgui_theme::palette().panelAlt);
  ImGui::BeginChild(
      "pages-list",
      ImVec2(0, listHeight),
      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
  if (!launch.initialBundlesLoaded) {
    imgui_theme::mutedHint("Loading bundle…");
  } else if (!launch.appRegistryReady) {
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_theme::palette().danger);
    ImGui::TextWrapped("%s", "Bundle did not install RN$AppRegistry.");
    ImGui::PopStyleColor();
  } else if (keys.empty()) {
    imgui_theme::mutedHint("No AppRegistry applications are registered.");
  } else {
    bool any = false;
    for (const auto& key : keys) {
      if (!matchesPageFilter(key, filter)) {
        continue;
      }
      any = true;
      const bool selected = key == ui.selectedKey;
      const bool running =
          launch.runningAppKey && *launch.runningAppKey == key;
      ImGui::PushID(key.c_str());
      if (ImGui::Selectable(
              key.c_str(),
              selected,
              ImGuiSelectableFlags_AllowDoubleClick)) {
        ui.selectedKey = key;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && canRun) {
          tryRun();
        }
      }
      if (running) {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextDisabled("running");
      }
      ImGui::PopID();
    }
    if (!any) {
      imgui_theme::mutedHint("No pages match the filter.");
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  ImGui::Dummy({0.0f, 6.0f});
  ImGui::TextDisabled("initialProps");
  ImGui::InputTextMultiline(
      "##initial-props",
      ui.json.data(),
      ui.json.size(),
      ImVec2(-FLT_MIN, jsonHeight),
      ImGuiInputTextFlags_AllowTabInput);

  if (launch.pending) {
    ImGui::Dummy({0.0f, 4.0f});
    imgui_theme::mutedHint("Starting application…");
  }
  const auto& errorText =
      !ui.localError.empty() ? ui.localError : launch.lastError;
  if (!errorText.empty()) {
    ImGui::Dummy({0.0f, 4.0f});
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_theme::palette().danger);
    ImGui::TextWrapped("%s", errorText.c_str());
    ImGui::PopStyleColor();
  }

  ImGui::Dummy({0.0f, 8.0f});
  const char* runLabel = launch.runningAppKey ? "Re-run" : "Run";
  if (imgui_theme::button(
          runLabel,
          canRun ? imgui_theme::ButtonKind::Primary
                 : imgui_theme::ButtonKind::Neutral,
          ImVec2(-FLT_MIN, 32.0f)) &&
      canRun) {
    tryRun();
  }
  drawPagesLog(log);
}

float dialogButtonWidth(const char* label) {
  return std::max(88.0f, ImGui::CalcTextSize(label).x + 28.0f);
}

bool dialogButton(
    const char* id,
    const char* label,
    imgui_theme::ButtonKind kind) {
  ImGui::PushID(id);
  const bool clicked =
      imgui_theme::button(label, kind, {dialogButtonWidth(label), 34.0f});
  ImGui::PopID();
  return clicked;
}

ImU32 withAlpha(ImU32 color, int alpha) {
  return (color & ~IM_COL32_A_MASK) |
      (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

void drawHostDialogChrome(
    const char* kind,
    ImU32 kindColor,
    const char* title,
    const char* message,
    const char* hint) {
  const ImVec2 windowPos = ImGui::GetWindowPos();
  const ImVec2 windowSize = ImGui::GetWindowSize();
  auto* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(
      windowPos,
      {windowPos.x + windowSize.x, windowPos.y + 3.0f},
      kindColor,
      12.0f,
      ImDrawFlags_RoundCornersTop);
  imgui_theme::chip(kind, withAlpha(kindColor, 36), kindColor, true);
  ImGui::Dummy({0.0f, 8.0f});
  if (title != nullptr && title[0] != '\0') {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.22f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 372.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
  }
  if (message != nullptr && message[0] != '\0') {
    ImGui::Dummy({0.0f, 6.0f});
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_theme::palette().muted);
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 372.0f);
    ImGui::TextWrapped("%s", message);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  if (hint != nullptr && hint[0] != '\0') {
    ImGui::Dummy({0.0f, 8.0f});
    imgui_theme::mutedHint(hint);
  }
}

void drawHostUi() {
  const auto pending = hostUi().peek();
  if (!pending) {
    return;
  }
  if (pending->kind == HostUiKind::Vibration) {
    static int vibrationId = -1;
    static std::chrono::steady_clock::time_point vibrationStart;
    if (vibrationId != pending->id) {
      vibrationId = pending->id;
      vibrationStart = std::chrono::steady_clock::now();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - vibrationStart)
                             .count();
    const float duration =
        std::max(1, pending->vibration.durationMs) * 1.0f;
    const float progress =
        std::clamp(static_cast<float>(elapsed) / duration, 0.0f, 1.0f);
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x - 20.0f, 56.0f},
        ImGuiCond_Always,
        {1.0f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16.0f, 14.0f});
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg, imgui_theme::palette().elevated);
    ImGui::Begin(
        "host-vibrate",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing);
    imgui_theme::toneChip("Vibrate", imgui_theme::ChipTone::Accent, true);
    ImGui::Dummy({0.0f, 4.0f});
    ImGui::TextDisabled("%d ms mock pulse", pending->vibration.durationMs);
    ImGui::Dummy({0.0f, 6.0f});
    ImGui::PushStyleColor(
        ImGuiCol_PlotHistogram, imgui_theme::palette().accent);
    ImGui::ProgressBar(progress, ImVec2(220.0f, 6.0f), "");
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (elapsed >= pending->vibration.durationMs) {
      hostUi().dismissVibration();
    }
    return;
  }

  static int openedId = -1;
  if (openedId != pending->id) {
    ImGui::OpenPopup("Host UI");
    openedId = pending->id;
  }
  ImGui::SetNextWindowPos(
      ImGui::GetMainViewport()->GetCenter(),
      ImGuiCond_Appearing,
      {0.5f, 0.5f});
  ImGui::SetNextWindowSizeConstraints({400.0f, 0.0f}, {440.0f, 640.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22.0f, 20.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {10.0f, 8.0f});
  if (!ImGui::BeginPopupModal(
          "Host UI",
          nullptr,
          ImGuiWindowFlags_AlwaysAutoResize |
              ImGuiWindowFlags_NoSavedSettings |
              ImGuiWindowFlags_NoTitleBar |
              ImGuiWindowFlags_NoMove)) {
    ImGui::PopStyleVar(2);
    return;
  }
  ImGui::Dummy({396.0f, 0.0f});
  switch (pending->kind) {
    case HostUiKind::Alert: {
      const auto& alert = pending->alert;
      drawHostDialogChrome(
          "Alert",
          imgui_theme::vecToU32(imgui_theme::palette().inspect),
          alert.title.empty() ? "Alert" : alert.title.c_str(),
          alert.message.c_str(),
          nullptr);
      if (!alert.items.empty()) {
        ImGui::Dummy({0.0f, 10.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.0f, 8.0f});
        for (int index = 0; index < static_cast<int>(alert.items.size());
             ++index) {
          ImGui::PushID(index);
          if (imgui_theme::button(
                  alert.items[index].c_str(),
                  imgui_theme::ButtonKind::Neutral,
                  ImVec2(-FLT_MIN, 36.0f))) {
            hostUi().completeAlert(
                {.dismissed = false, .buttonKey = index});
            ImGui::CloseCurrentPopup();
          }
          ImGui::PopID();
        }
        ImGui::PopStyleVar();
      }
      ImGui::Dummy({0.0f, 14.0f});
      if (alert.cancelable && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        hostUi().completeAlert({.dismissed = true, .buttonKey = 0});
        ImGui::CloseCurrentPopup();
      }
      const bool hasNeutral = !alert.buttonNeutral.empty();
      const bool hasNegative = !alert.buttonNegative.empty();
      const bool hasPositive = !alert.buttonPositive.empty();
      float rowWidth = 0.0f;
      if (alert.cancelable) {
        rowWidth += dialogButtonWidth("Dismiss");
      }
      if (hasNeutral) {
        rowWidth += dialogButtonWidth(alert.buttonNeutral.c_str()) + 8.0f;
      }
      if (hasNegative) {
        rowWidth += dialogButtonWidth(alert.buttonNegative.c_str()) + 8.0f;
      }
      if (hasPositive) {
        rowWidth += dialogButtonWidth(alert.buttonPositive.c_str()) + 8.0f;
      }
      ImGui::SetCursorPosX(
          ImGui::GetCursorPosX() +
          std::max(0.0f, ImGui::GetContentRegionAvail().x - rowWidth));
      if (alert.cancelable) {
        if (dialogButton("dismiss", "Dismiss", imgui_theme::ButtonKind::Ghost)) {
          hostUi().completeAlert({.dismissed = true, .buttonKey = 0});
          ImGui::CloseCurrentPopup();
        }
        if (hasNeutral || hasNegative || hasPositive) {
          ImGui::SameLine(0.0f, 8.0f);
        }
      }
      if (hasNeutral) {
        if (dialogButton(
                "neutral",
                alert.buttonNeutral.c_str(),
                imgui_theme::ButtonKind::Neutral)) {
          hostUi().completeAlert({.dismissed = false, .buttonKey = -3});
          ImGui::CloseCurrentPopup();
        }
        if (hasNegative || hasPositive) {
          ImGui::SameLine(0.0f, 8.0f);
        }
      }
      if (hasNegative) {
        if (dialogButton(
                "negative",
                alert.buttonNegative.c_str(),
                imgui_theme::ButtonKind::Danger)) {
          hostUi().completeAlert({.dismissed = false, .buttonKey = -2});
          ImGui::CloseCurrentPopup();
        }
        if (hasPositive) {
          ImGui::SameLine(0.0f, 8.0f);
        }
      }
      if (hasPositive &&
          dialogButton(
              "positive",
              alert.buttonPositive.c_str(),
              imgui_theme::ButtonKind::Primary)) {
        hostUi().completeAlert({.dismissed = false, .buttonKey = -1});
        ImGui::CloseCurrentPopup();
      }
      break;
    }
    case HostUiKind::Share: {
      const auto& share = pending->share;
      drawHostDialogChrome(
          "Share",
          imgui_theme::vecToU32(imgui_theme::palette().accent),
          share.dialogTitle.empty() ? "Share" : share.dialogTitle.c_str(),
          share.title.empty() ? share.message.c_str() : share.title.c_str(),
          "Mock sheet — no OS share targets");
      if (!share.title.empty() && !share.message.empty()) {
        ImGui::Dummy({0.0f, 6.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_theme::palette().text);
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 372.0f);
        ImGui::TextWrapped("%s", share.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
      }
      ImGui::Dummy({0.0f, 14.0f});
      ImGui::SetCursorPosX(
          ImGui::GetCursorPosX() +
          std::max(
              0.0f,
              ImGui::GetContentRegionAvail().x -
                  dialogButtonWidth("Dismiss") -
                  dialogButtonWidth("Share") - 8.0f));
      if (dialogButton("share-dismiss", "Dismiss", imgui_theme::ButtonKind::Ghost)) {
        hostUi().completeShare({.shared = false});
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine(0.0f, 8.0f);
      if (dialogButton("share", "Share", imgui_theme::ButtonKind::Primary)) {
        hostUi().completeShare({.shared = true});
        ImGui::CloseCurrentPopup();
      }
      break;
    }
    case HostUiKind::Permission: {
      drawHostDialogChrome(
          "Permission",
          imgui_theme::vecToU32(imgui_theme::palette().success),
          "Allow permission?",
          nullptr,
          "Mock prompt — records grant or deny for checkPermission");
      ImGui::Dummy({0.0f, 8.0f});
      ImGui::PushStyleColor(
          ImGuiCol_ChildBg, imgui_theme::palette().panelAlt);
      ImGui::BeginChild(
          "permission-list",
          ImVec2(0.0f, std::min(
              160.0f,
              16.0f +
                  28.0f *
                      static_cast<float>(
                          std::max<std::size_t>(
                              1, pending->permission.permissions.size())))),
          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
      for (const auto& permission : pending->permission.permissions) {
        ImGui::BulletText("%s", permission.c_str());
      }
      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::Dummy({0.0f, 14.0f});
      ImGui::SetCursorPosX(
          ImGui::GetCursorPosX() +
          std::max(
              0.0f,
              ImGui::GetContentRegionAvail().x -
                  dialogButtonWidth("Deny") -
                  dialogButtonWidth("Allow") - 8.0f));
      if (dialogButton("deny", "Deny", imgui_theme::ButtonKind::Danger)) {
        hostUi().completePermission(false);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine(0.0f, 8.0f);
      if (dialogButton("allow", "Allow", imgui_theme::ButtonKind::Primary)) {
        hostUi().completePermission(true);
        ImGui::CloseCurrentPopup();
      }
      break;
    }
    case HostUiKind::OpenUrl:
    case HostUiKind::OpenSettings: {
      const bool settings = pending->kind == HostUiKind::OpenSettings;
      drawHostDialogChrome(
          settings ? "Settings" : "Open URL",
          imgui_theme::vecToU32(imgui_theme::palette().info),
          settings ? "Open settings?" : "Open URL?",
          settings
              ? "No Settings app is hosted. Confirm to mock success."
              : pending->openUrl.url.c_str(),
          "Mock — does not launch an OS handler");
      ImGui::Dummy({0.0f, 14.0f});
      const char* confirm = settings ? "Open settings" : "Open";
      ImGui::SetCursorPosX(
          ImGui::GetCursorPosX() +
          std::max(
              0.0f,
              ImGui::GetContentRegionAvail().x -
                  dialogButtonWidth("Cancel") -
                  dialogButtonWidth(confirm) - 8.0f));
      if (dialogButton("url-cancel", "Cancel", imgui_theme::ButtonKind::Ghost)) {
        hostUi().completeOpenUrl(
            false,
            settings
                ? "IntentAndroid.openSettings has no Settings app in react-native-simulator"
                : "IntentAndroid.openURL has no OS handler in react-native-simulator");
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine(0.0f, 8.0f);
      if (dialogButton("url-open", confirm, imgui_theme::ButtonKind::Primary)) {
        hostUi().completeOpenUrl(true, {});
        ImGui::CloseCurrentPopup();
      }
      break;
    }
    case HostUiKind::Vibration:
      break;
  }
  ImGui::EndPopup();
  ImGui::PopStyleVar(2);
}

void drawToolbar(
    Engine& engine,
    const SimulatorWindowChrome& chrome,
    std::string& diagnosticLog,
    std::string& lastLoggedAppError,
    bool runtimeFinished,
    bool& selectOverlay) {
  const float contentHeight = ImGui::GetFrameHeight();
  float height = contentHeight + 16.0f;
  if (chrome.overlaysTitlebar && chrome.toolbarHeight > height) {
    height = chrome.toolbarHeight;
  }
  const float leftPad =
      chrome.overlaysTitlebar ? std::max(12.0f, chrome.leadingInset) : 12.0f;
  const float padY = std::max(4.0f, (height - contentHeight) * 0.5f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, imgui_theme::palette().bg);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {leftPad, padY});
  ImGui::BeginChild(
      "toolbar",
      ImVec2(0.0f, height),
      ImGuiChildFlags_AlwaysUseWindowPadding,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  imgui_theme::liveChip(!runtimeFinished);
  ImGui::SameLine(0.0f, 12.0f);
  int mode = selectOverlay ? 1 : 0;
  const char* modes[] = {"Interact", "Select"};
  const char* shortcuts[] = {"1", "2"};
  if (imgui_theme::segmented("mode", modes, 2, &mode, shortcuts)) {
    selectOverlay = mode == 1;
  }
  const auto launch = engine.applicationLaunchState();
  ImGui::SameLine(0.0f, 12.0f);
  ImGui::BeginDisabled(runtimeFinished || !launch.initialBundlesLoaded);
  if (imgui_theme::button(
          "Reload", imgui_theme::ButtonKind::Ghost, {72.0f, 0.0f})) {
    engine.requestReload();
  }
  ImGui::EndDisabled();
  {
    if (launch.runningAppKey) {
      ImGui::SameLine(0.0f, 14.0f);
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", launch.runningAppKey->c_str());
    }
    if (!launch.lastError.empty()) {
      appendDiagnostic(
          diagnosticLog,
          lastLoggedAppError,
          "AppRegistry",
          launch.lastError);
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 lineStart = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(
      lineStart,
      {lineStart.x + ImGui::GetContentRegionAvail().x, lineStart.y},
      imgui_theme::wash(imgui_theme::palette().border, 180),
      1.0f);
  ImGui::Dummy({0.0f, 1.0f});
}

} // namespace

EngineResult runInteractiveFrontend(
    Engine& engine,
    const std::filesystem::path& fontDirectory,
    std::function<void(const std::function<bool()>&)> prepareRuntime) {
  hostUi().setDeferToFrontend(true);
  auto state = std::make_shared<FrontendState>();
  engine.setSceneUpdateCallback(
      [state](std::shared_ptr<const SceneSnapshot> scene) {
        std::lock_guard lock(state->mutex);
        state->scene = std::move(scene);
      });
  engine.setActionResultCallback([state](const InteractionResult& result) {
    std::lock_guard lock(state->mutex);
    state->action = result;
  });

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    throw std::runtime_error(
        std::string("SDL_Init failed: ") + SDL_GetError());
  }
  applySimulatorAppIcon(nullptr);
  SDL_Window* window = SDL_CreateWindow(
      "react-native-simulator", 1280, 860,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr) {
    SDL_Quit();
    throw std::runtime_error(
        std::string("SDL_CreateWindow failed: ") + SDL_GetError());
  }
  SDL_SetWindowMinimumSize(window, 960, 640);
  applySimulatorWindowChrome(window);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    throw std::runtime_error(
        std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
  }
  SDL_SetRenderVSync(renderer, 1);
  SDL_StartTextInput(window);
  const SimulatorWindowChrome chrome = applySimulatorWindowChrome(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  float pixelDensity = SDL_GetWindowPixelDensity(window);
  if (pixelDensity < 1.0f) {
    pixelDensity = 1.0f;
  }
  imgui_theme::loadUiFonts(14.0f, pixelDensity, nullptr);
  imgui_theme::apply();
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  std::thread runtimeThread(
      [state, &engine, prepareRuntime = std::move(prepareRuntime)] {
    EngineResult result;
    try {
      if (prepareRuntime) {
        prepareRuntime([state] { return state->cancelRequested.load(); });
      }
      if (state->cancelRequested.load()) {
        result.exitCode = 0;
      } else {
        result = engine.run();
      }
    } catch (const std::exception& error) {
      result.exitCode = 1;
      result.error = error.what();
    } catch (...) {
      result.exitCode = 1;
      result.error = "Unknown interactive runtime preparation error";
    }
    {
      std::lock_guard lock(state->mutex);
      state->result = std::move(result);
    }
    state->runtimeFinished.store(true);
  });

  SkiaMountedTreeRenderer sceneRenderer(fontDirectory);
  SDL_Texture* texture = nullptr;
  std::int64_t renderedRevision = -1;
  std::string renderError;
  std::string diagnosticLog;
  std::string lastLoggedError;
  std::string lastLoggedAppError;
  PagesUi pages;
  char pageFilter[64] = {};
  bool blockCanvasInput = false;
  int selectedShadowTag = 0;
  char shadowFilter[64] = {};
  bool scrollToSelected = false;
  bool selectOverlay = false;
  CanvasMapping mapping;
  int buttons = 0;
  SDL_Cursor* arrowCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
  SDL_Cursor* selectCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
  std::optional<std::pair<float, float>> lastPoint;
  std::shared_ptr<const SceneSnapshot> scene;
  const char* smokeOutput = std::getenv("RNS_INTERACTIVE_SMOKE_OUTPUT");
  auto smokeTimeout = std::chrono::milliseconds(20000);
  if (smokeOutput != nullptr) {
    if (const char* value =
            std::getenv("RNS_INTERACTIVE_SMOKE_TIMEOUT_MS")) {
      char* end = nullptr;
      const long parsed = std::strtol(value, &end, 10);
      if (end != value && *end == '\0' && parsed >= 100 && parsed <= 60000) {
        smokeTimeout = std::chrono::milliseconds(parsed);
      }
    }
  }
  const auto smokeDeadline = std::chrono::steady_clock::now() +
      smokeTimeout;
  bool smokeFrameReady = false;
  int smokeFrameWidth = 0;
  int smokeFrameHeight = 0;
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT ||
          (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
           event.window.windowID == SDL_GetWindowID(window))) {
        done = true;
        continue;
      }
      const bool mouseEvent =
          !blockCanvasInput &&
          (event.type == SDL_EVENT_MOUSE_MOTION ||
           event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
           event.type == SDL_EVENT_MOUSE_WHEEL);
      const auto point = mouseEvent
          ? mapping.map(io.MousePos.x, io.MousePos.y, buttons == 0)
          : std::nullopt;
      if (point) {
        lastPoint = point;
      }
      const bool altHeld = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && point) {
        const int button = event.button.button == SDL_BUTTON_LEFT ? 0 :
            event.button.button == SDL_BUTTON_RIGHT ? 2 : 1;
        if (selectOverlay && scene) {
          if (const auto hit =
                  hitTestScene(*scene, point->first, point->second)) {
            if (selectedShadowTag != hit->tag) {
              selectedShadowTag = hit->tag;
              scrollToSelected = true;
            }
          }
        }
        if (!(selectOverlay && altHeld)) {
          buttons |= 1 << button;
          enqueueIgnoringStopped(engine, {
              .type = InteractionActionType::PointerDown,
              .x = point->first,
              .y = point->second,
              .button = button,
              .buttons = buttons});
        }
      } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        const int button = event.button.button == SDL_BUTTON_LEFT ? 0 :
            event.button.button == SDL_BUTTON_RIGHT ? 2 : 1;
        if (buttons != 0) {
          buttons &= ~(1 << button);
          const auto upPoint = point ? point : lastPoint;
          if (upPoint) {
            enqueueIgnoringStopped(engine, {
                .type = InteractionActionType::PointerUp,
                .x = upPoint->first,
                .y = upPoint->second,
                .button = button,
                .buttons = buttons});
          }
        }
      } else if (event.type == SDL_EVENT_MOUSE_MOTION && point && buttons != 0) {
        enqueueIgnoringStopped(engine, {
            .type = InteractionActionType::PointerMove,
            .x = point->first,
            .y = point->second,
            .buttons = buttons});
      } else if (event.type == SDL_EVENT_MOUSE_WHEEL && point) {
        enqueueIgnoringStopped(engine, {
            .type = InteractionActionType::Scroll,
            .x = point->first,
            .y = point->second,
            .deltaX = -event.wheel.x * 40.0f,
            .deltaY = -event.wheel.y * 40.0f});
      } else if (
          event.type == SDL_EVENT_TEXT_INPUT && event.text.text[0] &&
          !blockCanvasInput && !io.WantTextInput) {
        enqueueIgnoringStopped(engine, {
            .type = InteractionActionType::TextInput,
            .text = event.text.text});
      } else if (
          event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
          !blockCanvasInput && !io.WantTextInput) {
        if (event.key.key == SDLK_1 || event.key.key == SDLK_2) {
          selectOverlay = event.key.key == SDLK_2;
        } else {
          std::string key;
          if (event.key.key == SDLK_ESCAPE ||
              event.key.key == SDLK_AC_BACK) {
            enqueueIgnoringStopped(engine, {
                .type = InteractionActionType::HardwareBackPress});
          } else if (event.key.key == SDLK_BACKSPACE) {
            key = "Backspace";
          } else if (event.key.key == SDLK_RETURN ||
                     event.key.key == SDLK_KP_ENTER) {
            key = "Enter";
          } else if (event.key.key == SDLK_TAB) {
            key = "Tab";
          }
          if (!key.empty()) {
            enqueueIgnoringStopped(engine, {
                .type = InteractionActionType::KeyDown,
                .ctrlKey = (event.key.mod & SDL_KMOD_CTRL) != 0,
                .shiftKey = (event.key.mod & SDL_KMOD_SHIFT) != 0,
                .altKey = (event.key.mod & SDL_KMOD_ALT) != 0,
                .metaKey = (event.key.mod & SDL_KMOD_GUI) != 0,
                .key = std::move(key)});
          }
        }
      }
    }

    std::optional<InteractionResult> action;
    std::optional<EngineResult> result;
    {
      std::lock_guard lock(state->mutex);
      scene = state->scene;
      action = state->action;
      result = state->result;
    }
    if (scene &&
        (scene->revision != renderedRevision ||
         sceneHasAnimatingIndicator(*scene))) {
      const auto frame = sceneRenderer.render(*scene);
      renderError = frame.error;
      if (frame) {
        if (texture == nullptr) {
          texture = SDL_CreateTexture(
              renderer, SDL_PIXELFORMAT_RGBA32,
              SDL_TEXTUREACCESS_STREAMING, frame.width, frame.height);
        } else {
          float width = 0;
          float height = 0;
          SDL_GetTextureSize(texture, &width, &height);
          if (static_cast<int>(width) != frame.width ||
              static_cast<int>(height) != frame.height) {
            SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(
                renderer, SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING, frame.width, frame.height);
          }
        }
        if (texture == nullptr || !SDL_UpdateTexture(
                texture, nullptr, frame.rgba.data(),
                static_cast<int>(frame.rowBytes))) {
          renderError = std::string("SDL texture upload failed: ") +
              SDL_GetError();
        } else {
          smokeFrameReady = true;
          smokeFrameWidth = frame.width;
          smokeFrameHeight = frame.height;
        }
      }
      renderedRevision = scene->revision;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, imgui_theme::palette().bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 0.0f});
    ImGui::Begin(
        "simulator", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    drawToolbar(
        engine,
        chrome,
        diagnosticLog,
        lastLoggedAppError,
        state->runtimeFinished.load(),
        selectOverlay);
    if (renderError.empty() && result && !result->error.empty()) {
      appendDiagnostic(
          diagnosticLog, lastLoggedError, "Runtime error", result->error);
    } else if (!renderError.empty()) {
      appendDiagnostic(
          diagnosticLog, lastLoggedError, "Render error", renderError);
    }
    if (action && !action->error.empty()) {
      appendDiagnostic(
          diagnosticLog, lastLoggedError, "Input error", action->error);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 8.0f});
    ImGui::BeginChild(
        "workspace",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float treeWidth = std::clamp(available.x * 0.21f, 220.0f, 268.0f);
    const float pagesWidth = std::clamp(available.x * 0.21f, 244.0f, 292.0f);
    const float canvasWidth = std::max(
        160.0f, available.x - treeWidth - pagesWidth - spacing * 2.0f);
    int hoveredTag = 0;
    imgui_theme::beginPanel(
        "shadow-tree",
        ImVec2(treeWidth, 0),
        imgui_theme::palette().panel,
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar);
    drawShadowTree(
        scene.get(),
        selectedShadowTag,
        hoveredTag,
        scrollToSelected,
        shadowFilter,
        sizeof(shadowFilter));
    imgui_theme::endPanel();
    ImGui::SameLine();
    imgui_theme::beginPanel(
        "device-canvas",
        ImVec2(canvasWidth, 0),
        imgui_theme::palette().panelAlt,
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar);
    drawDeviceCanvas(
        texture,
        scene.get(),
        mapping,
        selectedShadowTag,
        hoveredTag,
        selectOverlay,
        engine,
        !blockCanvasInput);
    if (selectCursor != nullptr && arrowCursor != nullptr) {
      SDL_SetCursor(
          selectOverlay && mapping.hovered &&
                  (SDL_GetModState() & SDL_KMOD_ALT) != 0
              ? selectCursor
              : arrowCursor);
    }
    imgui_theme::endPanel();
    ImGui::SameLine();
    imgui_theme::beginPanel(
        "pages",
        ImVec2(pagesWidth, 0),
        imgui_theme::palette().panel,
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar);
    drawPages(
        engine,
        pages,
        diagnosticLog,
        state->runtimeFinished.load(),
        pageFilter,
        sizeof(pageFilter));
    imgui_theme::endPanel();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    drawHostUi();
    blockCanvasInput = ImGui::IsPopupOpen(
        "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    ImGui::Render();
    SDL_SetRenderScale(
        renderer, io.DisplayFramebufferScale.x,
        io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(
        renderer,
        imgui_theme::canvasR(),
        imgui_theme::canvasG(),
        imgui_theme::canvasB(),
        255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
    if (smokeOutput != nullptr) {
      const bool sceneReady = scene && scene->revision >= 0 &&
          (!scene->nodes.empty() || !scene->shadowNodes.empty());
      if (sceneReady && smokeFrameReady) {
        std::ofstream output(smokeOutput, std::ios::binary);
        if (!output) {
          renderError = std::string("Cannot write interactive smoke result: ") +
              smokeOutput;
        } else {
          output << "{\"ready\":true,\"window\":true,\"sceneRevision\":"
                 << scene->revision << ",\"sceneNodes\":"
                 << scene->nodes.size() << ",\"shadowNodes\":"
                 << scene->shadowNodes.size() << ",\"frameWidth\":"
                 << smokeFrameWidth << ",\"frameHeight\":"
                 << smokeFrameHeight << "}\n";
        }
        done = true;
      } else if (std::chrono::steady_clock::now() >= smokeDeadline) {
        std::ofstream output(smokeOutput, std::ios::binary);
        if (output) {
          output << "{\"ready\":false,\"window\":true,\"error\":"
                    "\"timed out waiting for a non-empty Skia frame\"}\n";
        }
        done = true;
      }
    }
  }

  hostUi().setDeferToFrontend(false);
  hostUi().reset();
  state->cancelRequested.store(true);
  engine.requestStop();
  runtimeThread.join();
  EngineResult finalResult;
  {
    std::lock_guard lock(state->mutex);
    if (state->result) {
      finalResult = std::move(*state->result);
    }
  }
  if (selectCursor != nullptr) {
    SDL_DestroyCursor(selectCursor);
  }
  if (arrowCursor != nullptr) {
    SDL_DestroyCursor(arrowCursor);
  }
  if (texture != nullptr) {
    SDL_DestroyTexture(texture);
  }
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return finalResult;
}

} // namespace ReactNativeSimulator
