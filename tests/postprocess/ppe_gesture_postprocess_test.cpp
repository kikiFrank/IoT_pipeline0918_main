// Compile the PPE postprocessing module and its test as one translation unit.
#include "../../plugins/mlpostprocess_roi/modules/object-detection/ml-postprocess-yolox-ppe.cc"

#include <stdexcept>

namespace {
void Check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  Module module([](uint32_t, const char*) {});
  Check(module.Configure(argv[1], R"({"confidence":25,"nms-iou":0.45})"),
        "PPE gesture configuration");

  std::vector<float> rows(8400 * 9, 0.0f);
  auto set_box = [&](int row, int class_id, float x, float probability) {
    float* values = rows.data() + row * 9;
    values[0] = x;
    values[1] = 100;
    values[2] = 50;
    values[3] = 70;
    values[4] = 1;
    values[5 + class_id] = probability;
  };

  // Two overlapping anchors for person A, and a different person B.
  set_box(0, 0, 100, 0.7f);
  set_box(1, 3, 103, 0.9f);
  set_box(2, 0, 350, 0.8f);
  set_box(3, 1, 100, 0.6f);
  set_box(4, 2, 100, 0.5f);

  Tensor tensor;
  tensor.type = kFloat32;
  tensor.name = "output";
  tensor.dimensions = {1, 8400, 9};
  tensor.data = rows.data();
  Dictionary params;
  params["input-tensor-region"] = Region(0, 0, 640, 360);

  std::any output = ObjectDetections{};
  Check(module.Process({tensor}, params, output), "decode candidates");
  const auto& detections = std::any_cast<const ObjectDetections&>(output);
  Check(detections.size() == 4 && detections[0].name == "gesture" &&
        detections[1].name == "human" && detections[1].left > 0.5f &&
        detections[2].name == "white_hat" && detections[3].name == "white_cloth",
        "gesture wins for A; human for B and both PPE classes remain");

  set_box(0, 0, 100, 0.95f);
  tensor.name = "unknown";  // The board's qtimlqnn FLOAT32 path omits the name.
  output = ObjectDetections{};
  Check(module.Process({tensor}, params, output), "decode unnamed FLOAT32 output");
  const auto& reversed = std::any_cast<const ObjectDetections&>(output);
  Check(reversed.size() == 4 && reversed[0].name == "human" &&
        reversed[1].name == "human", "human wins for A when confidence is higher");

  tensor.name = "unexpected_output";
  output = ObjectDetections{};
  Check(!module.Process({tensor}, params, output), "reject unexpected named output");
  tensor.name = "unknown";
  tensor.type = kUint16;
  Check(!module.Process({tensor}, params, output), "reject unnamed quantized output");
}
