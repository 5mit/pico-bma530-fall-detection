import os
import csv
import argparse

SCALE = 0.000122070 # BMA530 ACC at 4G Full Scale -> 122.070 micro g/LSB
DT = 0.020 # BMA530 ACC at 50Hz Sample rate -> delta time of 20ms between samples

def hex_to_int16(lo, hi):
    """Convert 2 bytes (little endian) to signed int16"""
    val = (hi << 8) | lo
    if val >= 0x8000:
        val -= 0x10000
    return val

def parse_file(filepath, decode=False):
    with open(filepath, 'r') as f:
        lines = [line.strip() for line in f if line.strip()]

    # ---- Label ----
    label = int(lines[0])

    # Flatten all hex tokens after label
    tokens = []
    for line in lines[1:]:
        parts = line.split()
        for p in parts:
            p = p.lower()

            # skip markers
            if p in ("header:", "data:"):
                continue

            # keep only valid hex bytes
            if len(p) == 2:
                try:
                    int(p, 16)
                    tokens.append(p)
                except ValueError:
                    continue

    # Convert to integers
    bytes_data = [int(x, 16) for x in tokens]

    # ---- Header ---- (Unused)
    header = bytes_data[:64]

    #fifo_len = int.from_bytes(header[0:4], byteorder='little')
    #timestamp = int.from_bytes(header[8:14], byteorder='little')

    # ---- Data ----
    data_bytes = bytes_data[64:]

    rows = []
    n_samples = 0
    i = 0

    if decode:
        dt = DT
    else:
        dt = DT * 1000

    while i + 6 < len(data_bytes):
        chunk = data_bytes[i:i+7]

        # Stop if chunk is all zeros
        if all(b == 0 for b in chunk):
            break

        # Validate frame
        if chunk[0] != 0xCE:
            i += 1
            continue

        x_raw = hex_to_int16(chunk[1], chunk[2])
        y_raw = hex_to_int16(chunk[3], chunk[4])
        z_raw = hex_to_int16(chunk[5], chunk[6])
        if decode:
            x = x_raw * SCALE
            y = y_raw * SCALE
            z = z_raw * SCALE
        else:
            x,y,z = x_raw, y_raw, z_raw


        rows.append([label, n_samples * dt, x, y, z])

        n_samples += 1
        i += 7

    return rows


def process_folder(input_dir, output_dir, decode: bool):
    print(decode)
    for root, _, files in os.walk(input_dir):
        # Compute relative path from input root
        rel_path = os.path.relpath(root, input_dir)

        # Build corresponding output directory
        out_subdir = os.path.join(output_dir, rel_path)
        os.makedirs(out_subdir, exist_ok=True)

        for file in files:
            if not file.lower().endswith((".txt", ".log", ".dat")):
                continue

            in_path = os.path.join(root, file)

            try:
                rows = parse_file(in_path, decode)

                if not rows:
                    continue

                # Output path mirrors input structure
                out_name = os.path.splitext(file)[0] + ".csv"
                out_path = os.path.join(out_subdir, out_name)

                with open(out_path, 'w', newline='') as f:
                    writer = csv.writer(f)
                    writer.writerow(["label", "time_s" if decode else "time_ms", "x_g" if decode else "x_lsb", "y_g" if decode else "y_lsb", "z_g" if decode else "z_lsb"])
                    writer.writerows(rows)

                print(f"Processed: {os.path.join(rel_path, file)} ({len(rows)} rows)")

            except Exception as e:
                print(f"Failed: {os.path.join(rel_path, file)} -> {e}")

def main():
    parser = argparse.ArgumentParser(description="Convert hex sensor dumps to CSV")
    parser.add_argument(
        "-i", "--input",
        required=True,
        help="Input directory containing data files"
    )

    parser.add_argument(
        "-o", "--output",
        required=True,
        help="Output directory for CSV files"
    )

    parser.add_argument(
        "-d", "--decode",
        action="store_true",
        required=False,
        help="Decodes x,y,z from raw LSB units to (g)"
    )

    args = parser.parse_args()

    process_folder(args.input, args.output, args.decode)


if __name__ == "__main__":
    main()
