#include "../../plugins/mlpostprocess_roi/modules/object-detection/ml-postprocess-yolox-ppe.cc"

#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  Module module([](uint32_t, const char*) {});
  Check(module.Configure(argv[1], R"({"confidence":25,"nms-iou":0.65})"),
        "load four Light labels");

  constexpr std::size_t kColumns = 9;
  std::vector<float> rows(8400 * kColumns, 0.0f);
  auto add_box = [&](std::size_t row, std::size_t class_id, float x, float score) {
    float* values = rows.data() + row * kColumns;
    values[0] = x;
    values[1] = 100.0f;
    values[2] = 40.0f;
    values[3] = 60.0f;
    values[4] = 0.9f;
    values[5 + class_id] = score;
  };

  Tensor tensor;
  tensor.name = "output";
  tensor.type = kFloat32;
  tensor.dimensions = {1, 8400, kColumns};
  tensor.data = rows.data();
  Dictionary params;
  params["input-tensor-region"] = Region(0, 0, 640, 640);
  std::any output = ObjectDetections{};
  Check(module.Process({tensor}, params, output) &&
        std::any_cast<const ObjectDetections&>(output).empty(), "zero detections");

  add_box(0, 0, 100, 0.9f);
  add_box(1, 0, 101, 0.8f);  // Same class and location: suppressed by NMS.
  add_box(2, 1, 100, 0.7f);  // Another class: kept at the same location.
  add_box(3, 2, 200, 0.6f);
  add_box(4, 3, 300, 0.5f);
  add_box(5, 3, 400, 0.2f);  // Below the confidence threshold.
  auto check_detections = [&] {
    const auto& detections = std::any_cast<const ObjectDetections&>(output);
    Check(detections.size() == 4, "class-wise NMS and confidence filter");
    Check(detections[0].name == "red" && detections[1].name == "yellow" &&
          detections[2].name == "green" && detections[3].name == "none",
          "Light class order");
    Check(detections[0].color == 0xFF0000FF && detections[1].color == 0xFFFF00FF &&
          detections[2].color == 0x00FF00FF && detections[3].color == 0x808080FF,
          "Light color mapping");
  };

  Check(module.Process({tensor}, params, output), "decode FLOAT32");
  check_detections();

  // Use independent synthetic tensor quantization so this test stays stable
  // when a new model is trained.
  constexpr float kScale = 0.01f;
  std::vector<uint16_t> quantized;
  quantized.reserve(rows.size());
  for (float value : rows) {
    quantized.push_back(static_cast<uint16_t>(std::lround(value / kScale)));
  }
  tensor.type = kUint16;
  tensor.qscale = kScale;
  tensor.qoffset = 0;
  tensor.data = quantized.data();
  output = ObjectDetections{};
  Check(module.Process({tensor}, params, output), "decode UINT16");
  check_detections();

  tensor.dimensions = {1, 8400, 6};
  Check(!module.Process({tensor}, params, output), "reject old Stacklight tensor shape");
}
