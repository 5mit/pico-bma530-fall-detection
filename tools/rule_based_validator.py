import os
import argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# Constants based on BMA530 config for rule based fall detector
SCALE = 0.000122070
FREEFALL_G = 0.5 # less than 0.5 g on all axes
IMPACT_G = 2.0  # more than 2.0g on one axis
MAX_LOOKAHEAD_FOR_IMPACT = 2000 # number of seconds between last free_fall detected and impact
DT = 20  # ms per sample

FREEFALL_SAMPLES = int(300 / DT)   # 300ms
IMPACT_SAMPLES = int(30 / DT)      # 30ms
# Compute max number of samples to look ahead for impact (2 seconds)
MAX_LOOKAHEAD = int(MAX_LOOKAHEAD_FOR_IMPACT / DT)  # 2 seconds / sample interval


def detect_event(filepath):
    try:
        df = pd.read_csv(filepath)

        x = df.iloc[:, 2].values
        y = df.iloc[:, 3].values
        z = df.iloc[:, 4].values

        # Convert to g
        x_g = x * SCALE
        y_g = y * SCALE
        z_g = z * SCALE

        # Magnitude (for freefall)
        mag = np.sqrt(x_g**2 + y_g**2 + z_g**2)

        # Conditions
        freefall = mag < FREEFALL_G
        impact = (np.abs(x_g) > IMPACT_G) | (np.abs(y_g) > IMPACT_G) | (np.abs(z_g) > IMPACT_G)



        ff_count = 0
        for i in range(len(mag)):
            if freefall[i]:
                ff_count += 1
            else:
                ff_count = 0

            # Stop at first freefall segment long enough
            if ff_count >= FREEFALL_SAMPLES:

                # Look for impact within 2 seconds after freefall start
                end_idx = min(i + MAX_LOOKAHEAD,  len(mag))
                for j in range(i, end_idx):
                    if impact[j]:
                        return True  # impact occurred within 2 seconds of valid freefall

        # No valid freefall found
        return False

    except Exception as e:
        print(f"Error processing {filepath}: {e}")
        return False



def get_label_bool(filepath):
    try:
        df = pd.read_csv(filepath, nrows=1)

        if "label" not in df.columns:
            raise ValueError("Missing 'label' column")

        label = int(df["label"].iloc[0])

        if label == 1:
            return True
        elif label == 0:
            return False
        else:
            raise ValueError(f"Unexpected label value: {label}")

    except Exception as e:
        print(f"Error reading label from {filepath}: {e}")
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input", required=True, help="Folder of CSV files")
    args = parser.parse_args()

    total = 0
    detected = 0

    # Evaluation counts
    tp = 0 # True Positive
    fp = 0 # False Positive
    tn = 0 # True Negative
    fn = 0 # False Negative

    for root, _, files in os.walk(args.input):
        for f in files:
            if not f.endswith(".csv"):
                continue

            path = os.path.join(root, f)
            total += 1

            has_event = detect_event(path)
            if has_event:
                detected += 1
            is_fall = get_label_bool(path)

            if has_event == is_fall:
                if is_fall:
                    tp += 1
                else:
                    tn += 1
            else:
                if is_fall:
                    fn += 1
                else:
                    fp += 1

    cm = np.array([
        
        [tn, fp],  # Actual Negative
        [fn, tp],  # Actual Positive
    ])

    plt.figure()
    sns.heatmap(cm, annot=True, fmt="d", cmap="Blues",
                xticklabels=["No Fall", "Fall"],
                yticklabels=["No Fall", "Fall"])

    plt.xlabel("Predicted")
    plt.ylabel("Actual")
    plt.title("Confusion Matrix (Rule-Based Detector)")
    plt.tight_layout()
    plt.savefig("confusion_matrix_rule_based.png")
    plt.close()
            

    print("\nSummary:")
    print(f"Total files: {total}")
    print(f"Detected events: {detected}")
    print(f"Detection rate: {detected / total * 100:.2f}%")
    print(f"Accuracy: {((tp + tn) / total) * 100:.2f}%")
    
    print(f"True Negative Rate: {tn / total * 100:.2f}%")
    print(f"False Positive Rate: {fp / total * 100:.2f}%")
    print(f"False Negative Rate: {fn / total * 100:.2f}%")
    print(f"True Positive Rate: {tp / total * 100:.2f}%")


if __name__ == "__main__":
    main()
