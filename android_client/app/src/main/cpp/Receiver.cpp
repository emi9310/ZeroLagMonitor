#include "Receiver.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <android/log.h>
#include <cstring>
#include <vector>

#define LOG_TAG "MyrrorReceiver"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

Receiver::Receiver(int port) : port_(port) {}
Receiver::~Receiver() { Stop(); }

bool Receiver::Start(NALCallback cb, DisconnectCallback on_disconnect) {
    callback_     = std::move(cb);
    on_disconnect_ = std::move(on_disconnect);

    // ── TCP server socket ────────────────────────────────────────
    // ADB forward conecta desde el PC al puerto TCP de la tablet.
    // La tablet actúa como SERVER — acepta la conexión del PC.
    server_sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock_ < 0) {
        LOGE("socket() falló: %s", strerror(errno));
        return false;
    }

    int reuse = 1;
    ::setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (::bind(server_sock_,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOGE("bind() en puerto %d falló: %s", port_, strerror(errno));
        ::close(server_sock_);
        server_sock_ = -1;
        return false;
    }

    ::listen(server_sock_, 1); // solo 1 conexión (el PC)

    running_ = true;
    thread_  = std::thread(&Receiver::RecvLoop, this);
    LOGI("Escuchando TCP en puerto %d (modo USB-C / ADB)", port_);
    return true;
}

void Receiver::Stop() {
    if (!running_) return;
    running_ = false;
    if (server_sock_ >= 0) { ::close(server_sock_); server_sock_ = -1; }
    if (client_sock_ >= 0) { ::close(client_sock_); client_sock_ = -1; }
    if (thread_.joinable()) thread_.join();
    LOGI("Receiver detenido.");
}

// Lee exactamente `n` bytes del socket TCP (maneja fragmentación)
static bool RecvAll(int fd, uint8_t* buf, size_t n) {
    size_t received = 0;
    while (received < n) {
        ssize_t r = ::recv(fd, buf + received, n - received, 0);
        if (r <= 0) return false; // conexión cerrada o error
        received += static_cast<size_t>(r);
    }
    return true;
}

void Receiver::RecvLoop() {
    std::vector<uint8_t> payload_buf;
    uint64_t pkt_count = 0;

    while (running_) {
        // ── Esperar conexión del PC ──────────────────────────────
        LOGI("Esperando conexión del PC por ADB...");
        client_sock_ = ::accept(server_sock_, nullptr, nullptr);
        if (client_sock_ < 0) break;

        // TCP_NODELAY en el lado Android también
        int flag = 1;
        ::setsockopt(client_sock_, IPPROTO_TCP, TCP_NODELAY,
                     &flag, sizeof(flag));

        // Buffer de recepción 4 MB
        int rcvbuf = 4 * 1024 * 1024;
        ::setsockopt(client_sock_, SOL_SOCKET, SO_RCVBUF,
                     &rcvbuf, sizeof(rcvbuf));

        LOGI("PC conectado — stream iniciado");

        // ── Loop de recepción ────────────────────────────────────
        PacketHeader hdr{};
        while (running_) {
            // 1. Leer header (16 bytes exactos)
            if (!RecvAll(client_sock_,
                         reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr))) {
                LOGI("PC desconectado.");
                break;
            }

            if (hdr.magic != PACKET_MAGIC) {
                LOGE("Magic incorrecto: 0x%08X", hdr.magic);
                break;
            }

            // 2. Leer payload
            payload_buf.resize(hdr.size);
            if (!RecvAll(client_sock_, payload_buf.data(), hdr.size)) {
                LOGI("PC desconectado durante payload.");
                break;
            }

            // 3. Log cada 60 paquetes
            ++pkt_count;
            if (pkt_count == 1 || pkt_count % 60 == 0)
                LOGI("Paquete #%llu — %u bytes, pts=%llu",
                     (unsigned long long)pkt_count,
                     hdr.size,
                     (unsigned long long)hdr.pts_us);

            // 4. Entregar al decoder
            callback_(payload_buf.data(), hdr.size, hdr.pts_us);
        }

        ::close(client_sock_);
        client_sock_ = -1;

        // PC se desconectó voluntariamente — avisar a la UI (no si Stop() fue llamado)
        if (running_ && on_disconnect_) on_disconnect_();
    }
}
