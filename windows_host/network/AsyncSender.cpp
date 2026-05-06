#include "AsyncSender.hpp"
#include <ws2tcpip.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

// Header compartido con Receiver.cpp de Android
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;   // 0x4D595252 "MYRR"
    uint32_t size;
    uint64_t pts_us;
};
#pragma pack(pop)
static constexpr uint32_t PACKET_MAGIC = 0x4D595252u;

AsyncSender::AsyncSender(Config cfg) : cfg_(std::move(cfg)) {}
AsyncSender::~AsyncSender() { Stop(); }

bool AsyncSender::Init() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::memset(&dest_, 0, sizeof(dest_));
    dest_.sin_family = AF_INET;
    dest_.sin_port   = htons(cfg_.target_port);
    inet_pton(AF_INET, cfg_.target_ip, &dest_.sin_addr);

    running_ = true;

    if (cfg_.mode == Mode::USB) {
        // TCP: el hilo intenta conectar y reconectar si se pierde el cable
        thread_ = std::thread(&AsyncSender::SendLoopTCP, this);
        std::printf("[Sender] Modo USB — TCP 127.0.0.1:%u\n", cfg_.target_port);
    } else {
        // UDP: crear socket de una vez
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        int sndbuf = 4 * 1024 * 1024;
        setsockopt(sock_, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));
        thread_ = std::thread(&AsyncSender::SendLoopUDP, this);
        std::printf("[Sender] Modo WiFi — UDP %s:%u\n",
                    cfg_.target_ip, cfg_.target_port);
    }
    return true;
}

bool AsyncSender::ConnectTCP() {
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;

    // TCP_NODELAY: deshabilita el algoritmo de Nagle — latencia mínima
    int flag = 1;
    setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));

    // Buffer de envío 4 MB
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));

    if (connect(sock_, reinterpret_cast<sockaddr*>(&dest_), sizeof(dest_)) != 0) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    std::printf("[Sender] TCP conectado a 127.0.0.1:%u (ADB tunnel)\n",
                cfg_.target_port);
    return true;
}

void AsyncSender::SendLoopTCP() {
    // Buffer local: header + payload
    static thread_local std::vector<uint8_t> send_buf;

    while (running_) {
        // ── Conectar / reconectar ────────────────────────────────
        if (sock_ == INVALID_SOCKET) {
            if (!ConnectTCP()) {
                std::printf("[Sender] Esperando ADB forward... "
                            "(ejecuta: adb forward tcp:%u tcp:%u)\n",
                            cfg_.target_port, cfg_.target_port);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        SPSCRingBuffer<>::Slot* slot = nullptr;
        if (!ring_.TryPop(slot)) {
            std::this_thread::yield();
            continue;
        }

        // ── Construir paquete: header + payload ──────────────────
        PacketHeader hdr{};
        hdr.magic  = PACKET_MAGIC;
        hdr.size   = slot->size;
        hdr.pts_us = slot->pts_us;

        size_t total = sizeof(hdr) + slot->size;
        send_buf.resize(total);
        std::memcpy(send_buf.data(), &hdr, sizeof(hdr));
        std::memcpy(send_buf.data() + sizeof(hdr), slot->data, slot->size);

        // ── Enviar completo (TCP puede fragmentar) ───────────────
        size_t sent = 0;
        while (sent < total && running_) {
            int n = send(sock_,
                         reinterpret_cast<const char*>(send_buf.data() + sent),
                         static_cast<int>(total - sent), 0);
            if (n <= 0) {
                // Conexión rota — reconectar en el próximo ciclo
                std::printf("[Sender] Conexión TCP perdida, reconectando...\n");
                closesocket(sock_);
                sock_ = INVALID_SOCKET;
                break;
            }
            sent += static_cast<size_t>(n);
        }
    }
}

void AsyncSender::SendLoopUDP() {
    static thread_local std::vector<uint8_t> send_buf;

    SPSCRingBuffer<>::Slot* slot = nullptr;
    while (running_) {
        if (!ring_.TryPop(slot)) {
            std::this_thread::yield();
            continue;
        }

        PacketHeader hdr{};
        hdr.magic  = PACKET_MAGIC;
        hdr.size   = slot->size;
        hdr.pts_us = slot->pts_us;

        size_t total = sizeof(hdr) + slot->size;
        send_buf.resize(total);
        std::memcpy(send_buf.data(), &hdr, sizeof(hdr));
        std::memcpy(send_buf.data() + sizeof(hdr), slot->data, slot->size);

        sendto(sock_,
               reinterpret_cast<const char*>(send_buf.data()),
               static_cast<int>(total), 0,
               reinterpret_cast<const sockaddr*>(&dest_), sizeof(dest_));
    }
}

void AsyncSender::Enqueue(const uint8_t* data, size_t size, uint64_t pts_us) {
    if (!running_) return;
    if (!ring_.TryPush(data, static_cast<uint32_t>(size), pts_us)) {
        static uint32_t drops = 0;
        if (++drops % 60 == 0)
            std::fprintf(stderr, "[Sender] %u frames dropped (ring lleno)\n", drops);
    }
}

void AsyncSender::Stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    WSACleanup();
}
