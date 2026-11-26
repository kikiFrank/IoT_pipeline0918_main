#include "aibox/media/SnapshotZoneOverlay.hpp"

#include <cairo.h>
#include <gst/video/video.h>
#include <gst/video/video-overlay-composition.h>
#include <json-glib/json-glib.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <utility>

#include "aibox/adapter/NatsAdapter.hpp"
#include "aibox/detection/Uuid.hpp"

namespace aibox::media {
namespace {

constexpr char kZoneUpdates[] = "event.cloud.zones";
constexpr char kZoneList[] = "rpc.datamgr.zone.list";

// Match the Live View palette and SVG fill alpha in ZoneOverlay.tsx.
constexpr std::array<std::array<uint8_t, 3>, 6> kZoneColors{{
    {0x0a, 0x84, 0xff},
    {0xa8, 0x55, 0xf7},
    {0x00, 0xd4, 0xaa},
    {0xff, 0xa5, 0x02},
    {0xff, 0x47, 0x57},
    {0x2e, 0xd5, 0x73},
}};
constexpr double kColorChannelMax = 255.0;
constexpr double kFillOpacity = 0x14 / kColorChannelMax;

struct ZonePoint { double x; double y; };
using Polygons = std::vector<std::vector<ZonePoint>>;

struct ZoneSnapshot {
  std::uint64_t version = 0;
  std::unordered_map<std::string, Polygons> by_camera;
};

JsonNode* Field(JsonObject* object, const char* name) {
  return object ? json_object_get_member(object, name) : nullptr;
}

JsonObject* ReadObject(JsonNode* node) {
  return node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : nullptr;
}

JsonArray* ReadArray(JsonNode* node) {
  return node && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : nullptr;
}

bool IsValue(JsonNode* node, GType type) {
  return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == type;
}

bool ReadCoordinate(JsonNode* node, double& value) {
  if (!IsValue(node, G_TYPE_DOUBLE) && !IsValue(node, G_TYPE_INT64)) return false;
  value = json_node_get_double(node);
  return std::isfinite(value) && value >= 0 && value <= 1;
}

bool ReadPoint(JsonNode* node, ZonePoint& point) {
  if (auto* object = ReadObject(node)) {
    return ReadCoordinate(Field(object, "x"), point.x) &&
           ReadCoordinate(Field(object, "y"), point.y);
  }
  auto* array = ReadArray(node);
  return array && json_array_get_length(array) == 2 &&
         ReadCoordinate(json_array_get_element(array, 0), point.x) &&
         ReadCoordinate(json_array_get_element(array, 1), point.y);
}

std::shared_ptr<ZoneSnapshot> ParseSnapshot(const std::string& payload) {
  const auto unref = [](JsonParser* parser) { g_object_unref(parser); };
  std::unique_ptr<JsonParser, decltype(unref)> parser(json_parser_new(), unref);
  if (!json_parser_load_from_data(parser.get(), payload.data(), payload.size(), nullptr))
    return {};
  auto* root = ReadObject(json_parser_get_root(parser.get()));
  auto* version = Field(root, "config_version");
  auto* zones = ReadArray(Field(root, "zones"));
  if (!IsValue(version, G_TYPE_INT64) || json_node_get_int(version) < 0 || !zones)
    return {};

  auto snapshot = std::make_shared<ZoneSnapshot>();
  snapshot->version = static_cast<std::uint64_t>(json_node_get_int(version));
  for (guint i = 0; i < json_array_get_length(zones); ++i) {
    auto* zone = ReadObject(json_array_get_element(zones, i));
    auto* enabled = Field(zone, "enabled");
    if (!IsValue(enabled, G_TYPE_BOOLEAN)) return {};
    if (!json_node_get_boolean(enabled)) continue;

    auto* camera = Field(zone, "camera_id");
    auto* points = ReadArray(Field(zone, "points"));
    if (!IsValue(camera, G_TYPE_STRING) || !points || json_array_get_length(points) < 3)
      return {};
    const char* camera_id = json_node_get_string(camera);
    if (!camera_id || !*camera_id) return {};

    std::vector<ZonePoint> polygon;
    polygon.reserve(json_array_get_length(points));
    for (guint j = 0; j < json_array_get_length(points); ++j) {
      ZonePoint point{};
      if (!ReadPoint(json_array_get_element(points, j), point)) return {};
      polygon.push_back(point);
    }
    snapshot->by_camera[camera_id].push_back(std::move(polygon));
  }
  return snapshot;
}

GstVideoOverlayComposition* DrawPolygons(const Polygons& polygons, int width, int height) {
  // The line must remain visible after resizing the frame to a thumbnail.
  const double line_width = std::max(3.0, width / 320.0);
  const int padding = static_cast<int>(std::ceil(line_width)) + 2;
  int left = width, right = 0, top = height, bottom = 0;
  for (const auto& polygon : polygons) {
    for (const auto& point : polygon) {
      const int x = static_cast<int>(std::lround(point.x * (width - 1)));
      const int y = static_cast<int>(std::lround(point.y * (height - 1)));
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  left = std::max(0, left - padding);
  top = std::max(0, top - padding);
  const int draw_width = std::min(width, right + padding + 1) - left;
  const int draw_height = std::min(height, bottom + padding + 1) - top;
  const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, draw_width);
  if (draw_width <= 0 || draw_height <= 0 || stride <= 0) return nullptr;

  const auto bytes = static_cast<gsize>(stride) * draw_height;
  std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)> pixels(
      gst_buffer_new_allocate(nullptr, bytes, nullptr), gst_buffer_unref);
  if (!pixels) return nullptr;
  GstMapInfo map{};
  if (!gst_buffer_map(pixels.get(), &map, GST_MAP_WRITE)) return nullptr;
  std::memset(map.data, 0, bytes);
  auto* surface = cairo_image_surface_create_for_data(
      map.data, CAIRO_FORMAT_ARGB32, draw_width, draw_height, stride);
  auto* canvas = cairo_create(surface);
  cairo_set_line_width(canvas, line_width);
  cairo_set_line_join(canvas, CAIRO_LINE_JOIN_ROUND);
  for (std::size_t i = 0; i < polygons.size(); ++i) {
    const auto& polygon = polygons[i];
    const auto x = [&](const ZonePoint& point) { return point.x * (width - 1) - left; };
    const auto y = [&](const ZonePoint& point) { return point.y * (height - 1) - top; };
    cairo_move_to(canvas, x(polygon.front()), y(polygon.front()));
    for (auto point = std::next(polygon.begin()); point != polygon.end(); ++point)
      cairo_line_to(canvas, x(*point), y(*point));
    cairo_close_path(canvas);
    const auto& [red, green, blue] = kZoneColors[i % kZoneColors.size()];
    const double r = red / kColorChannelMax;
    const double g = green / kColorChannelMax;
    const double b = blue / kColorChannelMax;
    cairo_set_source_rgba(canvas, r, g, b, kFillOpacity);
    cairo_fill_preserve(canvas);
    cairo_set_source_rgb(canvas, r, g, b);
    cairo_stroke(canvas);
  }
  const auto status = cairo_status(canvas);
  cairo_destroy(canvas);
  cairo_surface_destroy(surface);
  gst_buffer_unmap(pixels.get(), &map);

  GstVideoOverlayComposition* result = nullptr;
  if (status == CAIRO_STATUS_SUCCESS) {
    gst_buffer_add_video_meta(pixels.get(), GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, draw_width, draw_height);
    auto* rectangle = gst_video_overlay_rectangle_new_raw(
        pixels.get(), left, top, draw_width, draw_height,
        GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
    if (rectangle) {
      result = gst_video_overlay_composition_new(rectangle);
      gst_video_overlay_rectangle_unref(rectangle);
    }
  }
  return result;
}

}  // namespace

struct SnapshotZoneOverlay::State {
  void Apply(const std::string& payload) {
    auto incoming = ParseSnapshot(payload);
    if (!incoming) {
      g_warning("Snapshot Zone overlay ignored an invalid Zone configuration");
      return;
    }
    std::lock_guard<std::mutex> lock(updates);
    const auto current = std::atomic_load(&snapshot);
    if (current && current->version >= incoming->version) return;
    g_message("Snapshot Zone overlay config_version=%" G_GUINT64_FORMAT " cameras=%zu",
              static_cast<guint64>(incoming->version), incoming->by_camera.size());
    std::atomic_store(&snapshot, std::shared_ptr<const ZoneSnapshot>(std::move(incoming)));
  }

  std::mutex updates;
  std::shared_ptr<const ZoneSnapshot> snapshot;
};

SnapshotZoneOverlay::SnapshotZoneOverlay(adapter::NatsAdapter& nats)
    : nats_(nats), state_(std::make_shared<State>()),
      inbox_("_INBOX." + detection::UuidV4()) {}

SnapshotZoneOverlay::~SnapshotZoneOverlay() {
  Stop();
}

void SnapshotZoneOverlay::Stop() {
  if (!refresh_thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  stop_.notify_one();
  refresh_thread_.join();
  nats_.Unsubscribe(kZoneUpdates);
  nats_.Unsubscribe(inbox_);
}

bool SnapshotZoneOverlay::Start() {
  if (refresh_thread_.joinable()) return true;
  auto receive = [state = state_](const std::string&, const std::string&,
                                  const std::string& payload) { state->Apply(payload); };
  if (!nats_.Subscribe(kZoneUpdates, receive)) return false;
  if (!nats_.Subscribe(inbox_, receive)) {
    nats_.Unsubscribe(kZoneUpdates);
    return false;
  }
  stopping_ = false;
  Refresh();
  // SigLIP2 pulls samples with its own loop, without a GLib main loop.
  refresh_thread_ = std::thread([this] {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_.wait_for(lock, std::chrono::seconds(5), [this] { return stopping_; })) {
      lock.unlock();
      Refresh();
      lock.lock();
    }
  });
  return true;
}

void SnapshotZoneOverlay::Refresh() {
  static const std::vector<uint8_t> request{'{', '}'};
  nats_.PublishRequest(kZoneList, inbox_, request);
}

void SnapshotZoneOverlay::Draw(const std::string& camera_id,
                                std::vector<uint8_t>& packed_nv12,
                                uint32_t width, uint32_t height) const {
  const auto snapshot = std::atomic_load(&state_->snapshot);
  if (!snapshot) return;
  const auto camera = snapshot->by_camera.find(camera_id);
  if (camera == snapshot->by_camera.end()) return;

  if (width < 2 || height < 2 || width % 2 || height % 2 ||
      width > G_MAXINT / 2 || height > G_MAXINT / 2) return;
  const size_t luma_size = static_cast<size_t>(width) * height;
  if (packed_nv12.size() != luma_size + luma_size / 2) return;

  using Composition = std::unique_ptr<GstVideoOverlayComposition,
                                     decltype(&gst_video_overlay_composition_unref)>;
  Composition composition(DrawPolygons(camera->second, width, height),
                          gst_video_overlay_composition_unref);
  if (!composition) {
    g_warning("Could not render Snapshot Zone overlay for %s", camera_id.c_str());
    return;
  }

  // The snapshot cache supplies tightly packed NV12, without GStreamer's
  // default per-row padding.
  GstVideoInfo info{};
  if (!gst_video_info_set_format(&info, GST_VIDEO_FORMAT_NV12, width, height)) return;
  info.stride[0] = info.stride[1] = static_cast<int>(width);
  info.offset[1] = luma_size;
  info.size = packed_nv12.size();
  std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)> buffer(
      gst_buffer_new_wrapped_full(static_cast<GstMemoryFlags>(0), packed_nv12.data(),
                                  packed_nv12.size(), 0, packed_nv12.size(), nullptr, nullptr),
      gst_buffer_unref);
  GstVideoFrame frame{};
  if (!buffer || !gst_video_frame_map(&frame, &info, buffer.get(), GST_MAP_WRITE)) return;
  const bool blended = gst_video_overlay_composition_blend(composition.get(), &frame);
  gst_video_frame_unmap(&frame);
  if (!blended) g_warning("Could not blend Snapshot Zone overlay for %s", camera_id.c_str());
}

}  // namespace aibox::media
