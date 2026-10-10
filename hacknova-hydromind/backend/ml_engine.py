import numpy as np
from sklearn.ensemble import RandomForestRegressor
import joblib
import os

class WaterQualityMLEngine:
    def __init__(self, model_path=None):
        if model_path is None:
            base_dir = os.path.dirname(os.path.abspath(__file__))
            model_path = os.path.join(base_dir, "model.pkl")
        self.model_path = model_path
        self.model = self._load_or_train_model()

    def _load_or_train_model(self):
        """
        Loads pre-trained model from disk or trains a baseline RandomForestRegressor
        on synthetic multi-modal water telemetry data if no saved model exists.
        """
        if os.path.exists(self.model_path):
            try:
                return joblib.load(self.model_path)
            except Exception:
                pass  # Fallthrough to retrain if model file is corrupt

        # --- Synthetic Training Dataset for Prototype ---
        # Features: [temperature_c, turbidity_raw, hue_angle]
        # Expanded with more diverse samples to reduce overfitting
        X_train = np.array([
            [20.0, 800, 200],   # Cold, clean supply
            [22.0, 900, 195],   # Safe cold water
            [24.0, 1200, 190],  # Clean water (Blue)
            [25.0, 1300, 185],  # Optimal
            [26.0, 1100, 205],  # Safe warm water
            [26.5, 1400, 210],  # Clean/Normal
            [28.0, 1800, 70],   # Algae / Greenish
            [29.0, 1500, 80],   # Algae bloom
            [28.0, 2400, 40],   # Muddy / Turbid (Yellow)
            [30.0, 2700, 35],   # High turbidity
            [31.5, 2900, 30],   # High risk mud
            [32.0, 3500, 25],   # Highly contaminated
            [22.5, 1100, 200],  # Safe cold supply
            [25.0, 1300, 185],  # Optimal
            [27.0, 1600, 60],   # Slight algae
        ])

        # Target: WQI Score (0 to 100)
        y_train = np.array([96.0, 95.0, 92.0, 94.0, 94.0, 90.0, 48.0, 45.0, 55.0, 40.0, 30.0, 15.0, 96.0, 94.0, 50.0])

        # Initialize and train RandomForestRegressor with more trees
        rf_regressor = RandomForestRegressor(n_estimators=100, random_state=42)
        rf_regressor.fit(X_train, y_train)

        # Save model for future runs
        try:
            joblib.dump(rf_regressor, self.model_path)
        except Exception:
            pass

        return rf_regressor

    def predict_wqi(self, temperature: float, turbidity: int, hue: int) -> tuple[float, str]:
        """
        Predicts the WQI score and classifies the risk tier using multi-modal inputs.
        """
        # Construct feature vector
        features = np.array([[temperature, float(turbidity), float(hue)]])
        
        # Predict score via RandomForest model
        raw_pred = self.model.predict(features)[0]
        score = float(np.clip(raw_pred, 0.0, 100.0))
        
        # Determine operational risk tier
        if score >= 80.0:
            tier = "Safe / Normal"
        elif score >= 50.0:
            tier = "Moderate / Caution"
        else:
            tier = "Hazardous / Action Required"
            
        return round(score, 1), tier

# Instantiate global inference helper for FastAPI integration
ml_engine = WaterQualityMLEngine()

if __name__ == "__main__":
    # Test evaluation
    sample_score, sample_tier = ml_engine.predict_wqi(25.4, 1350, 195)
    print(f"Test Evaluation -> WQI Score: {sample_score} | Tier: {sample_tier}")