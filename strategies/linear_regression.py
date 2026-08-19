#Linear regression from scratch in plain numpy, closed-form.

import numpy as np


class LinearRegression:
    # Closed-form ridge regression: w = (X^T X + lambda*I)^-1 X^T y

    def __init__(self, ridge=1e-3):
        self.ridge = ridge
        self.w = None
        self.b = 0.0

    def fit(self, X, y):
        X_mean, y_mean = X.mean(axis=0), y.mean()
        Xc, yc = X - X_mean, y - y_mean
        n_features = X.shape[1]
        A = Xc.T @ Xc + self.ridge * np.eye(n_features)
        self.w = np.linalg.solve(A, Xc.T @ yc)
        self.b = y_mean - X_mean @ self.w
        return self

    def predict(self, X):
        return X @ self.w + self.b
