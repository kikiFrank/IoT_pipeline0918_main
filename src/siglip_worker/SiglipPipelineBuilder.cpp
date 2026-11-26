#include "aibox/siglip_worker/SiglipPipelineBuilder.hpp"
#include "aibox/siglip_worker/ImagePreprocess.hpp"
#include "aibox/pipeline/GstElementHelper.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#ifdef AIBOX_SIGLIP_QTI_METADATA
#include <gst/ml/gstmlmeta.h>
#include <gst/ml/ml-info.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace aibox::siglip {
namespace {
void SetCaps(GstElement* filter, const char* description) {
  GstCaps* caps = gst_caps_from_string(description);
  if (!caps) throw std::runtime_error(std::string("invalid caps: ") + description);
  g_object_set(filter, "caps", caps, nullptr);
  gst_caps_unref(caps);
}
GstElement* Make(GstElement* pipeline, const char* type, const char* name) {
  GstElement* element = gst_element_factory_make(type, name);
  if (!element) throw std::runtime_error(std::string("missing GStreamer element: ") + type);
  if (!gst_bin_add(GST_BIN(pipeline), element)) {
    gst_object_unref(element);
    throw std::runtime_error(std::string("cannot add GStreamer element: ") + type);
  }
  return element;
}
}  // namespace
SiglipPipelineBuilder::SiglipPipelineBuilder(const WorkerConfig& config,
                                             const std::string& rtsp_url)
    : config_(config) {
  gst_init(nullptr, nullptr);
  const bool text_encoder = config_.postprocess_module == "siglip2-text";
#ifndef AIBOX_SIGLIP_QTI_METADATA
  if (text_encoder) throw std::runtime_error("text encoder requires the Qualcomm GStreamer SDK");
#endif
  pipeline_ = gst_pipeline_new(text_encoder ? "siglip2-text" : "siglip2-image");
  if (!pipeline_) throw std::runtime_error("cannot create SigLIP pipeline");
  try {
    source_ = Make(pipeline_, "appsrc", "siglip_src");
    GstElement* queue = Make(pipeline_, "queue", "siglip_queue");
    GstElement* qnn = Make(pipeline_, "qtimlqnn", "siglip_qnn");
    GstElement* post = Make(pipeline_, "qtimlpostprocess_roi", "siglip_post");
    GstElement* text = Make(pipeline_, "capsfilter", "siglip_text_caps");
    sink_ = Make(pipeline_, "appsink", "siglip_sink");
    GstCaps* input = nullptr;
#ifdef AIBOX_SIGLIP_QTI_METADATA
    if (text_encoder) {
      GstMLInfo info{};
      gst_ml_info_init(&info);
      GST_ML_INFO_TYPE(&info) = GST_ML_TYPE_INT32;
      GST_ML_INFO_N_TENSORS(&info) = 1;
      GST_ML_INFO_N_DIMENSIONS(&info, 0) = 2;
      GST_ML_INFO_TENSOR_DIM(&info, 0, 0) = 1;
      GST_ML_INFO_TENSOR_DIM(&info, 0, 1) = 64;
      input = gst_ml_info_to_caps(&info);
    }
#endif
    if (!input) input = gst_caps_from_string("image/png");
    g_object_set(source_, "caps", input, "is-live", TRUE, "format", GST_FORMAT_TIME,
                 "block", FALSE, "do-timestamp", FALSE, nullptr);
    gst_caps_unref(input);
    g_object_set(queue, "max-size-buffers", 1u, "max-size-bytes", 0u,
                 "max-size-time", static_cast<guint64>(0), nullptr);
    std::string error;
    g_object_set(qnn, "model", config_.model_path.c_str(),
                 "backend", config_.backend_library.c_str(),
                 "system", config_.system_library.c_str(),
                 "backend-device-id", static_cast<guint>(config_.htp_device_id), nullptr);
    gst_util_set_object_arg(G_OBJECT(qnn), "tensors",
                            text_encoder ? "text_embeds" : "image_embeds");
    guint applied = 0;
    g_object_get(qnn, "backend-device-id", &applied, nullptr);
    if (config_.require_device_id && applied != static_cast<guint>(config_.htp_device_id))
      throw std::runtime_error("HTP device ID readback differs");
    if (text_encoder || !rtsp_url.empty()) {
      std::cout << "[SIGLIP_HTP] model=" << config_.postprocess_module
                << " backend-device-id=" << applied << std::endl;
    }
    for (const auto& property : {std::pair<const char*, std::string>{"model", config_.model_path},
                                  {"backend", config_.backend_library},
                                  {"system", config_.system_library}}) {
      gchar* value = nullptr;
      g_object_get(qnn, property.first, &value, nullptr);
      const bool matches = value && property.second == value;
      g_free(value);
      if (!matches) throw std::runtime_error(std::string("QNN property readback differs: ") + property.first);
    }
    if (!aibox::pipeline::SetEnumPropertyByNick(post, "module",
                                                config_.postprocess_module.c_str(), &error))
      throw std::runtime_error("postprocess module: " + error);
    SetCaps(text, "text/x-raw,format=utf8");
    g_object_set(sink_, "sync", FALSE, "async", FALSE, "max-buffers", 1u,
                 "drop", FALSE, "emit-signals", FALSE, nullptr);
    bool input_linked = false;
    if (text_encoder) {
      input_linked = gst_element_link_many(source_, queue, qnn, nullptr);
    } else {
      auto* png = Make(pipeline_, "pngdec", "siglip_pngdec");
      auto* convert = Make(pipeline_, "videoconvert", "siglip_rgb");
      auto* rgb = Make(pipeline_, "capsfilter", "siglip_rgb_caps");
      auto* dma = Make(pipeline_, "qtivtransform", "siglip_dma");
      auto* dma_rgb = Make(pipeline_, "capsfilter", "siglip_dma_rgb_caps");
      auto* tensor = Make(pipeline_, "qtimlvconverter", "siglip_tensor");
      SetCaps(rgb, "video/x-raw,format=RGB");
      SetCaps(dma_rgb, "video/x-raw,format=RGB");
      if (!aibox::pipeline::SetEnumPropertyByNick(tensor, "engine", "gles", &error) ||
          !aibox::pipeline::SetEnumPropertyByNick(tensor, "image-disposition", "stretch", &error) ||
          !aibox::pipeline::SetEnumPropertyByNick(tensor, "subpixel-layout", "regular", &error))
        throw std::runtime_error("converter property: " + error);
      input_linked = gst_element_link_many(source_, queue, png, convert, rgb, dma,
                                            dma_rgb, tensor, qnn, nullptr);
    }
    if (!input_linked || !gst_element_link_many(qnn, post, text, sink_, nullptr))
      throw std::runtime_error("SigLIP pipeline link failed");
    if (!rtsp_url.empty()) {
      GError* parse_error = nullptr;
      GstElement* capture = gst_parse_bin_from_description(
          "rtspsrc name=scene_rtsp protocols=tcp latency=160 tcp-timeout=5000000 "
          "! application/x-rtp,media=video,encoding-name=H264 "
          "! rtph264depay ! h264parse ! v4l2h264dec "
          "! video/x-raw,format=NV12 ! appsink name=scene_frames "
          "sync=false async=false max-buffers=1 drop=true enable-last-sample=false",
          FALSE, &parse_error);
      if (parse_error || !capture) {
        const std::string detail = parse_error ? parse_error->message : "missing capture bin";
        g_clear_error(&parse_error);
        if (capture) gst_object_unref(capture);
        throw std::runtime_error("RTSP capture: " + detail);
      }
      if (!gst_bin_add(GST_BIN(pipeline_), capture)) {
        gst_object_unref(capture);
        throw std::runtime_error("cannot add RTSP capture");
      }
      GstElement* rtsp = gst_bin_get_by_name(GST_BIN(capture), "scene_rtsp");
      live_sink_ = gst_bin_get_by_name(GST_BIN(capture), "scene_frames");
      if (!rtsp || !live_sink_) {
        if (rtsp) gst_object_unref(rtsp);
        throw std::runtime_error("RTSP capture elements missing");
      }
      g_object_set(rtsp, "location", rtsp_url.c_str(), nullptr);
      gst_object_unref(rtsp);
    }
    bus_ = gst_element_get_bus(pipeline_);
    const auto state = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (state == GST_STATE_CHANGE_FAILURE) throw std::runtime_error("SigLIP PLAYING failed");
  } catch (...) {
    Stop();
    throw;
  }
}
SiglipPipelineBuilder::~SiglipPipelineBuilder() {
  try { Stop(); } catch (...) {}
}
void SiglipPipelineBuilder::CheckBus() {
  while (GstMessage* message = gst_bus_timed_pop_filtered(bus_, 0,
                                    static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
      GError* error = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &error, &debug);
      const std::string detail = error ? error->message : "unknown GStreamer error";
      if (error) g_error_free(error);
      g_free(debug);
      gst_message_unref(message);
      throw std::runtime_error("SigLIP pipeline: " + detail);
    }
    gst_message_unref(message);
    throw std::runtime_error("unexpected SigLIP EOS");
  }
}
GstSample* SiglipPipelineBuilder::PullLiveFrame(GstClockTime timeout) {
  if (!live_sink_) throw std::runtime_error("SigLIP RTSP capture unavailable");
  CheckBus();
  return gst_app_sink_try_pull_sample(GST_APP_SINK(live_sink_), timeout);
}

Embedding SiglipPipelineBuilder::Infer(const std::vector<std::uint8_t>& input, int timeout_ms,
                                      const std::function<void()>& heartbeat) {
  if (stopped_ || input.empty()) throw std::runtime_error("SigLIP pipeline unavailable");
  CheckBus();
  if (GstSample* stale = gst_app_sink_try_pull_sample(GST_APP_SINK(sink_), 0)) {
    gst_sample_unref(stale);
    throw std::runtime_error("unexpected extra or late SigLIP output");
  }
  GstBuffer* buffer = gst_buffer_new_allocate(nullptr, input.size(), nullptr);
  if (!buffer) throw std::runtime_error("cannot allocate appsrc buffer");
  gst_buffer_fill(buffer, 0, input.data(), input.size());
#ifdef AIBOX_SIGLIP_QTI_METADATA
  if (config_.postprocess_module == "siglip2-text") {
    const guint dims[GST_ML_TENSOR_MAX_DIMS] = {1, 64};
    auto* meta = gst_buffer_add_ml_tensor_meta(buffer, GST_ML_TYPE_INT32, 2, dims);
    if (!meta || input.size() != 64 * sizeof(std::int32_t)) {
      gst_buffer_unref(buffer);
      throw std::runtime_error("invalid text encoder input tensor");
    }
    meta->name = g_quark_from_static_string("input_ids");
  }
#endif
  const auto pts = next_pts_ns_;
  next_pts_ns_ += 1000000000;
  GST_BUFFER_PTS(buffer) = pts;
  GST_BUFFER_DTS(buffer) = pts;
  GST_BUFFER_DURATION(buffer) = 1000000000;
  const auto flow = gst_app_src_push_buffer(GST_APP_SRC(source_), buffer);
  if (flow != GST_FLOW_OK) throw std::runtime_error("SigLIP appsrc push failed");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    CheckBus();
    if (heartbeat) heartbeat();
    GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink_), 250 * GST_MSECOND);
    if (!sample) continue;
    GstBuffer* output = gst_sample_get_buffer(sample);
    GstMapInfo map{};
    if (!output || !gst_buffer_map(output, &map, GST_MAP_READ)) {
      gst_sample_unref(sample);
      throw std::runtime_error("SigLIP output map failed");
    }
    try {
      auto embedding = ParseEmbedding(
          reinterpret_cast<const char*>(map.data), map.size, pts,
          config_.postprocess_module == "siglip2-text" ? EmbeddingKind::Text : EmbeddingKind::Image);
      gst_buffer_unmap(output, &map);
      gst_sample_unref(sample);
      return embedding;
    } catch (...) {
      gst_buffer_unmap(output, &map);
      gst_sample_unref(sample);
      throw;
    }
  }
  throw std::runtime_error("SigLIP inference timeout; pipeline must exit");
}
void SiglipPipelineBuilder::WaitReady() {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.startup_timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    CheckBus();
    GstState current = GST_STATE_VOID_PENDING, pending = GST_STATE_VOID_PENDING;
    if (gst_element_get_state(pipeline_, &current, &pending, 100 * GST_MSECOND) == GST_STATE_CHANGE_FAILURE)
      throw std::runtime_error("SigLIP startup state failure");
    if (current == GST_STATE_PLAYING) return;
  }
  throw std::runtime_error("SigLIP PLAYING startup timeout");
}
void SiglipPipelineBuilder::Stop() {
  if (stopped_) return;
  stopped_ = true;
  bool clean = true;
  if (pipeline_) {
    if (gst_element_set_state(pipeline_, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE)
      clean = false;
    GstState current = GST_STATE_VOID_PENDING, pending = GST_STATE_VOID_PENDING;
    if (gst_element_get_state(pipeline_, &current, &pending, 10 * GST_SECOND) ==
        GST_STATE_CHANGE_FAILURE || current != GST_STATE_NULL) clean = false;
  }
  if (live_sink_) { gst_object_unref(live_sink_); live_sink_ = nullptr; }
  if (bus_) { gst_object_unref(bus_); bus_ = nullptr; }
  if (pipeline_) { gst_object_unref(pipeline_); pipeline_ = nullptr; }
  if (!clean) throw std::runtime_error("SigLIP pipeline did not reach NULL");
  std::cout << "[SIGLIP_SHUTDOWN_OK] state=NULL\n";
}
}  // namespace aibox::siglip
