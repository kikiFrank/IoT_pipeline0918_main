#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aibox::siglip {
struct PreparedImage {
  std::vector<std::uint8_t> png;
  std::string source_sha256;
  std::string inference_sha256;
  int source_width = 0, source_height = 0;
};
PreparedImage PrepareImage(const std::vector<std::uint8_t>& jpeg);
std::vector<std::uint8_t> PrepareRgbImage(const std::uint8_t* rgb, int width,
                                         int height, std::size_t stride);
}  // namespace aibox::siglip
