from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from pathlib import Path

from .ml_engine import ml_engine

app = FastAPI(
    title="Hydromind Backend Server",
    description="FastAPI ingestion pipeline for multi-modal water quality telemetry.",
    version="1.0.0",
)

# Enable CORS for local testing and dashboard communication
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# Pydantic model validating incoming telemetry payload from ESP32-CAM
class TelemetryPayload(BaseModel):
    device_id: str = Field(..., description="Unique identifier of the reporting node")
    temperature_c: float = Field(..., description="Water temperature in Celsius")
    turbidity_raw: int = Field(..., description="Raw turbidity reading (0-4095)")
    hue_angle: int = Field(..., description="Dominant optical hue angle (0-360)")
    color_desc: str = Field(default="Detecting...", description="Human readable colour classification")


# In-memory storage for the latest received telemetry (acts as real-time state for UI/dashboards)
latest_telemetry_cache = {
    "device_id": "hydromind_node_01",
    "temperature_c": 24.5,
    "turbidity_raw": 1420,
    "hue_angle": 190,
    "color_desc": "Blue / Clean",
    "wqi_score": 92.4,
    "risk_tier": "Safe / Normal",
}


@app.get("/api/v1/health")
async def root():
    """Basic health/liveness probe."""
    return {"status": "online", "service": "hydromind-backend", "version": app.version}


@app.post("/api/v1/telemetry")
async def receive_telemetry(payload: TelemetryPayload):
    """
    Endpoint where an edge node pushes its JSON sensor and optical data.
    The multi-modal WQI model evaluates the reading and the result is cached.
    """
    try:
        wqi, tier = ml_engine.predict_wqi(
            payload.temperature_c,
            payload.turbidity_raw,
            payload.hue_angle,
        )

        latest_telemetry_cache.update(
            {
                "device_id": payload.device_id,
                "temperature_c": payload.temperature_c,
                "turbidity_raw": payload.turbidity_raw,
                "hue_angle": payload.hue_angle,
                "color_desc": payload.color_desc,
                "wqi_score": wqi,
                "risk_tier": tier,
            }
        )

        return {
            "status": "success",
            "message": "Telemetry received and evaluated successfully",
            "computed_wqi": wqi,
            "risk_tier": tier,
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@app.get("/api/v1/telemetry/latest")
async def get_latest_telemetry():
    """Endpoint for frontend dashboards to pull the latest water health state."""
    return latest_telemetry_cache


# Serve the monitoring dashboard as static files so opening the backend root
# (http://<host>:8000/) shows the UI. API routes above take precedence because
# they are registered before this catch-all mount.
DASHBOARD_DIR = Path(__file__).resolve().parent.parent / "dashboard"
if DASHBOARD_DIR.is_dir():
    app.mount("/", StaticFiles(directory=str(DASHBOARD_DIR), html=True), name="dashboard")


if __name__ == "__main__":
    import uvicorn

    # Run from the repository root:  python -m backend.server
    uvicorn.run(app, host="0.0.0.0", port=8000)
