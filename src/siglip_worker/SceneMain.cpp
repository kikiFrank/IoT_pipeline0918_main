#include "aibox/adapter/NatsAdapter.hpp"
#include "aibox/detection/Uuid.hpp"
#include "aibox/media/AnomalySnapshotService.hpp"
#include "aibox/media/SnapshotZoneOverlay.hpp"
#include "aibox/media/MediaReadyEvent.hpp"
#include "aibox/media/NatsMediaReadyTransport.hpp"
#include "aibox/media/SnapshotRequestSubscriber.hpp"
#include "aibox/siglip_worker/ImagePreprocess.hpp"
#include "aibox/siglip_worker/JsonUtil.hpp"
#include "aibox/siglip_worker/SiglipPipelineBuilder.hpp"

#include <gst/video/video.h>

#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace aibox;
using namespace std::chrono_literals;
using siglip::Embedding;
using siglip::WorkerConfig;
constexpr std::uint64_t kSampleIntervalMs = 1000;
constexpr int kInferTimeoutMs = 5000;
constexpr char kScoreSubject[] = "scene.siglip2.score";
volatile std::sig_atomic_t stopping = 0;

void Stop(int) { stopping = 1; }

struct Prompt {
  std::vector<std::uint8_t> token_bytes;
};

struct PromptPair {
  std::string scene;
  std::string version;
  Prompt positive;
  Prompt negative;
};

struct SceneConfig {
  std::string camera_id, nats_url, media_dir;
  siglip::SiglipLiveSource live;
  std::array<double, 4> roi_xyxy{0.22, 0.245, 0.74, 0.945};
  WorkerConfig image, text;
  std::vector<PromptPair> prompts;
};

struct TextEmbeddings {
  Embedding positive;
  Embedding negative;
};

Prompt LoadPrompt(JsonObject* object) {
  if (siglip::String(object, "text").empty()) throw std::runtime_error("empty scene prompt");
  JsonArray* ids = siglip::Array(object, "input_ids");
  if (json_array_get_length(ids) != 64) throw std::runtime_error("prompt must contain 64 tokens");
  std::array<std::int32_t, 64> tokens{};
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const double id = siglip::Numeric(json_array_get_element(ids, static_cast<guint>(index)));
    if (!std::isfinite(id) || id < 0 || id > INT32_MAX || std::floor(id) != id)
      throw std::runtime_error("invalid prompt token");
    tokens[index] = static_cast<std::int32_t>(id);
  }
  Prompt result;
  result.token_bytes.resize(sizeof(tokens));
  std::memcpy(result.token_bytes.data(), tokens.data(), sizeof(tokens));
  return result;
}

PromptPair LoadPair(JsonObject* object, const std::string& revision) {
  const auto scene = json_object_has_member(object, "scene")
                         ? siglip::String(object, "scene")
                         : "workstation_organization";
  const auto version = siglip::String(object, "prompt_version");
  if (scene.empty() || version.empty()) throw std::runtime_error("empty scene or prompt version");
  return {scene, version + "-" + revision,
          LoadPrompt(siglip::Object(object, json_object_has_member(object, "positive")
                                              ? "positive" : "clutter")),
          LoadPrompt(siglip::Object(object, json_object_has_member(object, "negative")
                                              ? "negative" : "clean"))};
}

SceneConfig LoadScene(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read scene configuration");
  const std::string contents{std::istreambuf_iterator<char>(input), {}};
  auto json = siglip::ParseJson(contents);
  auto* root = siglip::RootObject(json.get());
  SceneConfig config;
  config.camera_id = siglip::String(root, "camera_id");
  if (json_object_has_member(root, "frame_socket")) {
    const auto socket = siglip::String(root, "frame_socket");
    config.live.frame_caps = siglip::String(root, "frame_caps");
    if (socket.empty() || config.live.frame_caps.empty() ||
        json_object_has_member(root, "rtsp_url"))
      throw std::runtime_error("invalid shared scene frame source");
    config.live.frame_socket = (path.parent_path() / socket).lexically_normal().string();
  } else {
    config.live.rtsp_url = siglip::String(root, "rtsp_url");
    if (config.live.rtsp_url.compare(0, 7, "rtsp://") != 0)
      throw std::runtime_error("invalid scene RTSP source");
  }
  config.nats_url = siglip::String(root, "nats_url");
  config.media_dir = siglip::String(root, "media_dir");
  if (json_object_has_member(root, "roi_xyxy")) {
    auto* roi = siglip::Array(root, "roi_xyxy");
    if (json_array_get_length(roi) != config.roi_xyxy.size())
      throw std::runtime_error("roi_xyxy requires four normalized coordinates");
    for (guint i = 0; i < config.roi_xyxy.size(); ++i) {
      const auto value = siglip::Numeric(json_array_get_element(roi, i));
      if (!std::isfinite(value) || value < 0 || value > 1)
        throw std::runtime_error("roi_xyxy coordinates must be in [0,1]");
      config.roi_xyxy[i] = value;
    }
    if (config.roi_xyxy[0] >= config.roi_xyxy[2] ||
        config.roi_xyxy[1] >= config.roi_xyxy[3])
      throw std::runtime_error("roi_xyxy must have positive area");
  }
  if (config.camera_id.empty() || config.nats_url.empty() || config.media_dir.empty())
    throw std::runtime_error("invalid scene endpoints");
  const auto revision = siglip::Sha256(contents.data(), contents.size()).substr(0, 12);
  if (json_object_has_member(root, "scenes")) {
    auto* scenes = siglip::Array(root, "scenes");
    std::set<std::string> scene_names;
    for (guint index = 0; index < json_array_get_length(scenes); ++index) {
      auto* entry = json_array_get_element(scenes, index);
      if (!JSON_NODE_HOLDS_OBJECT(entry)) throw std::runtime_error("invalid scene entry");
      auto pair = LoadPair(json_node_get_object(entry), revision);
      if (!scene_names.insert(pair.scene).second) throw std::runtime_error("duplicate scene");
      config.prompts.push_back(std::move(pair));
    }
    if (config.prompts.empty()) throw std::runtime_error("scenes must contain a prompt pair");
  } else {
    config.prompts.push_back(LoadPair(root, revision));
  }

  // Text encoding finishes before the image pipeline starts on the same HTP.
  const bool has_secondary_htp = std::filesystem::exists("/dev/fastrpc-cdsp1");
  if (!has_secondary_htp && !std::filesystem::exists("/dev/fastrpc-cdsp"))
    throw std::runtime_error("scene requires an HTP device");
  const int htp_device_id = has_secondary_htp ? 1 : 0;
  const std::array<WorkerConfig*, 2> encoders{&config.image, &config.text};
  const std::array<const char*, 2> models{"image_encoder", "text_encoder"};
  const std::array<const char*, 2> modules{"siglip2-image", "siglip2-text"};
  for (std::size_t stage = 0; stage < encoders.size(); ++stage) {
    auto& encoder = *encoders[stage];
    encoder.htp_device_id = htp_device_id;
    encoder.backend_library = "/usr/lib/libQnnHtp.so";
    encoder.system_library = "/usr/lib/libQnnSystem.so";
    encoder.postprocess_module = modules[stage];
    encoder.model_path = (path.parent_path() / siglip::String(root, models[stage]))
                             .lexically_normal().string();
    if (!std::filesystem::is_regular_file(encoder.model_path))
      throw std::runtime_error("missing encoder: " + encoder.model_path);
  }
  return config;
}

struct SampleDeleter {
  void operator()(GstSample* sample) const { gst_sample_unref(sample); }
};

struct BufferDeleter {
  void operator()(GstBuffer* buffer) const { gst_buffer_unref(buffer); }
};

struct ConverterDeleter {
  void operator()(GstVideoConverter* converter) const { gst_video_converter_free(converter); }
};

class MappedVideoFrame {
 public:
  MappedVideoFrame(const GstVideoInfo& info, GstBuffer* buffer, GstMapFlags flags) {
    if (!gst_video_frame_map(&frame_, &info, buffer, flags))
      throw std::runtime_error("cannot map video frame");
  }
  ~MappedVideoFrame() { gst_video_frame_unmap(&frame_); }
  MappedVideoFrame(const MappedVideoFrame&) = delete;
  MappedVideoFrame& operator=(const MappedVideoFrame&) = delete;
  GstVideoFrame& frame() { return frame_; }

 private:
  GstVideoFrame frame_{};
};

class SceneImagePreprocessor {
 public:
  explicit SceneImagePreprocessor(const std::array<double, 4>& roi) : roi_(roi) {}

  std::vector<std::uint8_t> Prepare(GstSample* sample) {
    GstVideoInfo info{};
    if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) ||
        GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_NV12)
      throw std::runtime_error("invalid scene video caps");
    const int width = GST_VIDEO_INFO_WIDTH(&info);
    const int height = GST_VIDEO_INFO_HEIGHT(&info);
    if (!converter_ || !gst_video_info_is_equal(&input_, &info)) {
      gst_video_info_set_format(&output_, GST_VIDEO_FORMAT_RGB, width, height);
      output_.fps_n = info.fps_n;
      output_.fps_d = info.fps_d;
      output_.par_n = info.par_n;
      output_.par_d = info.par_d;
      converter_.reset(gst_video_converter_new(&info, &output_, nullptr));
      if (!converter_) throw std::runtime_error("cannot create NV12-to-RGB converter");
      input_ = info;
    }
    std::unique_ptr<GstBuffer, BufferDeleter> rgb(
        gst_buffer_new_allocate(nullptr, output_.size, nullptr));
    if (!rgb) throw std::runtime_error("cannot allocate RGB video frame");
    MappedVideoFrame source(info, gst_sample_get_buffer(sample), GST_MAP_READ);
    MappedVideoFrame target(output_, rgb.get(), GST_MAP_WRITE);
    gst_video_converter_frame(converter_.get(), &source.frame(), &target.frame());
    const int x = static_cast<int>(width * roi_[0] + 1e-8);
    const int y = static_cast<int>(height * roi_[1] + 1e-8);
    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&target.frame(), 0);
    const auto* data = static_cast<const std::uint8_t*>(
        GST_VIDEO_FRAME_PLANE_DATA(&target.frame(), 0));
    return siglip::PrepareRgbImage(data + y * stride + x * 3,
                                  static_cast<int>(width * roi_[2] + 1e-8) - x,
                                  static_cast<int>(height * roi_[3] + 1e-8) - y, stride);
  }

 private:
  const std::array<double, 4> roi_;
  GstVideoInfo input_{};
  GstVideoInfo output_{};
  std::unique_ptr<GstVideoConverter, ConverterDeleter> converter_;
};

std::vector<TextEmbeddings> EncodePrompts(const SceneConfig& config) {
  siglip::SiglipPipelineBuilder text(config.text);
  text.WaitReady();
  const auto heartbeat = [] {
    if (stopping) throw std::runtime_error("scene stopping");
  };
  std::vector<TextEmbeddings> embeddings;
  embeddings.reserve(config.prompts.size());
  for (const auto& pair : config.prompts) {
    embeddings.push_back({text.Infer(pair.positive.token_bytes, kInferTimeoutMs, heartbeat),
                          text.Infer(pair.negative.token_bytes, kInferTimeoutMs, heartbeat)});
  }
  // Surface HTP teardown failures before starting the live stream.
  text.Stop();
  return embeddings;
}

double Score(const Embedding& image, const Embedding& text) {
  if (image.values.size() != text.values.size())
    throw std::runtime_error("image and text dimensions differ");
  const double cosine = std::inner_product(
      image.values.begin(), image.values.end(), text.values.begin(), 0.0);
  return 100.0 / (1.0 + std::exp(-(112.66890226978423 * cosine - 16.771724700927734)));
}

std::uint64_t MonotonicMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

void RunStream(const SceneConfig& config, const std::vector<TextEmbeddings>& text_embeddings,
               adapter::NatsAdapter& nats,
               media::FrameSnapshotCache& cache) {
  siglip::SiglipPipelineBuilder image(config.image, config.live);
  image.WaitReady();
  SceneImagePreprocessor preprocess(config.roi_xyxy);
  const auto epoch = detection::UuidV4();
  std::uint64_t sequence = 0;
  auto next_tick = MonotonicMs();
  auto last_frame = next_tick;
  while (!stopping) {
    std::unique_ptr<GstSample, SampleDeleter> sample(image.PullLiveFrame(250 * GST_MSECOND));
    const auto now = MonotonicMs();
    if (!sample) {
      if (now - last_frame > 8000) throw std::runtime_error("scene frame timeout");
      continue;
    }
    last_frame = now;
    if (now < next_tick) continue;
    const auto elapsed_ticks = (now - next_tick) / kSampleIntervalMs;
    const auto tick = next_tick + elapsed_ticks * kSampleIntervalMs;
    sequence += elapsed_ticks + 1;
    next_tick = tick + kSampleIntervalMs;

    media::CachedFrame frame;
    frame.camera_id = config.camera_id;
    frame.frame_id = "siglip2-" + epoch + "-" + std::to_string(sequence);
    frame.timestamp_ms = siglip::NowMs();
    if (!media::AnomalySnapshotService::CopySampleToPackedNv12(
            sample.get(), frame.packed_nv12, frame.width, frame.height))
      throw std::runtime_error("cannot copy scene snapshot frame");
    auto png = preprocess.Prepare(sample.get());
    sample.reset();
    const auto embedding = image.Infer(png, kInferTimeoutMs, [] {
      if (stopping) throw std::runtime_error("scene stopping");
    });
    const auto frame_id = frame.frame_id;
    const auto timestamp = frame.timestamp_ms;
    if (!cache.Put(std::move(frame))) throw std::runtime_error("cannot cache scene frame");

    for (std::size_t index = 0; index < config.prompts.size(); ++index) {
      const auto& pair = config.prompts[index];
      const auto& vectors = text_embeddings[index];
      using media::MediaReadyEvent;
      std::ostringstream message;
      message.imbue(std::locale::classic());
      message.precision(15);
      message << "{\"schema_version\":1,\"camera_id\":\""
              << MediaReadyEvent::EscapeJson(config.camera_id)
              << "\",\"scene\":\"" << MediaReadyEvent::EscapeJson(pair.scene)
              << "\",\"frame_id\":\"" << frame_id << "\",\"stream_epoch\":\"" << epoch
              << "\",\"sample_sequence\":" << sequence << ",\"capture_time_ms\":" << timestamp
              << ",\"monotonic_ms\":" << tick << ",\"prompt_version\":\""
              << MediaReadyEvent::EscapeJson(pair.version)
              << "\",\"clutter_score\":" << Score(embedding, vectors.positive)
              << ",\"clean_score\":" << Score(embedding, vectors.negative)
              << ",\"roi_xyxy\":[" << config.roi_xyxy[0] << ',' << config.roi_xyxy[1]
              << ',' << config.roi_xyxy[2] << ',' << config.roi_xyxy[3] << "]}";
      const auto payload = message.str();
      if (nats.Publish(kScoreSubject, payload)) {
        std::cout << "[SIGLIP_SCENE_SCORE] " << payload << std::endl;
      } else {
        std::cerr << "[SIGLIP_SCENE_SCORE_DROPPED] frame_id=" << frame_id
                  << " scene=" << pair.scene << '\n';
      }
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: aibox_siglip_scene CONFIG.json\n";
    return 2;
  }
  std::signal(SIGINT, Stop);
  std::signal(SIGTERM, Stop);
  gst_init(nullptr, nullptr);
  try {
    const auto config = LoadScene(argv[1]);
    adapter::NatsAdapter nats;
    adapter::NatsAdapter::Config nats_config;
    nats_config.nats_url = config.nats_url;
    nats_config.client_name = "aibox-siglip-scene";
    nats_config.max_pending = 0;
    if (!nats.Connect(nats_config)) throw std::runtime_error("scene NATS connection failed");
    media::MediaFileWriter writer(config.media_dir);
    if (!writer.EnsureDirectory()) throw std::runtime_error("scene media directory is unavailable");
    media::GstSnapshotEncoder jpeg;
    media::GstThumbnailEncoder thumbnail;
    media::NatsMediaReadyTransport transport(nats);
    media::FrameCacheConfig cache_config;
    cache_config.ttl_ms = 30'000;
    cache_config.max_frames_per_camera = 32;
    media::FrameSnapshotCache cache(cache_config);
    media::SnapshotZoneOverlay snapshot_zones(nats);
    if (!snapshot_zones.Start()) {
      std::cerr << "[WARN] Could not subscribe to snapshot Zone updates" << std::endl;
    }
    media::AnomalySnapshotService snapshot(jpeg, thumbnail, writer, transport);
    snapshot.SetFrameOverlay(
        [&snapshot_zones](const std::string& camera_id, std::vector<uint8_t>& packed_nv12,
                          uint32_t width, uint32_t height) {
          snapshot_zones.Draw(camera_id, packed_nv12, width, height);
        });
    snapshot.SetFrameCache(&cache);
    media::SnapshotRequestSubscriber subscriber(
        nats, snapshot, media::SnapshotRequestSubscriber::kSiglip2Subject, config.camera_id);
    snapshot.Start();
    try {
      if (!subscriber.Start()) throw std::runtime_error("scene snapshot subscription failed");
      const auto text_embeddings = EncodePrompts(config);
      std::cout << "[SIGLIP_SCENE_TEXT_READY] pipeline=NULL version="
                << config.prompts.front().version << " pairs=" << config.prompts.size()
                << std::endl;
      bool running = false;
      while (!stopping) {
        try {
          RunStream(config, text_embeddings, nats, cache);
        } catch (const std::exception& error) {
          if (stopping) break;
          if (!running && cache.Frames() == 0) throw;
          std::cerr << "[SIGLIP_SCENE_RECONNECT] " << error.what() << std::endl;
          running = true;
          std::this_thread::sleep_for(1s);
        }
      }
    } catch (...) {
      snapshot.Stop();
      subscriber.Stop();
      throw;
    }
    snapshot.Stop();
    subscriber.Stop();
  } catch (const std::exception& error) {
    if (stopping) return 0;
    std::cerr << "[SIGLIP_SCENE_FAILED] " << error.what() << std::endl;
    return 1;
  }
  return 0;
}
