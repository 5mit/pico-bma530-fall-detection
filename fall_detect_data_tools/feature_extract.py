import os
import argparse
import random
import shutil
import numpy as np
import pandas as pd

SATURATION = (2**16 // 2) - 1
SCALE = 0.000122070
FREEFALL_G = 0.5
FREEFALL_THR = (FREEFALL_G / SCALE) ** 2
DT = 20

PRE_IMPACT_WINDOW_MS = 200
POST_IMPACT_WINDOW_MS = 400


def extract_features(filepath):
    df = pd.read_csv(filepath)
    label = int(df["label"].iloc[0])

    x = df.iloc[:, 2].values
    y = df.iloc[:, 3].values
    z = df.iloc[:, 4].values

    mask = (np.abs(x) != SATURATION) & (np.abs(y) != SATURATION) & (np.abs(z) != SATURATION)
    x, y, z = x[mask], y[mask], z[mask]

    if len(x) < 3:
        return None

    mag = x*x + y*y + z*z

    feats = {
        "mag_mean": np.mean(mag),
        "mag_mad": np.mean(np.abs(mag - np.mean(mag))),
        "mag_max": np.max(mag),
        "mag_min": np.min(mag),
        "mag_range": np.max(mag) - np.min(mag),
    }

    # Freefall detection
    freefall_mask = mag < FREEFALL_THR
    feats["freefall_samples"] = np.sum(freefall_mask)

    # Impact detection
    if np.any(freefall_mask):
        last_ff_idx = np.where(freefall_mask)[0][-1]
        candidate_mag = mag[last_ff_idx:]
        impact_idx = last_ff_idx + np.argmax(candidate_mag)
    else:
        impact_idx = np.argmax(mag)

    feats["impact_mag"] = mag[impact_idx]

    # Pre/post-impact windows
    window_pre = int(PRE_IMPACT_WINDOW_MS / DT)
    start_pre = max(0, impact_idx - window_pre)
    pre_window = mag[start_pre:impact_idx]
    feats["pre_impact_mean"] = np.mean(pre_window) if len(pre_window) > 0 else feats["mag_mean"]
    feats["pre_impact_mad"] = np.mean(np.abs(pre_window - np.mean(pre_window))) if len(pre_window) > 1 else feats["mag_mad"]

    window_post = int(POST_IMPACT_WINDOW_MS / DT)
    end_post = min(len(mag), impact_idx + window_post)
    post_window = mag[impact_idx:end_post]
    feats["post_impact_mean"] = np.mean(post_window) if len(post_window) > 1 else feats["mag_mad"]
    feats["post_impact_mad"] = np.mean(np.abs(post_window - np.mean(post_window))) if len(post_window) > 1 else feats["mag_mad"]

    # Impact rise
    local_min = np.min(pre_window) if len(pre_window) > 0 else feats["mag_min"]
    feats["impact_rise"] = feats["impact_mag"] - local_min

    # Jerk features
    jerk = np.diff(mag)
    feats["jerk_max"] = np.max(np.abs(jerk))
    feats["jerk_mean_abs"] = np.mean(np.abs(jerk))

    feats["label"] = label
    return feats


def find_files(root):
    fall = []
    nonfall = []
    for r, _, files in os.walk(root):
        for f in files:
            if not f.endswith(".csv"):
                continue
            path = os.path.join(r, f)
            try:
                df = pd.read_csv(path, nrows=1)
                label = int(df["label"].iloc[0])
                if label == 1:
                    fall.append(path)
                else:
                    nonfall.append(path)
            except:
                continue
    return fall, nonfall


def copy_files(files, output_folder, root):
    os.makedirs(output_folder, exist_ok=True)
    for f in files:
        rel_path = os.path.relpath(f, root)
        safe_name = rel_path.replace(os.sep, "_")
        dest_path = os.path.join(output_folder, safe_name)
        shutil.copy(f, dest_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input", required=True)
    parser.add_argument("-o", "--output", required=True, help="Folder to store train/test subfolders and CSVs")
    parser.add_argument("-p", "--percent", type=float, default=80, help="Percentage of each class for training")
    args = parser.parse_args()

    train_dir = os.path.join(args.output, "train_files")
    test_dir = os.path.join(args.output, "test_files")
    os.makedirs(train_dir, exist_ok=True)
    os.makedirs(test_dir, exist_ok=True)

    fall, nonfall = find_files(args.input)
    print("Found fall files:", len(fall))
    print("Found nonfall files:", len(nonfall))

    # Split train/test
    num_fall = int(len(fall) * args.percent / 100)
    num_nonfall = int(len(nonfall) * args.percent / 100)

    fall_train = random.sample(fall, num_fall)
    nonfall_train = random.sample(nonfall, num_nonfall)

    fall_test = list(set(fall) - set(fall_train))
    nonfall_test = list(set(nonfall) - set(nonfall_train))

    # Extract features and copy files
    train_feats = []
    for f in fall_train + nonfall_train:
        feats = extract_features(f)
        if feats:
            train_feats.append(feats)
            copy_files([f], train_dir, args.input)

    test_feats = []
    for f in fall_test + nonfall_test:
        feats = extract_features(f)
        if feats:
            test_feats.append(feats)
            copy_files([f], test_dir, args.input)

    # Save feature CSVs
    pd.DataFrame(train_feats).to_csv(os.path.join(args.output, "train_features.csv"), index=False)
    pd.DataFrame(test_feats).to_csv(os.path.join(args.output, "test_features.csv"), index=False)

    print(f"Training samples: {len(train_feats)}, Testing samples: {len(test_feats)}")
    print(f"Files copied to {train_dir} and {test_dir}")


if __name__ == "__main__":
    main()