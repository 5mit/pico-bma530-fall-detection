import os
import random
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# Constants for acceleration conversion
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


def load_files(folder):
    files = [os.path.join(folder, f) for f in os.listdir(folder) if f.endswith(".csv")]
    return files


def main(real_folder, sim_folder, seed=42):
    random.seed(seed)

    real_files = load_files(real_folder)
    sim_files = load_files(sim_folder)

    # Extract features
    real_feats = [extract_features(f) for f in real_files]
    sim_feats = [extract_features(f) for f in sim_files]

    # Drop any failed files
    real_feats = [f for f in real_feats if f is not None]
    sim_feats = [f for f in sim_feats if f is not None]

    df_real = pd.DataFrame(real_feats)
    df_sim = pd.DataFrame(sim_feats)

    print(f"Real samples: {len(df_real)}, Simulated samples: {len(df_sim)}")

    # Compare distributions for each feature
    features = df_real.columns
    for feat in features:
        plt.figure(figsize=(6,4))
        sns.kdeplot(df_real[feat], label="Real", fill=True)
        sns.kdeplot(df_sim[feat], label="Simulated", fill=True)
        plt.title(f"Feature distribution: {feat}")
        plt.xlabel(feat)
        plt.ylabel("Density")
        plt.legend()
        plt.tight_layout()
        plt.savefig(f"feature_{feat}.png")
        plt.close()

        # Print basic statistics
        print(f"\nFeature: {feat}")
        print("Real: mean {:.3f}, std {:.3f}".format(df_real[feat].mean(), df_real[feat].std()))
        print("Sim:  mean {:.3f}, std {:.3f}".format(df_sim[feat].mean(), df_sim[feat].std()))

    # Optional: calculate overall difference (e.g., KS test)
    from scipy.stats import ks_2samp
    print("\nKolmogorov-Smirnov test (distribution similarity)")
    for feat in features:
        ks_stat, p_val = ks_2samp(df_real[feat], df_sim[feat])
        print(f"{feat}: KS stat={ks_stat:.3f}, p={p_val:.3f}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("-r", "--real", required=True, help="Folder of real fall CSVs")
    parser.add_argument("-s", "--sim", required=True, help="Folder of simulated fall CSVs")
    args = parser.parse_args()

    main(args.real, args.sim)
