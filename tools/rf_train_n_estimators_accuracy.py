import argparse
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from micromlgen import port

def main():

    parser = argparse.ArgumentParser(description="Train and test Random Forest on feature CSVs")
    parser.add_argument("-tr", "--train", required=True, help="Training features CSV")
    parser.add_argument("-te", "--test", required=True, help="Testing features CSV")
    parser.add_argument("-n", "--n_estimators_range", type=range, default=range(1,200,1), help="Number of trees in the forest")
    parser.add_argument("-s", "--seed", type=int, default=42, help="Seed for reproducibility")
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

    accuracies = []
    tree_counts = list(args.n_estimators_range)
  
    for n in tree_counts:
        print(f"Training n = {n}")
        temp_clf = RandomForestClassifier(n_estimators=n, random_state=args.seed)
        temp_clf.fit(X_train, y_train)
        preds = temp_clf.predict(X_test)
        accuracies.append(accuracy_score(y_test, preds))

    plt.figure()
    plt.plot(tree_counts, accuracies, marker='o')
    plt.xlabel("Number of Trees")
    plt.ylabel("Accuracy")
    plt.title("Accuracy vs Number of Trees")
    plt.savefig("accuracy_vs_trees.png")
    plt.close()

if __name__ == "__main__":
    main()