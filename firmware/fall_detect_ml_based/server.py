import socket
import sys
import os
from time import sleep

HOST = "0.0.0.0"
PORT = 3045


def handle_client(conn, addr):
    global file_index

    print(f"Connected by {addr}")
    data = b""

    try:
        chunk = conn.recv(2048)
        if not chunk:
            return
        data += chunk
    except ConnectionResetError:
        print("Connection reset by client")
        return

    msg = data.split(b'\x00', 1)[0]   # take everything before first null
    print(msg.decode())

    # Give lwIP client time
    sleep(0.05)

    # Respond
    response = b'{"Status":0}'
    conn.sendall(response)
    print("Sent JSON with Status = 0")

    # Keep connection alive briefly so lwIP doesn't see RST
    sleep(0.1)


with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen()
    print(f"Listening on {HOST}:{PORT}...")

    while True:
        conn, addr = s.accept()
        with conn:
            handle_client(conn, addr)
