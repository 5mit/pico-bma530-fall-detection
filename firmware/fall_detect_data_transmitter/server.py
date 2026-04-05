import socket
import sys
import os
from time import sleep

HOST = "0.0.0.0"
PORT = 3045

if len(sys.argv) != 4:
    print("Usage: python server.py <folder_path> <start_file_number> <label (1 or 0)>")
    sys.exit(1)

folder_path = sys.argv[1]
file_index = int(sys.argv[2])
label = sys.argv[3]

if label not in ["0", "1"]:
    print("Label must be 0 or 1")
    sys.exit(1)

os.makedirs(folder_path, exist_ok=True)


def save_data(data, file_index):
    file_path = os.path.join(folder_path, f"{file_index}.txt")

    with open(file_path, "w") as f:
        # First line = label
        f.write(f"{label}\n")

        # Header
        f.write("Header:\n")
        header = data[:64]
        f.write(" ".join(f"{b:02x}" for b in header) + "\n")

        # Data
        f.write("Data:\n")
        for i in range(64, len(data), 7):
            row = data[i:i+7]
            f.write(" ".join(f"{b:02x}" for b in row) + "\n")

    print(f"Saved to {file_path}")


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

    # Save data to file
    save_data(data, file_index)
    file_index += 1

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
