import socket, struct, time

TABLET_IP = "192.168.1.107"
PORT      = 9000
MAGIC     = 0x4D595252  # "MYRR"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
print(f"Enviando a {TABLET_IP}:{PORT}...")

for i in range(10):
    payload = b"\x00" * 100
    header  = struct.pack("<IIQ", MAGIC, len(payload), i * 1000)
    sock.sendto(header + payload, (TABLET_IP, PORT))
    print(f"  paquete {i} enviado")
    time.sleep(0.5)

print("Listo.")
