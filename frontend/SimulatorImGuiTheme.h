#pragma once

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace ReactNativeSimulator {
namespace imgui_theme {

// Host chrome tokens. Source of truth: docs/design/CHROME_STYLE.md (Linear ladder, Raycast
// keyboard chrome, Expo device bezel, Apple SF Pro).

inline ImVec4 rgba(int r, int g, int b, int a = 255) {
  return {
      static_cast<float>(r) / 255.0f,
      static_cast<float>(g) / 255.0f,
      static_cast<float>(b) / 255.0f,
      static_cast<float>(a) / 255.0f};
}

inline ImU32 u32(int r, int g, int b, int a = 255) {
  return IM_COL32(r, g, b, a);
}

inline ImU32 vecToU32(const ImVec4& color) {
  return ImGui::ColorConvertFloat4ToU32(color);
}

inline ImU32 wash(const ImVec4& color, int alpha) {
  return IM_COL32(
      static_cast<int>(color.x * 255.0f + 0.5f),
      static_cast<int>(color.y * 255.0f + 0.5f),
      static_cast<int>(color.z * 255.0f + 0.5f),
      alpha);
}

inline ImVec4 washVec(const ImVec4& color, int alpha) {
  return {color.x, color.y, color.z, static_cast<float>(alpha) / 255.0f};
}

struct Palette {
  ImVec4 bg{rgba(7, 8, 10)};
  ImVec4 window{rgba(12, 13, 16)};
  ImVec4 panel{rgba(16, 17, 20)};
  ImVec4 panelAlt{rgba(13, 14, 17)};
  ImVec4 elevated{rgba(22, 23, 28)};
  ImVec4 frame{rgba(28, 30, 38)};
  ImVec4 frameHover{rgba(37, 40, 51)};
  ImVec4 border{rgba(35, 37, 42)};
  ImVec4 hairlineStrong{rgba(52, 52, 58)};
  ImVec4 text{rgba(247, 248, 248)};
  ImVec4 muted{rgba(138, 143, 152)};
  ImVec4 accent{rgba(94, 106, 210)};
  ImVec4 accentHover{rgba(130, 143, 255)};
  ImVec4 accentActive{rgba(79, 91, 192)};
  ImVec4 onAccent{rgba(255, 255, 255)};
  ImVec4 inspect{rgba(245, 197, 66)};
  ImVec4 success{rgba(74, 222, 128)};
  ImVec4 danger{rgba(240, 113, 120)};
  ImVec4 dangerHover{rgba(248, 138, 144)};
  ImVec4 info{rgba(87, 193, 255)};
};

inline const Palette& palette() {
  static const Palette kPalette;
  return kPalette;
}

inline int canvasR() {
  return 7;
}
inline int canvasG() {
  return 8;
}
inline int canvasB() {
  return 10;
}

inline void apply() {
  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  const auto& p = palette();

  style.Alpha = 1.0f;
  style.WindowPadding = {12.0f, 10.0f};
  style.WindowRounding = 8.0f;
  style.WindowBorderSize = 1.0f;
  style.WindowTitleAlign = {0.0f, 0.5f};
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.ChildRounding = 8.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupRounding = 12.0f;
  style.PopupBorderSize = 1.0f;
  style.FramePadding = {8.0f, 5.0f};
  style.FrameRounding = 6.0f;
  style.FrameBorderSize = 1.0f;
  style.ItemSpacing = {8.0f, 6.0f};
  style.ItemInnerSpacing = {6.0f, 4.0f};
  style.CellPadding = {8.0f, 5.0f};
  style.IndentSpacing = 16.0f;
  style.ScrollbarSize = 10.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabMinSize = 10.0f;
  style.GrabRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.TabBorderSize = 0.0f;
  style.TabBarBorderSize = 1.0f;
  style.TabBarOverlineSize = 2.0f;
  style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
  style.TreeLinesSize = 1.0f;
  style.TreeLinesRounding = 3.0f;
  style.SeparatorTextBorderSize = 1.0f;
  style.SeparatorTextPadding = {12.0f, 6.0f};
  style.ButtonTextAlign = {0.5f, 0.5f};
  style.DisplaySafeAreaPadding = {8.0f, 8.0f};
  style.CircleTessellationMaxError = 0.30f;
  style.HoverStationaryDelay = 0.08f;

  auto* c = style.Colors;
  c[ImGuiCol_Text] = p.text;
  c[ImGuiCol_TextDisabled] = p.muted;
  c[ImGuiCol_WindowBg] = p.window;
  c[ImGuiCol_ChildBg] = p.panel;
  c[ImGuiCol_PopupBg] = rgba(22, 23, 28, 252);
  c[ImGuiCol_Border] = p.border;
  c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = p.frame;
  c[ImGuiCol_FrameBgHovered] = p.frameHover;
  c[ImGuiCol_FrameBgActive] = p.elevated;
  c[ImGuiCol_TitleBg] = p.bg;
  c[ImGuiCol_TitleBgActive] = p.panel;
  c[ImGuiCol_TitleBgCollapsed] = rgba(7, 8, 10, 200);
  c[ImGuiCol_MenuBarBg] = p.bg;
  c[ImGuiCol_ScrollbarBg] = rgba(7, 8, 10, 140);
  c[ImGuiCol_ScrollbarGrab] = rgba(70, 74, 86);
  c[ImGuiCol_ScrollbarGrabHovered] = rgba(90, 96, 112);
  c[ImGuiCol_ScrollbarGrabActive] = rgba(110, 118, 138);
  c[ImGuiCol_CheckMark] = p.accent;
  c[ImGuiCol_CheckboxSelectedBg] = washVec(p.accent, 70);
  c[ImGuiCol_SliderGrab] = p.accent;
  c[ImGuiCol_SliderGrabActive] = p.accentHover;
  c[ImGuiCol_Button] = p.frame;
  c[ImGuiCol_ButtonHovered] = p.frameHover;
  c[ImGuiCol_ButtonActive] = p.elevated;
  c[ImGuiCol_Header] = rgba(94, 106, 210, 36);
  c[ImGuiCol_HeaderHovered] = rgba(94, 106, 210, 64);
  c[ImGuiCol_HeaderActive] = rgba(94, 106, 210, 92);
  c[ImGuiCol_Separator] = rgba(35, 37, 42, 180);
  c[ImGuiCol_SeparatorHovered] = p.accent;
  c[ImGuiCol_SeparatorActive] = p.accentHover;
  c[ImGuiCol_ResizeGrip] = washVec(p.accent, 30);
  c[ImGuiCol_ResizeGripHovered] = washVec(p.accent, 110);
  c[ImGuiCol_ResizeGripActive] = washVec(p.accent, 180);
  c[ImGuiCol_InputTextCursor] = p.accent;
  c[ImGuiCol_TabHovered] = washVec(p.accent, 80);
  c[ImGuiCol_Tab] = p.elevated;
  c[ImGuiCol_TabSelected] = p.frame;
  c[ImGuiCol_TabSelectedOverline] = p.accent;
  c[ImGuiCol_TabDimmed] = p.panel;
  c[ImGuiCol_TabDimmedSelected] = p.elevated;
  c[ImGuiCol_TabDimmedSelectedOverline] = washVec(p.accent, 120);
  c[ImGuiCol_PlotLines] = p.accent;
  c[ImGuiCol_PlotLinesHovered] = p.inspect;
  c[ImGuiCol_PlotHistogram] = p.accent;
  c[ImGuiCol_PlotHistogramHovered] = p.accentHover;
  c[ImGuiCol_TableHeaderBg] = p.elevated;
  c[ImGuiCol_TableBorderStrong] = p.hairlineStrong;
  c[ImGuiCol_TableBorderLight] = p.border;
  c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = rgba(255, 255, 255, 8);
  c[ImGuiCol_TextLink] = p.accentHover;
  c[ImGuiCol_TextSelectedBg] = washVec(p.accent, 70);
  c[ImGuiCol_TreeLines] = rgba(70, 74, 86, 160);
  c[ImGuiCol_DragDropTarget] = p.inspect;
  c[ImGuiCol_NavCursor] = p.accent;
  c[ImGuiCol_NavWindowingHighlight] = rgba(255, 255, 255, 160);
  c[ImGuiCol_NavWindowingDimBg] = rgba(7, 8, 10, 90);
  c[ImGuiCol_ModalWindowDimBg] = rgba(4, 5, 8, 176);
}

inline bool addFontFile(
    const char* path,
    float sizePx,
    float density,
    bool merge,
    int fontNo,
    const ImWchar* ranges) {
  if (path == nullptr || !std::filesystem::exists(path)) {
    return false;
  }
  ImFontConfig config;
  config.FontNo = fontNo;
  config.MergeMode = merge;
  config.RasterizerDensity = std::max(density, 1.0f);
  config.PixelSnapH = false;
  config.OversampleH = 2;
  config.OversampleV = 2;
  return ImGui::GetIO().Fonts->AddFontFromFileTTF(
             path, sizePx, &config, ranges) != nullptr;
}

inline void loadUiFonts(float sizePx, float density, const ImWchar* ranges) {
  auto& io = ImGui::GetIO();
  static ImVector<ImWchar> builtRanges;
  if (ranges == nullptr) {
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    builtRanges.clear();
    builder.BuildRanges(&builtRanges);
    ranges = builtRanges.Data;
  }

  const char* latinCandidates[] = {
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/HelveticaNeue.ttc",
      "/System/Library/Fonts/Helvetica.ttc",
  };
  bool loaded = false;
  for (const char* path : latinCandidates) {
    if (addFontFile(path, sizePx, density, false, 0, ranges)) {
      loaded = true;
      break;
    }
  }
  if (loaded) {
    const char* cjkCandidates[] = {
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    };
    for (const char* path : cjkCandidates) {
      if (addFontFile(path, sizePx, density, true, 0, ranges)) {
        break;
      }
    }
    addFontFile(
        "/System/Library/Fonts/STHeiti Medium.ttc",
        sizePx,
        density,
        false,
        0,
        ranges);
  }
}

enum class ButtonKind {
  Primary,
  Ghost,
  Danger,
  Neutral,
};

enum class ChipTone {
  Success,
  Danger,
  Accent,
  Inspect,
  Info,
  Neutral,
};

inline bool button(const char* label, ButtonKind kind, ImVec2 size = {});

inline ImVec2 keycapSize(const char* label) {
  const ImVec2 text = ImGui::CalcTextSize(label);
  return {text.x + 10.0f, std::max(16.0f, text.y + 4.0f)};
}

inline void drawKeycap(ImDrawList* drawList, ImVec2 pos, const char* label) {
  const auto& p = palette();
  const ImVec2 text = ImGui::CalcTextSize(label);
  const ImVec2 size = keycapSize(label);
  drawList->AddRectFilled(
      pos, {pos.x + size.x, pos.y + size.y}, vecToU32(p.elevated), 4.0f);
  drawList->AddRect(
      pos, {pos.x + size.x, pos.y + size.y}, vecToU32(p.border), 4.0f);
  drawList->AddText(
      {pos.x + 5.0f, pos.y + (size.y - text.y) * 0.5f},
      vecToU32(p.muted),
      label);
}

inline void keycap(const char* label) {
  const ImVec2 size = keycapSize(label);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  drawKeycap(ImGui::GetWindowDrawList(), pos, label);
  ImGui::Dummy(size);
}

inline bool segmented(
    const char* id,
    const char* const* labels,
    int count,
    int* selected,
    const char* const* shortcuts = nullptr) {
  ImGui::PushID(id);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  bool changed = false;
  const auto& p = palette();
  const float height = ImGui::GetFrameHeight();
  const float padX = 10.0f;
  float widths[16];
  float total = 2.0f;
  const int limited = std::min(count, 16);
  for (int index = 0; index < limited; ++index) {
    float width = ImGui::CalcTextSize(labels[index]).x + padX * 2.0f;
    if (shortcuts != nullptr && shortcuts[index] != nullptr &&
        shortcuts[index][0] != '\0') {
      width += keycapSize(shortcuts[index]).x + 8.0f;
    }
    widths[index] = std::max(72.0f, width);
    total += widths[index];
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  auto* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(
      origin, {origin.x + total, origin.y + height}, vecToU32(p.frame), 6.0f);
  drawList->AddRect(
      origin, {origin.x + total, origin.y + height}, vecToU32(p.border), 6.0f);
  ImGui::SetCursorScreenPos({origin.x + 1.0f, origin.y + 1.0f});
  for (int index = 0; index < limited; ++index) {
    if (index > 0) {
      ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::PushID(index);
    const bool on = *selected == index;
    ImGui::PushStyleColor(
        ImGuiCol_Button, on ? p.elevated : rgba(0, 0, 0, 0));
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        on ? p.frameHover : rgba(255, 255, 255, 12));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.elevated);
    ImGui::PushStyleColor(ImGuiCol_Text, on ? p.text : p.muted);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    const float innerHeight = height - 2.0f;
    if (ImGui::Button("##seg", {widths[index], innerHeight}) &&
        *selected != index) {
      *selected = index;
      changed = true;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 text = ImGui::CalcTextSize(labels[index]);
    const bool hasKey = shortcuts != nullptr && shortcuts[index] != nullptr &&
        shortcuts[index][0] != '\0';
    const ImVec2 cap = hasKey ? keycapSize(shortcuts[index]) : ImVec2{};
    const float content =
        text.x + (hasKey ? cap.x + 8.0f : 0.0f);
    const float textX = min.x + (max.x - min.x - content) * 0.5f;
    const float textY = min.y + (innerHeight - text.y) * 0.5f;
    drawList->AddText({textX, textY}, vecToU32(on ? p.text : p.muted), labels[index]);
    if (hasKey) {
      drawKeycap(
          drawList,
          {textX + text.x + 8.0f,
           min.y + (innerHeight - cap.y) * 0.5f},
          shortcuts[index]);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopID();
  }
  ImGui::PopStyleVar(2);
  ImGui::PopID();
  return changed;
}

inline bool button(const char* label, ButtonKind kind, ImVec2 size) {
  const auto& p = palette();
  ImVec4 bg;
  ImVec4 hovered;
  ImVec4 active;
  ImVec4 text = p.text;
  bool bordered = true;
  switch (kind) {
    case ButtonKind::Primary:
      bg = p.accent;
      hovered = p.accentHover;
      active = p.accentActive;
      text = p.onAccent;
      bordered = false;
      break;
    case ButtonKind::Danger:
      bg = rgba(240, 113, 120, 42);
      hovered = rgba(240, 113, 120, 86);
      active = rgba(240, 113, 120, 140);
      text = p.danger;
      bordered = false;
      break;
    case ButtonKind::Neutral:
      bg = p.frame;
      hovered = p.frameHover;
      active = p.elevated;
      break;
    case ButtonKind::Ghost:
      bg = rgba(255, 255, 255, 0);
      hovered = rgba(255, 255, 255, 12);
      active = rgba(255, 255, 255, 22);
      text = p.muted;
      bordered = false;
      break;
  }
  ImGui::PushStyleColor(ImGuiCol_Button, bg);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
  ImGui::PushStyleColor(ImGuiCol_Text, text);
  ImGui::PushStyleVar(
      ImGuiStyleVar_FrameBorderSize, bordered ? 1.0f : 0.0f);
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  return clicked;
}

inline void chip(const char* label, ImU32 background, ImU32 foreground, bool dot) {
  const ImVec2 text = ImGui::CalcTextSize(label);
  const float height = std::max(text.y + 6.0f, ImGui::GetFrameHeight() - 2.0f);
  const float width = text.x + (dot ? 22.0f : 16.0f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  auto* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(
      pos, {pos.x + width, pos.y + height}, background, height * 0.5f);
  float textX = pos.x + 8.0f;
  if (dot) {
    drawList->AddCircleFilled(
        {pos.x + 10.0f, pos.y + height * 0.5f}, 3.4f, foreground);
    textX = pos.x + 18.0f;
  }
  drawList->AddText(
      {textX, pos.y + (height - text.y) * 0.5f}, foreground, label);
  ImGui::Dummy({width, height});
}

inline ImVec4 toneForeground(ChipTone tone) {
  const auto& p = palette();
  switch (tone) {
    case ChipTone::Success:
      return p.success;
    case ChipTone::Danger:
      return p.danger;
    case ChipTone::Accent:
      return p.accentHover;
    case ChipTone::Inspect:
      return p.inspect;
    case ChipTone::Info:
      return p.info;
    case ChipTone::Neutral:
      return p.muted;
  }
  return p.muted;
}

inline void toneChip(const char* label, ChipTone tone, bool dot = false) {
  const ImVec4 fg = toneForeground(tone);
  chip(label, wash(fg, 28), vecToU32(fg), dot);
}

inline void liveChip(bool live) {
  if (live) {
    toneChip("live", ChipTone::Success, true);
  } else {
    toneChip("stopped", ChipTone::Danger, true);
  }
}

inline void propertyRow(const char* key, const char* value, float keyWidth = 108.0f) {
  ImGui::TextDisabled("%s", key);
  ImGui::SameLine(keyWidth);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(value != nullptr ? value : "");
  ImGui::PopTextWrapPos();
}

inline void panelHeader(const char* title, const char* subtitle = nullptr) {
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.05f);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  if (subtitle != nullptr && subtitle[0] != '\0') {
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("%s", subtitle);
  }
  ImGui::Dummy({0.0f, 1.0f});
  ImGui::Separator();
  ImGui::Dummy({0.0f, 6.0f});
}

inline bool beginPanel(
    const char* id,
    ImVec2 size,
    const ImVec4& background,
    ImGuiChildFlags childFlags = ImGuiChildFlags_Borders |
        ImGuiChildFlags_AlwaysUseWindowPadding,
    ImGuiWindowFlags windowFlags = 0) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
  return ImGui::BeginChild(id, size, childFlags, windowFlags);
}

inline void endPanel() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

inline void mutedHint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, palette().muted);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

} // namespace imgui_theme
} // namespace ReactNativeSimulator
