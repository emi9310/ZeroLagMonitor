#pragma once
#include <atomic>
#include <array>
#include <thread>
#include <cstdint>
#include <functional>
#include <winsock2.h>

// Ring buffer SPSC (Single Producer Single Consumer) - lock-free
template<size_t SLOTS = 64, size_t MAX_PKT = 1024 * 1024>
class SPSCRingBuffer {
public:
    struct Slot {
        uint8_t  data[MAX_PKT];
        uint32_t size;
        uint64_t pts_us;
    };

    bool TryPush(const uint8_t* src, uint32_t sz, uint64_t pts) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) % SLOTS;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        auto& s = slots_[head];
        std::memcpy(s.data, src, sz);
        s.size   = sz;
        s.pts_us = pts;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool TryPop(Slot*& out) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = &slots_[tail];
        tail_.store((tail + 1) % SLOTS, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::array<Slot, SLOTS> slots_;
};

class AsyncSender {
public:
    enum class Mode {
        USB,    // TCP → localhost (ADB forward)  — cable USB-C
        WiFi,   // UDP → IP de la tablet          — red WiFi
    };

    struct Config {
        Mode        mode       = Mode::USB;
        const char* target_ip  = "127.0.0.1"; // USB: siempre localhost
        uint16_t    target_port = 9000;
    };

    explicit AsyncSender(Config cfg);
    ~AsyncSender();

    bool Init();
    void Enqueue(const uint8_t* data, size_t size, uint64_t pts_us);
    void Stop();

private:
    void SendLoopTCP();
    void SendLoopUDP();
    bool ConnectTCP();          // reconecta si se pierde la conexión

    Config             cfg_;
    SOCKET             sock_   = INVALID_SOCKET;
    sockaddr_in        dest_{};
    std::thread        thread_;
    std::atomic<bool>  running_{false};
    SPSCRingBuffer<>   ring_;
};
