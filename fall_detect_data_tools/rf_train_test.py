import argparse
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report
from micromlgen import port

def main():
    parser = argparse.ArgumentParser(description="Train and test Random Forest on feature CSVs")
    parser.add_argument("-tr", "--train", required=True, help="Training features CSV")
    parser.add_argument("-te", "--test", required=True, help="Testing features CSV")
    parser.add_argument("-n", "--n_estimators", type=int, default=100, help="Number of trees in the forest")
    parser.add_argument("-s", "--seed", type=int, default=42, help="Random seed for reproducibility")
    args = parser.parse_args()

    # Load datasets
    train_df = pd.read_csv(args.train)
    test_df = pd.read_csv(args.test)

    if "label" not in train_df.columns or "label" not in test_df.columns:
        raise ValueError("Both CSVs must contain a 'label' column")

    X_train = train_df.drop(columns=["label"])
    y_train = train_df["label"]
    X_test = test_df.drop(columns=["label"])
    y_test = test_df["label"]

    # Train Random Forest
    clf = RandomForestClassifier(n_estimators=args.n_estimators, random_state=args.seed)
    clf.fit(X_train, y_train)

    c_code = port(clf, classmap={0: "0", 1: "1"})
    with open("rf_model.h", "w") as f:
        f.write(c_code)

    # Predict on test set
    y_pred = clf.predict(X_test)

    # Evaluation
    acc = accuracy_score(y_test, y_pred)
    report = classification_report(y_test, y_pred)

    print(f"Random Forest Accuracy: {acc*100:.2f}%")
    print("Classification Report:")
    print(report)

if __name__ == "__main__":
    main()