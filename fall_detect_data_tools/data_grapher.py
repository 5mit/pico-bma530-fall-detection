import os
import csv
import argparse
import matplotlib.pyplot as plt


def read_csv(filepath):
    time = []
    x = []
    y = []
    z = []

    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)

        x_col = None
        y_col = None
        z_col = None

        # Detect column names dynamically
        for col in reader.fieldnames:
            if col.startswith("x_"):
                x_col = col
            elif col.startswith("y_"):
                y_col = col
            elif col.startswith("z_"):
                z_col = col
            elif col.startswith("time"):
                time_col = col

        if not all([x_col, y_col, z_col, time_col]):
            raise ValueError(f"Invalid columns in {filepath}")

        # Determine if data decoded:
        unit = "g" if "g" in x_col.title() else "lsb"

        for row in reader:
            try:
                time.append(float(row[time_col]))
                x.append(float(row[x_col]))
                y.append(float(row[y_col]))
                z.append(float(row[z_col]))
            except:
                continue

    return time, x, y, z, unit



def plot_file(csv_path, output_path, input_root):
    time, x, y, z, unit = read_csv(csv_path)

    if not time:
        return

    plt.figure()

    plt.plot(time, x, label="X")
    plt.plot(time, y, label="Y")
    plt.plot(time, z, label="Z")

    plt.xlabel(f"time ({"s" if unit == "g" else "ms"})")
    plt.ylabel(f"accel ({unit})")
    rel_path = os.path.relpath(csv_path, input_root)
    plt.title(str(rel_path))
    plt.legend()
    plt.grid()

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()


def process_folder(input_dir, output_dir):
    for root, _, files in os.walk(input_dir):
        rel_path = os.path.relpath(root, input_dir)

        out_subdir = os.path.join(output_dir, rel_path)
        os.makedirs(out_subdir, exist_ok=True)

        for file in files:
            if not file.lower().endswith(".csv"):
                continue

            in_path = os.path.join(root, file)

            try:
                out_name = os.path.splitext(file)[0] + ".png"
                out_path = os.path.join(out_subdir, out_name)

                plot_file(in_path, out_path, input_dir)

                print(f"Plotted: {os.path.join(rel_path, file)}")

            except Exception as e:
                print(f"Failed: {os.path.join(rel_path, file)} -> {e}")


def main():
    parser = argparse.ArgumentParser(description="Generate graphs from CSV data")

    parser.add_argument(
        "-i", "--input",
        required=True,
        help="Input directory containing CSV files"
    )

    parser.add_argument(
        "-o", "--output",
        required=True,
        help="Output directory for graph images"
    )

    args = parser.parse_args()

    process_folder(args.input, args.output)


if __name__ == "__main__":
    main()
