# Logistic regression from scratch in plain numpy , batch gradient descent.

import numpy as np


class LogisticRegression:
    # Binary logistic regression via batch gradient descent

    def __init__(self, lr=0.1, epochs=500, ridge=1e-3):
        self.lr = lr
        self.epochs = epochs
        self.ridge = ridge
        self.w = None
        self.b = 0.0

    @staticmethod
    def _sigmoid(z):
        return 1.0 / (1.0 + np.exp(-np.clip(z, -30, 30)))

    def fit(self, X, y):
        n_samples, n_features = X.shape
        self.w = np.zeros(n_features)
        self.b = 0.0
        for _ in range(self.epochs):
            z = X @ self.w + self.b
            p = self._sigmoid(z)
            error = p - y
            grad_w = X.T @ error / n_samples + self.ridge * self.w
            grad_b = error.mean()
            self.w -= self.lr * grad_w
            self.b -= self.lr * grad_b
        return self

    def predict_proba(self, X):
        return self._sigmoid(X @ self.w + self.b)

    def predict(self, X, threshold=0.5):
        return (self.predict_proba(X) >= threshold).astype(int)
