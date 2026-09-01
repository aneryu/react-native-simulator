#pragma once

#include <cstddef>
#include <string>

struct HeadlessFabricResult {
  bool passed{false};
  std::size_t transactions{0};
  std::size_t creates{0};
  std::size_t inserts{0};
  std::size_t updates{0};
  float firstWidth{0};
  float firstFlexWidth{0};
  float updatedWidth{0};
  float updatedFlexWidth{0};
  std::string error;
};

HeadlessFabricResult runHeadlessFabricPipeline();
