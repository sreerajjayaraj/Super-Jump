#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace superjump {

// The frame callback never takes the runtime state lock to publish a movement
// sample. The queue lock protects a short fixed-array copy and target changes.
class JumpFrameQueue {
public:
    struct Frame {
        std::uint64_t tick;
        float deltaY;
    };

    struct Batch {
        std::array<Frame, 32> frames{};
        std::size_t count{};
        unsigned missed{};
        std::span<const Frame> Samples() const { return std::span(frames).first(count); }
    };

    void SetTarget(std::uintptr_t body, std::uint64_t now) {
        std::lock_guard lock(mutex_);
        since_ = now;
        batch_.count = 0;
        missed_.store(0, std::memory_order_relaxed);
        target_.store(body, std::memory_order_release);
    }

    std::uintptr_t Target() const { return target_.load(std::memory_order_acquire); }

    void Capture(std::uintptr_t body, Frame frame) {
        if (!body || Target() != body) return;
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock) {
            missed_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (Target() != body || frame.tick <= since_) return;
        if (batch_.count == batch_.frames.size()) {
            missed_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        batch_.frames[batch_.count++] = frame;
    }

    Batch Drain() {
        std::lock_guard lock(mutex_);
        auto batch = batch_;
        batch.missed = missed_.exchange(0, std::memory_order_relaxed);
        batch_.count = 0;
        return batch;
    }

private:
    std::mutex mutex_;
    std::atomic<std::uintptr_t> target_{};
    std::uint64_t since_{};
    Batch batch_;
    std::atomic<unsigned> missed_{};
};

} // namespace superjump
