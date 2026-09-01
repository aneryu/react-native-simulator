#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "MacWindowChrome.h"
#include "SimulatorImGuiTheme.h"

#if RNS_ENABLE_SKIA
#include "SkiaMountedTreeRenderer.h"
#endif

#include <SDL3/SDL.h>
#include <folly/json.h>

#include <mach-o/dyld.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <deque>
#include <string>
#include <vector>

extern char **environ;

namespace {

struct Document {
  std::filesystem::path path;
  folly::dynamic value;
  std::string raw;
  std::string error;
  std::filesystem::file_time_type modifiedAt{};
  bool hasModifiedAt{false};
  bool loaded{false};
  std::uint64_t revision{0};
  folly::dynamic sceneValue;
  std::uint64_t sceneRevision{0};
  bool sceneLoaded{false};
};

static std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), {}};
}

static void reloadIfChanged(Document &document, bool force = false) {
  if (document.path.empty()) {
    return;
  }
  try {
    const auto modifiedAt = std::filesystem::last_write_time(document.path);
    if (!force && document.hasModifiedAt && modifiedAt == document.modifiedAt) {
      return;
    }
    auto raw = readFile(document.path);
    auto value = folly::parseJson(raw);
    document.raw = std::move(raw);
    document.value = std::move(value);
    document.modifiedAt = modifiedAt;
    document.hasModifiedAt = true;
    document.loaded = true;
    ++document.revision;
    document.error.clear();
  } catch (const std::exception &error) {
    document.loaded = false;
    document.error = error.what();
  }
}

static const folly::dynamic *field(const folly::dynamic &object,
                                   const char *name) {
  if (!object.isObject()) {
    return nullptr;
  }
  return object.get_ptr(name);
}

static std::string stringValue(const folly::dynamic &object, const char *name,
                               std::string fallback = "-") {
  const auto *value = field(object, name);
  return value != nullptr && value->isString() ? value->asString()
                                               : std::move(fallback);
}

static double numberValue(const folly::dynamic &object, const char *name,
                          double fallback = 0) {
  const auto *value = field(object, name);
  return value != nullptr && value->isNumber() ? value->asDouble() : fallback;
}

static bool booleanValue(const folly::dynamic &object, const char *name,
                         bool fallback = false) {
  const auto *value = field(object, name);
  return value != nullptr && value->isBool() ? value->asBool() : fallback;
}

static std::string formatNumber(double value, int precision = 2) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(precision) << value;
  return output.str();
}

static std::string formatBytes(double bytes) {
  static constexpr const char *units[] = {"B", "KiB", "MiB", "GiB"};
  auto unit = 0U;
  auto value = std::max(0.0, bytes);
  while (value >= 1024.0 && unit < 3U) {
    value /= 1024.0;
    ++unit;
  }
  return formatNumber(value, unit == 0U ? 0 : 2) + " " + units[unit];
}

static void drawValue(const char *label, const std::string &value) {
  ReactNativeSimulator::imgui_theme::propertyRow(label, value.c_str(), 168.0f);
}

static void drawMetric(const char *label, const folly::dynamic &object,
                       const char *key, bool bytes = false) {
  const auto value = numberValue(object, key);
  drawValue(label, bytes ? formatBytes(value) : formatNumber(value) + " ms");
}

static void drawGate(const char *label, bool passed) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(label);
  ImGui::TableSetColumnIndex(1);
  namespace theme = ReactNativeSimulator::imgui_theme;
  if (passed) {
    theme::toneChip("PASS", theme::ChipTone::Success);
  } else {
    theme::toneChip("CHECK", theme::ChipTone::Danger);
  }
}

static void drawOverview(const folly::dynamic &metrics) {
  ImGui::Begin("Runtime overview");
  drawValue("Host", stringValue(metrics, "host"));
  drawValue("React Native", stringValue(metrics, "reactNativeVersion"));
  drawValue("Hermes", stringValue(metrics, "hermesVersion"));
  drawValue("Profile", stringValue(metrics, "profile"));
  drawValue("Platform", stringValue(metrics, "platformProfile"));
  drawValue("Workload", stringValue(metrics, "workload"));
  drawValue("Mode", stringValue(metrics, "validationMode"));
  drawValue("Runtime state", stringValue(metrics, "runtimeState", "replay"));
  if (field(metrics, "sequence") != nullptr) {
    drawValue("Snapshot", formatNumber(numberValue(metrics, "sequence"), 0));
  }
  drawValue("Bundle hash", stringValue(metrics, "bundleHash"));
  ImGui::Separator();
  ImGui::TextUnformatted("Runtime gates");
  if (ImGui::BeginTable("runtime-gates", 2, ImGuiTableFlags_RowBg)) {
    drawGate("Bundle loaded", booleanValue(metrics, "bundleLoaded"));
    drawGate("Workload ready", booleanValue(metrics, "workloadReady"));
    drawGate("Workload complete", booleanValue(metrics, "workloadComplete"));
    drawGate("RuntimeScheduler", booleanValue(metrics, "runtimeScheduler"));
    drawGate("React Fabric", booleanValue(metrics, "reactFabric"));
    drawGate("Yoga", booleanValue(metrics, "reactYoga"));
    drawGate("No JS errors", numberValue(metrics, "jsErrors") == 0);
    drawGate("No pending work", !booleanValue(metrics, "pendingWork"));
    ImGui::EndTable();
  }
  ImGui::End();
}

static void drawTimingAndResources(const folly::dynamic &metrics) {
  ImGui::Begin("Timing and resources");
  ImGui::TextUnformatted("Timing");
  drawMetric("Initialization", metrics, "initializationMs");
  drawMetric("Bundle + drain", metrics, "bundleAndDrainMs");
  drawMetric("Render", metrics, "renderMs");
  drawMetric("CPU workload", metrics, "cpuMs");
  drawMetric("Fabric commit", metrics, "commitMs");
  drawMetric("Fabric layout", metrics, "layoutMs");
  drawMetric("Fabric diff", metrics, "diffMs");
  ImGui::Separator();
  ImGui::TextUnformatted("Process resources");
  drawMetric("Resident", metrics, "residentBytes", true);
  drawMetric("Peak resident", metrics, "peakResidentBytes", true);
  drawMetric("Heap allocated", metrics, "heapAllocatedBytes", true);
  drawMetric("Heap size", metrics, "heapSizeBytes", true);
  drawMetric("GC total", metrics, "gcTotalMs");
  drawValue("GC collections",
            formatNumber(numberValue(metrics, "gcCollections"), 0));
  drawMetric("User CPU", metrics, "processUserCpuMs");
  drawMetric("System CPU", metrics, "processSystemCpuMs");
  ImGui::Separator();
  drawValue("Fabric transactions",
            formatNumber(numberValue(metrics, "reactFabricTransactions"), 0));
  drawValue("Fabric creates",
            formatNumber(numberValue(metrics, "reactFabricCreates"), 0));
  drawValue("Fabric inserts",
            formatNumber(numberValue(metrics, "reactFabricInserts"), 0));
  drawValue("Fabric updates",
            formatNumber(numberValue(metrics, "reactFabricUpdates"), 0));
  drawValue("Fabric removes",
            formatNumber(numberValue(metrics, "reactFabricRemoves"), 0));
  drawValue("Fabric deletes",
            formatNumber(numberValue(metrics, "reactFabricDeletes"), 0));
  ImGui::End();
}

static void drawBundles(const folly::dynamic &metrics) {
  ImGui::Begin("Bundles");
  const auto *bundles = field(metrics, "bundles");
  if (bundles == nullptr || !bundles->isArray()) {
    ImGui::TextDisabled("No bundle records");
    ImGui::End();
    return;
  }
  if (ImGui::BeginTable("bundles", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollX |
                            ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 86.0f);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Eval", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Error", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const auto &bundle : *bundles) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(stringValue(bundle, "path").c_str());
      ImGui::TableSetColumnIndex(1);
      const auto loaded = booleanValue(bundle, "loaded");
      namespace theme = ReactNativeSimulator::imgui_theme;
      if (loaded) {
        theme::toneChip("loaded", theme::ChipTone::Success);
      } else {
        theme::toneChip("failed", theme::ChipTone::Danger);
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(formatBytes(numberValue(bundle, "bytes")).c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(
          (formatNumber(numberValue(bundle, "evaluationMs")) + " ms").c_str());
      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(stringValue(bundle, "error", "").c_str());
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

static void drawTrace(const Document &trace) {
  ImGui::Begin("Trace events");
  if (!trace.loaded) {
    ImGui::TextDisabled("%s", trace.error.empty() ? "No trace selected"
                                                  : trace.error.c_str());
    ImGui::End();
    return;
  }
  const auto *events = field(trace.value, "traceEvents");
  if (events == nullptr || !events->isArray()) {
    ImGui::TextDisabled("Trace does not contain traceEvents");
    ImGui::End();
    return;
  }
  ImGui::Text("%d events", static_cast<int>(events->size()));
  if (ImGui::BeginTable("trace-events", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed,
                            110.0f);
    ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed,
                            100.0f);
    ImGui::TableHeadersRow();
    for (const auto &event : *events) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(stringValue(event, "name").c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(stringValue(event, "cat").c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(
          (formatNumber(numberValue(event, "ts")) + " us").c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(
          (formatNumber(numberValue(event, "dur")) + " us").c_str());
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

struct ShadowTreeInspectorState {
  int selectedTag{0};
  bool showLabels{true};
  bool showBounds{false};
  bool showTrace{false};
  bool showRawJson{false};
};

struct SimulatedRenderTexture {
  ImTextureID id{ImTextureID_Invalid};
  int pixelWidth{0};
  int pixelHeight{0};
  std::string backend;
  std::string error;

  bool valid() const {
    return id != ImTextureID_Invalid && pixelWidth > 0 && pixelHeight > 0;
  }
};

class SdlGpuTexture {
public:
  explicit SdlGpuTexture(SDL_GPUDevice *device) : device_(device) {}

  SdlGpuTexture(const SdlGpuTexture &) = delete;
  SdlGpuTexture &operator=(const SdlGpuTexture &) = delete;

  ~SdlGpuTexture() { reset(); }

  bool upload(const std::uint8_t *rgba, int width, int height,
              std::size_t rowBytes, std::string &error) {
    if (rgba == nullptr || width <= 0 || height <= 0 ||
        rowBytes < static_cast<std::size_t>(width) * 4) {
      error = "invalid RGBA frame";
      return false;
    }
    const auto packedRowBytes = static_cast<std::size_t>(width) * 4;
    const auto byteCount = packedRowBytes * static_cast<std::size_t>(height);
    if (byteCount > std::numeric_limits<Uint32>::max()) {
      error = "RGBA frame exceeds SDL_GPU transfer-buffer limits";
      return false;
    }

    if (texture_ == nullptr || width_ != width || height_ != height) {
      if (texture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, texture_);
      }
      const SDL_GPUTextureCreateInfo textureInfo{
          .type = SDL_GPU_TEXTURETYPE_2D,
          .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
          .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
          .width = static_cast<Uint32>(width),
          .height = static_cast<Uint32>(height),
          .layer_count_or_depth = 1,
          .num_levels = 1,
          .sample_count = SDL_GPU_SAMPLECOUNT_1,
          .props = 0,
      };
      texture_ = SDL_CreateGPUTexture(device_, &textureInfo);
      if (texture_ == nullptr) {
        width_ = 0;
        height_ = 0;
        error = std::string("SDL_CreateGPUTexture failed: ") + SDL_GetError();
        return false;
      }
      width_ = width;
      height_ = height;
    }

    if (transferBuffer_ == nullptr || transferCapacity_ < byteCount) {
      if (transferBuffer_ != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer_);
      }
      const SDL_GPUTransferBufferCreateInfo transferInfo{
          .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
          .size = static_cast<Uint32>(byteCount),
          .props = 0,
      };
      transferBuffer_ = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
      if (transferBuffer_ == nullptr) {
        transferCapacity_ = 0;
        error = std::string("SDL_CreateGPUTransferBuffer failed: ") +
                SDL_GetError();
        return false;
      }
      transferCapacity_ = byteCount;
    }

    auto *mapped = static_cast<std::uint8_t *>(
        SDL_MapGPUTransferBuffer(device_, transferBuffer_, true));
    if (mapped == nullptr) {
      error = std::string("SDL_MapGPUTransferBuffer failed: ") + SDL_GetError();
      return false;
    }
    for (int row = 0; row < height; ++row) {
      std::memcpy(mapped + static_cast<std::size_t>(row) * packedRowBytes,
                  rgba + static_cast<std::size_t>(row) * rowBytes,
                  packedRowBytes);
    }
    SDL_UnmapGPUTransferBuffer(device_, transferBuffer_);

    auto *commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr) {
      error =
          std::string("SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError();
      return false;
    }
    auto *copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr) {
      SDL_CancelGPUCommandBuffer(commandBuffer);
      error = std::string("SDL_BeginGPUCopyPass failed: ") + SDL_GetError();
      return false;
    }
    const SDL_GPUTextureTransferInfo source{
        .transfer_buffer = transferBuffer_,
        .offset = 0,
        .pixels_per_row = static_cast<Uint32>(width),
        .rows_per_layer = static_cast<Uint32>(height),
    };
    const SDL_GPUTextureRegion destination{
        .texture = texture_,
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = static_cast<Uint32>(width),
        .h = static_cast<Uint32>(height),
        .d = 1,
    };
    SDL_UploadToGPUTexture(copyPass, &source, &destination, true);
    SDL_EndGPUCopyPass(copyPass);
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
      error =
          std::string("SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError();
      return false;
    }
    error.clear();
    return true;
  }

  void reset() {
    if (transferBuffer_ != nullptr) {
      SDL_ReleaseGPUTransferBuffer(device_, transferBuffer_);
      transferBuffer_ = nullptr;
      transferCapacity_ = 0;
    }
    if (texture_ != nullptr) {
      SDL_ReleaseGPUTexture(device_, texture_);
      texture_ = nullptr;
      width_ = 0;
      height_ = 0;
    }
  }

  ImTextureID id() const {
    return texture_ == nullptr
               ? ImTextureID_Invalid
               : static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture_));
  }

private:
  SDL_GPUDevice *device_{nullptr};
  SDL_GPUTexture *texture_{nullptr};
  SDL_GPUTransferBuffer *transferBuffer_{nullptr};
  std::size_t transferCapacity_{0};
  int width_{0};
  int height_{0};
};

static const folly::dynamic *shadowTreeNodes(const folly::dynamic &metrics) {
  const auto *shadowTree = field(metrics, "shadowTree");
  if (shadowTree == nullptr) {
    return nullptr;
  }
  const auto *nodes = field(*shadowTree, "nodes");
  return nodes != nullptr && nodes->isArray() ? nodes : nullptr;
}

static const folly::dynamic *mountedViewNodes(const folly::dynamic &metrics) {
  const auto *mountedTree = field(metrics, "mountedViewTree");
  if (mountedTree == nullptr) {
    return nullptr;
  }
  const auto *nodes = field(*mountedTree, "nodes");
  return nodes != nullptr && nodes->isArray() ? nodes : nullptr;
}

static int nodeTag(const folly::dynamic &node) {
  return static_cast<int>(numberValue(node, "tag"));
}

static const folly::dynamic *nodeLayout(const folly::dynamic &node) {
  const auto *layout = field(node, "layout");
  return layout != nullptr && layout->isObject() ? layout : nullptr;
}

static bool hasParentTag(const folly::dynamic &node, int parentTag) {
  const auto *parent = field(node, "parentTag");
  return parent != nullptr && parent->isNumber() &&
         parent->asInt() == parentTag;
}

static bool hasChildren(const folly::dynamic &nodes, int tag) {
  return std::any_of(nodes.begin(), nodes.end(), [tag](const auto &node) {
    return hasParentTag(node, tag);
  });
}

static const folly::dynamic *findNode(const folly::dynamic &nodes, int tag) {
  for (const auto &node : nodes) {
    if (nodeTag(node) == tag) {
      return &node;
    }
  }
  return nullptr;
}

static void drawShadowNodeRow(const folly::dynamic &node,
                              const folly::dynamic &nodes,
                              ShadowTreeInspectorState &state) {
  const auto tag = nodeTag(node);
  const auto children = hasChildren(nodes, tag);
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow |
               ImGuiTreeNodeFlags_SpanAvailWidth |
               ImGuiTreeNodeFlags_DefaultOpen |
               ImGuiTreeNodeFlags_DrawLinesToNodes;
  if (!children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (state.selectedTag == tag) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  const auto component = stringValue(node, "componentName", "Unknown");
  std::string frame;
  if (const auto *layout = nodeLayout(node)) {
    frame = "  [" + formatNumber(numberValue(*layout, "x"), 0) + ", " +
            formatNumber(numberValue(*layout, "y"), 0) + ", " +
            formatNumber(numberValue(*layout, "width"), 0) + " x " +
            formatNumber(numberValue(*layout, "height"), 0) + "]";
  }
  const auto open = ImGui::TreeNodeEx(
      reinterpret_cast<void *>(static_cast<intptr_t>(tag)), flags, "%s #%d%s",
      component.c_str(), tag, frame.c_str());
  if (ImGui::IsItemClicked()) {
    state.selectedTag = tag;
  }
  if (children && open) {
    for (const auto &child : nodes) {
      if (hasParentTag(child, tag)) {
        drawShadowNodeRow(child, nodes, state);
      }
    }
    ImGui::TreePop();
  }
}

static void drawSelectedShadowNode(const folly::dynamic &nodes,
                                   ShadowTreeInspectorState &state) {
  const auto *selected = findNode(nodes, state.selectedTag);
  if (selected == nullptr) {
    return;
  }
  ImGui::SeparatorText("Selected node");
  drawValue("Component", stringValue(*selected, "componentName"));
  drawValue("Tag", std::to_string(nodeTag(*selected)));
  drawValue("Native ID", stringValue(*selected, "nativeId", ""));
  if (const auto *layout = nodeLayout(*selected)) {
    drawValue("Relative frame",
              formatNumber(numberValue(*layout, "x"), 1) + ", " +
                  formatNumber(numberValue(*layout, "y"), 1) + "  " +
                  formatNumber(numberValue(*layout, "width"), 1) + " x " +
                  formatNumber(numberValue(*layout, "height"), 1));
    drawValue("Absolute origin",
              formatNumber(numberValue(*layout, "absoluteX"), 1) + ", " +
                  formatNumber(numberValue(*layout, "absoluteY"), 1));
    drawValue("Display", stringValue(*layout, "display"));
    drawValue("Position", stringValue(*layout, "position"));
  }
  if (const auto *props = field(*selected, "props")) {
    drawValue("Opacity", formatNumber(numberValue(*props, "opacity"), 2));
    drawValue("Collapsable",
              booleanValue(*props, "collapsable", true) ? "true" : "false");
    const auto *zIndex = field(*props, "zIndex");
    drawValue("zIndex", zIndex != nullptr && zIndex->isNumber()
                            ? std::to_string(zIndex->asInt())
                            : "auto");
    const auto *custom = field(*props, "custom");
    if (custom != nullptr && custom->isObject()) {
      drawValue("Custom value", formatNumber(numberValue(*custom, "value"), 0));
      drawValue("Custom label", stringValue(*custom, "label", ""));
    }
    const auto text = stringValue(*props, "text", "");
    if (!text.empty()) {
      drawValue("Text", text);
      drawValue("Font size", formatNumber(numberValue(*props, "fontSize"), 1));
    }
    const auto *scroll = field(*props, "scroll");
    if (scroll != nullptr && scroll->isObject()) {
      drawValue("Scroll offset",
                formatNumber(numberValue(*scroll, "offsetX"), 1) + ", " +
                    formatNumber(numberValue(*scroll, "offsetY"), 1));
      drawValue("Scroll content",
                formatNumber(numberValue(*scroll, "contentWidth"), 1) + " x " +
                    formatNumber(numberValue(*scroll, "contentHeight"), 1));
    }
  }
}

static void drawShadowTree(const folly::dynamic &metrics,
                           ShadowTreeInspectorState &state) {
  ImGui::Begin("ShadowNode tree");
  const auto *shadowTree = field(metrics, "shadowTree");
  const auto *nodes = shadowTreeNodes(metrics);
  if (shadowTree == nullptr || nodes == nullptr || nodes->empty()) {
    ImGui::TextDisabled("No non-empty React ShadowTree snapshot");
    ImGui::TextWrapped(
        "Run a workload that mounts a React component through Fabric. The "
        "runtime preserves the last non-empty commit before unmount.");
    ImGui::End();
    return;
  }
  ImGui::Text("Surface %d  Revision %lld  %d nodes",
              static_cast<int>(numberValue(*shadowTree, "surfaceId")),
              static_cast<long long>(numberValue(*shadowTree, "revision")),
              static_cast<int>(nodes->size()));
  ImGui::Separator();
  if (state.selectedTag == 0) {
    state.selectedTag = static_cast<int>(numberValue(*shadowTree, "rootTag"));
  }
  const auto hierarchyHeight =
      std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.58f);
  ImGui::BeginChild("shadow-tree-hierarchy", ImVec2(0, hierarchyHeight),
                    ImGuiChildFlags_Borders);
  for (const auto &node : *nodes) {
    const auto *parent = field(node, "parentTag");
    if (parent == nullptr || parent->isNull()) {
      drawShadowNodeRow(node, *nodes, state);
    }
  }
  ImGui::EndChild();
  drawSelectedShadowNode(*nodes, state);
  ImGui::End();
}

static ImU32 fallbackNodeColor(const folly::dynamic &node, int alpha) {
  const auto component = stringValue(node, "componentName", "Unknown");
  const auto hash = std::hash<std::string>{}(component) ^
                    static_cast<std::size_t>(nodeTag(node) * 0x9e3779b9U);
  const auto red = 75 + static_cast<int>(hash & 0x5f);
  const auto green = 85 + static_cast<int>((hash >> 8) & 0x6f);
  const auto blue = 105 + static_cast<int>((hash >> 16) & 0x7f);
  return IM_COL32(red, green, blue, alpha);
}

static ImU32 nodeFillColor(const folly::dynamic &node) {
  const auto *props = field(node, "props");
  const auto *background =
      props == nullptr ? nullptr : field(*props, "backgroundColor");
  if (background == nullptr || !background->isObject()) {
    return IM_COL32(0, 0, 0, 0);
  }
  const auto channel = [background](const char *name) {
    return std::clamp(static_cast<int>(numberValue(*background, name) * 255.0),
                      0, 255);
  };
  const auto opacity =
      props == nullptr ? 1.0 : numberValue(*props, "opacity", 1.0);
  const auto alpha =
      std::clamp(static_cast<int>(numberValue(*background, "alpha", 1.0) *
                                  opacity * 255.0),
                 0, 255);
  return IM_COL32(channel("red"), channel("green"), channel("blue"), alpha);
}

static ImU32 nodeTextColor(const folly::dynamic &node) {
  const auto *props = field(node, "props");
  const auto *color = props == nullptr ? nullptr : field(*props, "textColor");
  if (color == nullptr || !color->isObject()) {
    return IM_COL32(242, 244, 248, 255);
  }
  const auto channel = [color](const char *name) {
    return std::clamp(static_cast<int>(numberValue(*color, name, 1.0) * 255.0),
                      0, 255);
  };
  return IM_COL32(channel("red"), channel("green"), channel("blue"),
                  channel("alpha"));
}

static ImVec2 presentationOrigin(const folly::dynamic &node,
                                 const folly::dynamic &nodes) {
  const auto *layout = nodeLayout(node);
  ImVec2 origin(static_cast<float>(numberValue(*layout, "absoluteX")),
                static_cast<float>(numberValue(*layout, "absoluteY")));
  const folly::dynamic *current = &node;
  while (const auto *parentTag = field(*current, "parentTag")) {
    if (!parentTag->isNumber()) {
      break;
    }
    const auto *parent = findNode(nodes, parentTag->asInt());
    if (parent == nullptr) {
      break;
    }
    if (const auto *props = field(*parent, "props")) {
      if (const auto *scroll = field(*props, "scroll");
          scroll != nullptr && scroll->isObject()) {
        origin.x -= static_cast<float>(numberValue(*scroll, "offsetX"));
        origin.y -= static_cast<float>(numberValue(*scroll, "offsetY"));
      }
    }
    current = parent;
  }
  return origin;
}

static void drawSimulatedRender(const folly::dynamic &metrics,
                                ShadowTreeInspectorState &state,
                                const SimulatedRenderTexture &renderedTexture) {
  ImGui::Begin("Simulated render");
  ImGui::TextDisabled("%s", renderedTexture.backend.empty()
                                ? "Fabric mount -> retained preview"
                                : renderedTexture.backend.c_str());
  ImGui::SameLine(0.0f, 16.0f);
  if (!renderedTexture.valid()) {
    ImGui::Checkbox("Labels", &state.showLabels);
    ImGui::SameLine();
  }
  ImGui::Checkbox("Bounds", &state.showBounds);
  if (!renderedTexture.error.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(
        ReactNativeSimulator::imgui_theme::palette().danger, "%s",
        renderedTexture.error.c_str());
  }

  const auto *nodes = mountedViewNodes(metrics);
  if (nodes == nullptr || nodes->empty()) {
    ImGui::TextDisabled("No mounted native view tree to render");
    ImGui::End();
    return;
  }

  const folly::dynamic *rootNode = nullptr;
  for (const auto &node : *nodes) {
    const auto *parent = field(node, "parentTag");
    if (parent == nullptr || parent->isNull()) {
      rootNode = &node;
      break;
    }
  }
  const auto *rootLayout =
      rootNode == nullptr ? nullptr : nodeLayout(*rootNode);
  const auto rootOrigin = rootNode == nullptr
                              ? ImVec2(0, 0)
                              : presentationOrigin(*rootNode, *nodes);
  const auto minX = rootOrigin.x;
  const auto minY = rootOrigin.y;
  const auto maxX =
      rootLayout == nullptr
          ? minX
          : minX + static_cast<float>(numberValue(*rootLayout, "width"));
  const auto maxY =
      rootLayout == nullptr
          ? minY
          : minY + static_cast<float>(numberValue(*rootLayout, "height"));
  if (minX > maxX || minY > maxY) {
    ImGui::TextDisabled("ShadowTree has no valid layout frames");
    ImGui::End();
    return;
  }

  const auto canvasOrigin = ImGui::GetCursorScreenPos();
  const auto available = ImGui::GetContentRegionAvail();
  const ImVec2 canvasSize(std::max(available.x, 100.0f),
                          std::max(available.y, 100.0f));
  ImGui::InvisibleButton("shadow-render-canvas", canvasSize);
  auto *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(
      canvasOrigin,
      ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y),
      IM_COL32(18, 22, 30, 255), 4.0f);
  drawList->AddRect(
      canvasOrigin,
      ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y),
      IM_COL32(65, 75, 92, 255), 4.0f);

  constexpr float padding = 18.0f;
  const auto contentWidth = std::max(maxX - minX, 1.0f);
  const auto contentHeight = std::max(maxY - minY, 1.0f);
  const auto scale =
      std::max(0.05f, std::min((canvasSize.x - padding * 2) / contentWidth,
                               (canvasSize.y - padding * 2) / contentHeight));
  const auto renderedWidth = contentWidth * scale;
  const auto renderedHeight = contentHeight * scale;
  const auto contentOffset =
      ImVec2(std::max((canvasSize.x - renderedWidth) * 0.5f, padding),
             std::max((canvasSize.y - renderedHeight) * 0.5f, padding));
  const auto mapPoint = [&](float x, float y) {
    return ImVec2(canvasOrigin.x + contentOffset.x + (x - minX) * scale,
                  canvasOrigin.y + contentOffset.y + (y - minY) * scale);
  };

  const auto viewportTopLeft = mapPoint(minX, minY);
  const auto viewportBottomRight = mapPoint(maxX, maxY);
  if (renderedTexture.valid()) {
    drawList->AddImage(renderedTexture.id, viewportTopLeft, viewportBottomRight,
                       ImVec2(0, 0), ImVec2(1, 1));
  }
  drawList->PushClipRect(viewportTopLeft, viewportBottomRight, true);
  for (const auto &node : *nodes) {
    const auto *layout = nodeLayout(node);
    if (layout == nullptr || !booleanValue(node, "layoutable") ||
        stringValue(*layout, "display") == "none") {
      continue;
    }
    const auto origin = presentationOrigin(node, *nodes);
    const auto x = origin.x;
    const auto y = origin.y;
    const auto width = static_cast<float>(numberValue(*layout, "width"));
    const auto height = static_cast<float>(numberValue(*layout, "height"));
    if (width <= 0 || height <= 0) {
      continue;
    }
    const auto topLeft = mapPoint(x, y);
    const auto bottomRight = mapPoint(x + width, y + height);
    const auto *props = field(node, "props");
    const auto borderRadius =
        props == nullptr
            ? 0.0f
            : static_cast<float>(numberValue(*props, "borderRadius"));
    if (!renderedTexture.valid()) {
      drawList->AddRectFilled(topLeft, bottomRight, nodeFillColor(node),
                              std::max(borderRadius * scale, 0.0f));
    }
    if (state.showBounds) {
      drawList->AddRect(
          topLeft, bottomRight,
          state.selectedTag == nodeTag(node) ? IM_COL32(255, 214, 90, 255)
                                             : fallbackNodeColor(node, 220),
          2.0f, 0, state.selectedTag == nodeTag(node) ? 2.5f : 1.0f);
    }
    if (!renderedTexture.valid() && state.showLabels &&
        bottomRight.y - topLeft.y > 8.0f) {
      const auto text = props == nullptr ? "" : stringValue(*props, "text", "");
      if (text.empty() && !state.showBounds) {
        continue;
      }
      const auto label = text.empty() ? stringValue(node, "componentName") +
                                            " #" + std::to_string(nodeTag(node))
                                      : text;
      const auto fontSize =
          text.empty()
              ? 12.0f
              : static_cast<float>(numberValue(*props, "fontSize", 14.0));
      const auto fontWeight =
          props == nullptr
              ? 400
              : static_cast<int>(numberValue(*props, "fontWeight", 400));
      auto &fonts = ImGui::GetIO().Fonts->Fonts;
      ImFont *font =
          fontWeight >= 600 && fonts.Size > 1 ? fonts[1] : ImGui::GetFont();
      const ImVec4 clipRect(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
      drawList->AddText(
          font, std::clamp(fontSize * scale, 1.0f, 72.0f),
          ImVec2(topLeft.x + scale, topLeft.y + 2.0f * scale),
          text.empty() ? IM_COL32(235, 240, 250, 255) : nodeTextColor(node),
          label.c_str(), nullptr,
          std::max(bottomRight.x - topLeft.x - 2.0f * scale, 0.0f), &clipRect);
    }
  }
  drawList->PopClipRect();

  if (ImGui::IsItemHovered()) {
    const auto mouse = ImGui::GetIO().MousePos;
    const folly::dynamic *hovered = nullptr;
    for (std::size_t index = nodes->size(); index > 0; --index) {
      const auto &candidate = (*nodes)[index - 1];
      const auto *layout = nodeLayout(candidate);
      if (layout == nullptr || !booleanValue(candidate, "layoutable") ||
          stringValue(*layout, "display") == "none") {
        continue;
      }
      const auto origin = presentationOrigin(candidate, *nodes);
      const auto topLeft = mapPoint(origin.x, origin.y);
      const auto bottomRight = mapPoint(
          origin.x + static_cast<float>(numberValue(*layout, "width")),
          origin.y + static_cast<float>(numberValue(*layout, "height")));
      if (mouse.x >= topLeft.x && mouse.x <= bottomRight.x &&
          mouse.y >= topLeft.y && mouse.y <= bottomRight.y) {
        hovered = &candidate;
        break;
      }
    }
    if (hovered != nullptr) {
      const auto *layout = nodeLayout(*hovered);
      ImGui::BeginTooltip();
      ImGui::Text("%s #%d", stringValue(*hovered, "componentName").c_str(),
                  nodeTag(*hovered));
      ImGui::Text(
          "frame %.1f, %.1f  %.1f x %.1f", numberValue(*layout, "absoluteX"),
          numberValue(*layout, "absoluteY"), numberValue(*layout, "width"),
          numberValue(*layout, "height"));
      ImGui::EndTooltip();
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state.selectedTag = nodeTag(*hovered);
      }
    }
  }
  ImGui::End();
}

static void drawRawJson(const Document &metrics, const Document &trace) {
  ImGui::Begin("Raw JSON");
  if (ImGui::BeginTabBar("raw-json-tabs")) {
    if (ImGui::BeginTabItem("Metrics")) {
      ImGui::BeginChild("metrics-json", ImVec2(0, 0), ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar);
      ImGui::TextUnformatted(metrics.raw.c_str());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Trace")) {
      ImGui::BeginChild("trace-json", ImVec2(0, 0), ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar);
      ImGui::TextUnformatted(trace.raw.c_str());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

struct Options {
  std::optional<std::filesystem::path> metricsPath;
  std::vector<std::filesystem::path> bundlePaths;
  std::optional<std::filesystem::path> runtimePath;
  std::optional<std::filesystem::path> tracePath;
  std::optional<std::filesystem::path> androidFontDir;
  std::vector<std::string> runtimeArguments;
};

static Options parseOptions(int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string name = argv[index];
    if (name == "--metrics" || name == "--trace" ||
        name == "--android-font-dir") {
      if (index + 1 >= argc) {
        throw std::invalid_argument(name + " requires a value");
      }
      const auto value = std::string(argv[++index]);
      if (name == "--metrics") {
        options.metricsPath = value;
      } else if (name == "--trace") {
        options.tracePath = value;
      } else if (name == "--android-font-dir") {
        options.androidFontDir = value;
      }
    } else if (name == "--help" || name == "-h") {
      std::cout
          << "Usage: react-native-simulator-inspector "
             "--metrics FILE [--trace FILE] "
             "[--android-font-dir DIR]\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown option: " + name);
    }
  }
  if (!options.metricsPath) {
    throw std::invalid_argument("--metrics is required for offline replay");
  }
  return options;
}

static std::filesystem::path executablePath() {
  std::vector<char> buffer(1024);
  uint32_t size = static_cast<uint32_t>(buffer.size());
  while (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    buffer.resize(size);
  }
  return std::filesystem::weakly_canonical(buffer.data());
}

class LiveSession final {
 public:
  static std::unique_ptr<LiveSession> start(const Options &options) {
    auto session = std::unique_ptr<LiveSession>(new LiveSession(options));
    session->launchRuntime();
    session->reader_ = std::thread([sessionPtr = session.get()] {
      sessionPtr->readLoop();
    });
    return session;
  }

  ~LiveSession() {
    stopped_.store(true);
    if (childPid_ > 0) {
      int status = 0;
      if (::waitpid(childPid_, &status, WNOHANG) == 0) {
        ::kill(childPid_, SIGTERM);
      }
    }
    closeFd(clientFd_);
    closeFd(listenerFd_);
    if (reader_.joinable()) {
      reader_.join();
    }
    if (childPid_ > 0) {
      int status = 0;
      while (::waitpid(childPid_, &status, 0) < 0 && errno == EINTR) {
      }
    }
    if (!socketPath_.empty()) {
      ::unlink(socketPath_.c_str());
    }
    if (!temporaryDirectory_.empty()) {
      ::rmdir(temporaryDirectory_.c_str());
    }
  }

  void drain(Document &metrics, std::string &state) {
    std::deque<std::string> messages;
    {
      std::lock_guard lock(mutex_);
      messages.swap(messages_);
    }
    for (const auto &raw : messages) {
      try {
        const auto message = folly::parseJson(raw);
        const auto type = stringValue(message, "type", "");
        if (type == "status") {
          const auto nextState = stringValue(message, "state", state);
          if (state != "error" && state != "complete") {
            state = nextState;
          }
          continue;
        }
        if (type == "snapshot") {
          const auto *value = field(message, "metrics");
          if (value != nullptr && value->isObject()) {
            metrics.value = *value;
            metrics.raw = folly::toJson(*value);
            metrics.loaded = true;
            metrics.error.clear();
            ++metrics.revision;
          }
          const auto *scene = field(message, "scene");
          if (scene != nullptr && scene->isObject()) {
            metrics.sceneValue = *scene;
            metrics.sceneLoaded = true;
            ++metrics.sceneRevision;
          }
          state = stringValue(message, "state", state);
          if (metrics.loaded) {
            metrics.value["runtimeState"] = state;
          }
          continue;
        }
        if (type == "exit") {
          const auto code = numberValue(message, "code", 1);
          state = code == 0 ? "complete" : "error";
          continue;
        }
        if (type == "error") {
          state = "error";
          metrics.error = stringValue(message, "message", "runtime error");
        }
      } catch (const std::exception &error) {
        state = "error";
        metrics.error = error.what();
      }
    }
  }

 private:
  explicit LiveSession(const Options &options) : options_(options) {
    std::string directoryTemplate =
        "/tmp/react-native-simulator-inspector-XXXXXX";
    std::vector<char> directoryBuffer(
        directoryTemplate.begin(), directoryTemplate.end());
    directoryBuffer.push_back('\0');
    const auto directory = ::mkdtemp(directoryBuffer.data());
    if (directory == nullptr) {
      throw std::runtime_error("cannot create inspector session directory");
    }
    temporaryDirectory_ = directory;
    socketPath_ = temporaryDirectory_ / "runtime.sock";
    if (socketPath_.string().size() >= sizeof(sockaddr_un::sun_path)) {
      throw std::runtime_error("inspector session socket path is too long");
    }
    listenerFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenerFd_ < 0) {
      throw std::runtime_error("cannot create inspector listener");
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(
        address.sun_path,
        socketPath_.c_str(),
        sizeof(address.sun_path) - 1);
    if (::bind(
            listenerFd_,
            reinterpret_cast<const sockaddr *>(&address),
            sizeof(address)) < 0 ||
        ::listen(listenerFd_, 1) < 0) {
      throw std::runtime_error(
          "cannot bind inspector listener: " +
          std::string(std::strerror(errno)));
    }
  }

  void launchRuntime() {
    const auto runtime = options_.runtimePath.value_or(
        executablePath().parent_path() / "rnsim");
    std::vector<std::string> storage;
    storage.push_back(runtime.string());
    for (std::size_t index = 0; index < options_.bundlePaths.size(); ++index) {
      if (index != 0) {
        storage.push_back("--bundle");
      }
      storage.push_back(options_.bundlePaths[index].string());
    }
    for (const auto &argument : options_.runtimeArguments) {
      storage.push_back(argument);
    }
    if (options_.tracePath) {
      storage.push_back("--trace");
      storage.push_back(options_.tracePath->string());
    }
    storage.push_back("--inspector-socket");
    storage.push_back(socketPath_.string());

    std::vector<char *> argv;
    argv.reserve(storage.size() + 1);
    for (auto &argument : storage) {
      argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    const auto result = ::posix_spawn(
        &childPid_, runtime.c_str(), nullptr, nullptr, argv.data(), environ);
    if (result != 0) {
      throw std::runtime_error(
          "cannot launch runtime: " + std::string(std::strerror(result)));
    }
  }

  void readLoop() {
    std::string buffer;
    while (!stopped_.load()) {
      if (clientFd_.load() < 0) {
        pollfd descriptor{.fd = listenerFd_.load(), .events = POLLIN};
        const auto ready = ::poll(&descriptor, 1, 100);
        if (ready < 0 && errno == EINTR) {
          continue;
        }
        if (ready > 0 && (descriptor.revents & POLLIN) != 0) {
          const auto client = ::accept(listenerFd_.load(), nullptr, nullptr);
          if (client >= 0) {
            clientFd_.store(client);
          }
        }
        continue;
      }

      pollfd descriptor{.fd = clientFd_.load(), .events = POLLIN};
      const auto ready = ::poll(&descriptor, 1, 100);
      if (ready < 0 && errno == EINTR) {
        continue;
      }
      if (ready <= 0) {
        continue;
      }
      if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        pushStatus("disconnected");
        return;
      }
      char chunk[16384];
      const auto count = ::recv(clientFd_.load(), chunk, sizeof(chunk), 0);
      if (count <= 0) {
        pushStatus("disconnected");
        return;
      }
      buffer.append(chunk, static_cast<std::size_t>(count));
      while (true) {
        const auto newline = buffer.find('\n');
        if (newline == std::string::npos) {
          break;
        }
        auto message = buffer.substr(0, newline);
        buffer.erase(0, newline + 1);
        if (!message.empty()) {
          std::lock_guard lock(mutex_);
          messages_.push_back(std::move(message));
        }
      }
    }
  }

  void pushStatus(const char *value) {
    std::lock_guard lock(mutex_);
    messages_.push_back(
        folly::toJson(folly::dynamic::object("type", "status")("state", value)));
  }

  static void closeFd(std::atomic<int> &fd) {
    const auto value = fd.exchange(-1);
    if (value >= 0) {
      ::close(value);
    }
  }

  const Options &options_;
  std::filesystem::path temporaryDirectory_;
  std::filesystem::path socketPath_;
  std::atomic<int> listenerFd_{-1};
  std::atomic<int> clientFd_{-1};
  pid_t childPid_{-1};
  std::atomic<bool> stopped_{false};
  std::thread reader_;
  std::mutex mutex_;
  std::deque<std::string> messages_;
};

} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = parseOptions(argc, argv);
    std::unique_ptr<LiveSession> liveSession;
    if (!options.bundlePaths.empty()) {
      liveSession = LiveSession::start(options);
    }
    Document metrics{
        .path = options.metricsPath.value_or(std::filesystem::path{})};
    Document trace{.path = options.tracePath.value_or(std::filesystem::path{})};
    reloadIfChanged(metrics, true);
    std::string liveState = liveSession ? "starting" : "replay";

    if (!options.tracePath && metrics.loaded) {
      const auto traceFile = stringValue(metrics.value, "traceFile", "");
      if (!traceFile.empty()) {
        trace.path = traceFile;
      }
    }
    reloadIfChanged(trace, true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImVector<ImWchar> glyphRanges;
    ImFontGlyphRangesBuilder glyphBuilder;
    glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    if (metrics.loaded) {
      if (const auto *nodes = mountedViewNodes(metrics.value)) {
        for (const auto &node : *nodes) {
          if (const auto *props = field(node, "props")) {
            const auto text = stringValue(*props, "text", "");
            if (!text.empty()) {
              glyphBuilder.AddText(text.c_str());
            }
          }
        }
      }
    }
    glyphBuilder.BuildRanges(&glyphRanges);
    const auto *viewport =
        metrics.loaded ? field(metrics.value, "viewport") : nullptr;
    const auto fontRasterizerDensity =
        viewport != nullptr
            ? std::clamp(static_cast<float>(
                             numberValue(*viewport, "pointScaleFactor", 1.0)),
                         1.0f, 4.0f)
            : 1.0f;
    const auto addFont = [&](const std::filesystem::path &path, int fontNumber,
                             bool merge) {
      ImFontConfig config;
      config.FontNo = fontNumber;
      config.MergeMode = merge;
      config.RasterizerDensity = fontRasterizerDensity;
      return io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &config,
                                          glyphRanges.Data);
    };
    if (options.androidFontDir) {
      const auto &fontDir = *options.androidFontDir;
      const auto staticRoboto = fontDir / "RobotoStatic-Regular.ttf";
      const auto variableRoboto = fontDir / "Roboto-Regular.ttf";
      const auto notoCjk = fontDir / "NotoSansCJK-Regular.ttc";
      if (!std::filesystem::exists(staticRoboto) ||
          !std::filesystem::exists(variableRoboto) ||
          !std::filesystem::exists(notoCjk)) {
        throw std::runtime_error(
            "--android-font-dir must contain RobotoStatic-Regular.ttf, "
            "Roboto-Regular.ttf, and NotoSansCJK-Regular.ttc");
      }
      addFont(staticRoboto, 0, false);
      addFont(notoCjk, 2, true);
      addFont(variableRoboto, 0, false);
      addFont(notoCjk, 2, true);
    } else {
      ReactNativeSimulator::imgui_theme::loadUiFonts(
          16.0f, fontRasterizerDensity, glyphRanges.Data);
    }
    ReactNativeSimulator::imgui_theme::apply();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      throw std::runtime_error(std::string("SDL_Init failed: ") +
                               SDL_GetError());
    }
    applySimulatorAppIcon(nullptr);
    SDL_Window *window =
        SDL_CreateWindow("react-native-simulator inspector", 1440, 900,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
      throw std::runtime_error(std::string("SDL_CreateWindow failed: ") +
                               SDL_GetError());
    }
    applySimulatorAppIcon(window);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);

#if defined(NDEBUG)
    constexpr bool enableGpuDebug = false;
#else
    constexpr bool enableGpuDebug = true;
#endif
    auto *gpuDevice = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
        enableGpuDebug, nullptr);
    if (gpuDevice == nullptr) {
      SDL_DestroyWindow(window);
      throw std::runtime_error(std::string("SDL_CreateGPUDevice failed: ") +
                               SDL_GetError());
    }
    const std::string gpuDriver = SDL_GetGPUDeviceDriver(gpuDevice);
    if (!SDL_ClaimWindowForGPUDevice(gpuDevice, window)) {
      SDL_DestroyGPUDevice(gpuDevice);
      SDL_DestroyWindow(window);
      throw std::runtime_error(
          std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError());
    }
    if (!SDL_SetGPUSwapchainParameters(gpuDevice, window,
                                       SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       SDL_GPU_PRESENTMODE_VSYNC)) {
      SDL_ReleaseWindowFromGPUDevice(gpuDevice, window);
      SDL_DestroyGPUDevice(gpuDevice);
      SDL_DestroyWindow(window);
      throw std::runtime_error(
          std::string("SDL_SetGPUSwapchainParameters failed: ") +
          SDL_GetError());
    }

    ImGui_ImplSDLGPU3_InitInfo initInfo{};
    initInfo.Device = gpuDevice;
    initInfo.ColorTargetFormat =
        SDL_GetGPUSwapchainTextureFormat(gpuDevice, window);
    initInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    initInfo.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    initInfo.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!ImGui_ImplSDL3_InitForSDLGPU(window) ||
        !ImGui_ImplSDLGPU3_Init(&initInfo)) {
      SDL_ReleaseWindowFromGPUDevice(gpuDevice, window);
      SDL_DestroyGPUDevice(gpuDevice);
      SDL_DestroyWindow(window);
      throw std::runtime_error(
          std::string(
              "failed to initialize SDL3 + SDL_GPU Dear ImGui backends: ") +
          SDL_GetError());
    }

    SimulatedRenderTexture simulatedRenderTexture;
#if RNS_ENABLE_SKIA
    ReactNativeSimulator::SkiaMountedTreeRenderer skiaRenderer(
        options.androidFontDir.value_or(std::filesystem::path{}));
    SdlGpuTexture skiaTexture(gpuDevice);
    std::uint64_t skiaMetricsRevision = 0;
    std::uint64_t skiaSceneRevision = 0;
#else
    simulatedRenderTexture.backend =
        "ImGui fallback renderer / SDL_GPU (" + gpuDriver + ")";
#endif

    bool done = false;
    ShadowTreeInspectorState shadowTreeState;
    auto nextReload = std::chrono::steady_clock::now();
    while (!done) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(window))) {
          done = true;
        }
      }
      if (done) {
        continue;
      }
      if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
        SDL_Delay(10);
        continue;
      }

      const auto now = std::chrono::steady_clock::now();
      if (now >= nextReload) {
        reloadIfChanged(metrics);
        if (!options.tracePath && metrics.loaded) {
          const auto traceFile = stringValue(metrics.value, "traceFile", "");
          if (trace.path != traceFile) {
            trace.path = traceFile;
            trace.hasModifiedAt = false;
          }
        }
        reloadIfChanged(trace);
        nextReload = now + std::chrono::milliseconds(500);
      }

      if (liveSession) {
        liveSession->drain(metrics, liveState);
        if (!options.tracePath && metrics.loaded) {
          const auto traceFile = stringValue(metrics.value, "traceFile", "");
          if (!traceFile.empty() && trace.path != traceFile) {
            trace.path = traceFile;
            trace.hasModifiedAt = false;
          }
        }
      }

#if RNS_ENABLE_SKIA
      if (metrics.loaded &&
          ((metrics.sceneLoaded && metrics.sceneRevision != skiaSceneRevision) ||
           (!metrics.sceneLoaded && metrics.revision != skiaMetricsRevision))) {
        auto frame = metrics.sceneLoaded
            ? skiaRenderer.renderSceneWire(metrics.sceneValue)
            : skiaRenderer.render(metrics.value);
        simulatedRenderTexture.error = frame.error;
        simulatedRenderTexture.backend =
            "Skia raster + SkParagraph / SDL_GPU (" + gpuDriver + ")";
        if (frame) {
          if (skiaTexture.upload(frame.rgba.data(), frame.width, frame.height,
                                 frame.rowBytes,
                                 simulatedRenderTexture.error)) {
            simulatedRenderTexture.id = skiaTexture.id();
            simulatedRenderTexture.pixelWidth = frame.width;
            simulatedRenderTexture.pixelHeight = frame.height;
          } else {
            simulatedRenderTexture.id = ImTextureID_Invalid;
            simulatedRenderTexture.pixelWidth = 0;
            simulatedRenderTexture.pixelHeight = 0;
          }
        } else {
          skiaTexture.reset();
          simulatedRenderTexture.id = ImTextureID_Invalid;
          simulatedRenderTexture.pixelWidth = 0;
          simulatedRenderTexture.pixelHeight = 0;
        }
        skiaMetricsRevision = metrics.revision;
        skiaSceneRevision = metrics.sceneRevision;
      }
#endif

      ImGui_ImplSDLGPU3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();

      if (ImGui::BeginMainMenuBar()) {
        ImGui::TextUnformatted("react-native-simulator");
        ImGui::SameLine(0.0f, 12.0f);
        namespace theme = ReactNativeSimulator::imgui_theme;
        if (liveSession) {
          theme::toneChip(liveState.c_str(), theme::ChipTone::Success, true);
        } else {
          theme::toneChip("replay", theme::ChipTone::Accent, true);
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (ImGui::BeginMenu("View")) {
          ImGui::MenuItem("Trace events", nullptr, &shadowTreeState.showTrace);
          ImGui::MenuItem("Raw JSON", nullptr, &shadowTreeState.showRawJson);
          ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
      }

      const auto display = io.DisplaySize;
      constexpr float margin = 8.0f;
      const auto contentTop = ImGui::GetFrameHeight() + margin;
      const auto contentHeight =
          std::max(display.y - contentTop - margin, 200.0f);
      const auto leftWidth = std::clamp(display.x * 0.22f, 280.0f, 340.0f);
      const auto middleWidth = std::clamp(display.x * 0.29f, 360.0f, 440.0f);
      const auto rightX = margin + leftWidth + margin + middleWidth + margin;
      const auto rightWidth = std::max(display.x - rightX - margin, 360.0f);
      const auto setPanel = [](float x, float y, float width, float height) {
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
      };

      const auto overviewHeight =
          std::clamp(contentHeight * 0.36f, 285.0f, 340.0f);
      const auto timingHeight =
          std::clamp(contentHeight * 0.40f, 315.0f, 370.0f);
      const auto bundlesHeight = std::max(
          contentHeight - overviewHeight - timingHeight - margin * 2, 150.0f);
      if (metrics.loaded) {
        setPanel(margin, contentTop, leftWidth, overviewHeight);
        drawOverview(metrics.value);
        setPanel(margin, contentTop + overviewHeight + margin, leftWidth,
                 timingHeight);
        drawTimingAndResources(metrics.value);
        setPanel(margin,
                 contentTop + overviewHeight + timingHeight + margin * 2,
                 leftWidth, bundlesHeight);
        drawBundles(metrics.value);

        setPanel(margin + leftWidth + margin, contentTop, middleWidth,
                 contentHeight);
        drawShadowTree(metrics.value, shadowTreeState);

        const auto traceHeight =
            shadowTreeState.showTrace
                ? std::clamp(contentHeight * 0.24f, 170.0f, 220.0f)
                : 0.0f;
        const auto previewHeight =
            contentHeight -
            (shadowTreeState.showTrace ? traceHeight + margin : 0.0f);
        setPanel(rightX, contentTop, rightWidth, previewHeight);
        drawSimulatedRender(metrics.value, shadowTreeState,
                            simulatedRenderTexture);
        if (shadowTreeState.showTrace) {
          setPanel(rightX, contentTop + previewHeight + margin, rightWidth,
                   traceHeight);
          drawTrace(trace);
        }
      } else {
        setPanel(margin, contentTop, display.x - margin * 2, contentHeight);
        ImGui::Begin("Runtime overview");
        if (liveSession) {
          ImGui::Text("Starting runtime (%s)", liveState.c_str());
          if (!metrics.error.empty()) {
            ImGui::TextWrapped("%s", metrics.error.c_str());
          }
        } else {
          ImGui::TextColored(
              ReactNativeSimulator::imgui_theme::palette().danger,
              "Metrics unavailable");
          ImGui::TextWrapped("%s", metrics.error.c_str());
        }
        ImGui::End();
      }
      if (shadowTreeState.showRawJson) {
        const auto rawWidth = std::max(display.x * 0.72f, 640.0f);
        const auto rawHeight = std::max(display.y * 0.72f, 480.0f);
        setPanel((display.x - rawWidth) * 0.5f, (display.y - rawHeight) * 0.5f,
                 rawWidth, rawHeight);
        ImGui::SetNextWindowFocus();
        drawRawJson(metrics, trace);
      }
      ImGui::Render();
      auto *drawData = ImGui::GetDrawData();
      auto *commandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);
      if (commandBuffer == nullptr) {
        throw std::runtime_error(
            std::string("SDL_AcquireGPUCommandBuffer failed: ") +
            SDL_GetError());
      }
      SDL_GPUTexture *swapchainTexture = nullptr;
      if (!SDL_WaitAndAcquireGPUSwapchainTexture(
              commandBuffer, window, &swapchainTexture, nullptr, nullptr)) {
        SDL_CancelGPUCommandBuffer(commandBuffer);
        throw std::runtime_error(
            std::string("SDL_WaitAndAcquireGPUSwapchainTexture failed: ") +
            SDL_GetError());
      }
      const bool drawable = swapchainTexture != nullptr &&
                            drawData->DisplaySize.x > 0.0f &&
                            drawData->DisplaySize.y > 0.0f;
      if (drawable) {
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commandBuffer);
        const SDL_GPUColorTargetInfo targetInfo{
            .texture = swapchainTexture,
            .mip_level = 0,
            .layer_or_depth_plane = 0,
            .clear_color = SDL_FColor{0.055f, 0.065f, 0.085f, 1.0f},
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
            .resolve_texture = nullptr,
            .resolve_mip_level = 0,
            .resolve_layer = 0,
            .cycle = false,
            .cycle_resolve_texture = false,
        };
        auto *renderPass =
            SDL_BeginGPURenderPass(commandBuffer, &targetInfo, 1, nullptr);
        if (renderPass == nullptr) {
          SDL_CancelGPUCommandBuffer(commandBuffer);
          throw std::runtime_error(
              std::string("SDL_BeginGPURenderPass failed: ") + SDL_GetError());
        }
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, renderPass);
        SDL_EndGPURenderPass(renderPass);
      }
      if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        throw std::runtime_error(
            std::string("SDL_SubmitGPUCommandBuffer failed: ") +
            SDL_GetError());
      }
    }

    SDL_WaitForGPUIdle(gpuDevice);
#if RNS_ENABLE_SKIA
    skiaTexture.reset();
#endif
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(gpuDevice, window);
    SDL_DestroyGPUDevice(gpuDevice);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "react-native-simulator-inspector failed: " << error.what()
              << '\n';
    SDL_Quit();
    return 1;
  }
}
