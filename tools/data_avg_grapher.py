import os
import csv
import argparse
import matplotlib.pyplot as plt
import numpy as np

def read_csv(filepath):
    time, x, y, z = [], [], [], []

    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)

        x_col = y_col = z_col = time_col = None

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

        for row in reader:
            try:
                time.append(float(row[time_col]))
                x.append(float(row[x_col]))
                y.append(float(row[y_col]))
                z.append(float(row[z_col]))
            except:
                continue

    return np.array(time), np.array(x), np.array(y), np.array(z)

def average_folder(folder_path):
    times_list = []
    xs, ys, zs = [], [], []

    for file in os.listdir(folder_path):
        if not file.lower().endswith(".csv"):
            continue
        path = os.path.join(folder_path, file)
        t, x, y, z = read_csv(path)
        times_list.append(t)
        xs.append(x)
        ys.append(y)
        zs.append(z)

    # Find minimum length to align arrays
    min_len = min([len(t) for t in times_list])
    xs = np.array([x[:min_len] for x in xs])
    ys = np.array([y[:min_len] for y in ys])
    zs = np.array([z[:min_len] for z in zs])
    t = times_list[0][:min_len]

    avg_x = np.mean(xs, axis=0)
    avg_y = np.mean(ys, axis=0)
    avg_z = np.mean(zs, axis=0)

    return t, avg_x, avg_y, avg_z

def plot_folder_avg(folder_path, output_path):
    t, avg_x, avg_y, avg_z = average_folder(folder_path)

    plt.figure()
    plt.plot(t, avg_x, label="X")
    plt.plot(t, avg_y, label="Y")
    plt.plot(t, avg_z, label="Z")

    plt.xlabel("Time (s)")
    plt.ylabel("Acceleration (g)")
    plt.title("Average: " + os.path.basename(folder_path))
    plt.legend()
    plt.grid()
    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()

def process_folders(input_dir, output_dir):
    for root, dirs, _ in os.walk(input_dir):
        for d in dirs:
            folder_path = os.path.join(root, d)
            rel_path = os.path.relpath(folder_path, input_dir)
            out_subdir = os.path.join(output_dir)
            os.makedirs(out_subdir, exist_ok=True)
            out_path = os.path.join(out_subdir, f"{rel_path.replace(os.sep, '_')}_avg.png")

            try:
                plot_folder_avg(folder_path, out_path)
                print(f"Averaged plot created: {rel_path}")
            except Exception as e:
                print(f"Failed {rel_path}: {e}")

def main():
    parser = argparse.ArgumentParser(description="Generate averaged folder graphs")
    parser.add_argument("-i", "--input", required=True, help="Input directory")
    parser.add_argument("-o", "--output", required=True, help="Output directory")
    args = parser.parse_args()

    process_folders(args.input, args.output)

if __name__ == "__main__":
    main()
