#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aibox::adapter { class NatsAdapter; }

namespace aibox::media {

class SnapshotZoneOverlay {
 public:
  explicit SnapshotZoneOverlay(adapter::NatsAdapter& nats);
  ~SnapshotZoneOverlay();
  SnapshotZoneOverlay(const SnapshotZoneOverlay&) = delete;
  SnapshotZoneOverlay& operator=(const SnapshotZoneOverlay&) = delete;

  bool Start();
  void Stop();
  void Draw(const std::string& camera_id, std::vector<uint8_t>& packed_nv12,
            uint32_t width, uint32_t height) const;

 private:
  struct State;

  void Refresh();
  adapter::NatsAdapter& nats_;
  const std::shared_ptr<State> state_;
  const std::string inbox_;
  std::mutex mutex_;
  std::condition_variable stop_;
  bool stopping_ = false;
  std::thread refresh_thread_;
};

}  // namespace aibox::media
