#pragma once
#include <cstdint>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>

using NALCallback        = std::function<void(const uint8_t* data, size_t size, uint64_t pts_us)>;
using DisconnectCallback = std::function<void()>;

// Header compartido con AsyncSender.cpp de Windows — 16 bytes
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;   // 0x4D595252 "MYRR"
    uint32_t size;    // bytes del payload NAL
    uint64_t pts_us;  // timestamp en microsegundos
};
#pragma pack(pop)

static constexpr uint32_t PACKET_MAGIC = 0x4D595252u;
static constexpr int      DEFAULT_PORT = 9000;

// ─────────────────────────────────────────────────────────────────
// Receiver — servidor TCP en modo USB-C / ADB forward
//
// La tablet actúa como servidor TCP. El PC (Windows) se conecta
// a través del túnel ADB:
//   PC → TCP 127.0.0.1:9000 → [ADB USB] → TCP tablet:9000
//
// Comando ADB necesario en el PC (una sola vez por sesión):
//   adb forward tcp:9000 tcp:9000
// ─────────────────────────────────────────────────────────────────
class Receiver {
public:
    explicit Receiver(int port = DEFAULT_PORT);
    ~Receiver();

    bool Start(NALCallback cb, DisconnectCallback on_disconnect = nullptr);
    void Stop();
    bool IsRunning() const { return running_; }

private:
    void RecvLoop();

    int                port_;
    int                server_sock_ = -1;  // acepta conexiones
    int                client_sock_ = -1;  // conexión activa del PC
    std::atomic<bool>  running_{false};
    std::thread        thread_;
    NALCallback        callback_;
    DisconnectCallback on_disconnect_;
};
