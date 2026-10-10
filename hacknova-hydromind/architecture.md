# Hydromind Architecture

This document describes the system architecture for the Hydromind water safety monitoring platform. It focuses on how the sensing layer, processing layer, and monitoring interface work together to detect contamination risk at a shared water intake point.

## 1. Project File Structure

```text
hydromind/
├── firmware/
│   ├── src/
│   │   └── main.cpp
│   └── platformio.ini
├── backend/
│   ├── __init__.py
│   ├── server.py
│   ├── ml_engine.py
│   ├── requirements.txt
│   └── model.pkl            # auto-trained on first run if missing
├── dashboard/
│   └── index.html
├── readme.md
├── architecture.md
└── .gitignore
```


## 2. System Overview

Hydromind is designed for a residential or gated-community water intake where multiple households depend on a single sump, tank, or source point. The system continuously monitors the water condition and generates a safety score that can trigger intervention before unsafe water spreads.

The architecture follows a simple layered pattern:

- Sensor and acquisition layer
- Edge processing layer
- Backend analytics layer
- Monitoring and visualization layer

## 3. High-Level Architecture


+----------------------------------------------------------+
|                    Water Source / Sump                    |
|  Master intake point for residential water distribution  |
+---------------------------+------------------------------+
                            |
                            v
+----------------------------------------------------------+
|                 Sensor Node / Embedded Device             |
| - Temperature sensor (DS18B20)                           |
| - Turbidity sensor                                        |
| - Camera / optical capture                                |
| - Local feature extraction                                |
| - Telemetry packaging                                      |
+---------------------------+------------------------------+
                            |
                            v
+----------------------------------------------------------+
|                    Local / Edge Processing                 |
| - Image color analysis                                     |
| - RGB to hue / optical feature conversion                 |
| - Data normalization                                      |
| - Lightweight structured payload generation               |
+---------------------------+------------------------------+
                            |
                            v
+----------------------------------------------------------+
|                     Backend Service                        |
| - FastAPI ingestion endpoint (/api/v1/telemetry)          |
| - Input validation                                         |
| - ML inference using trained safety model                  |
| - Latest-state cache (/api/v1/telemetry/latest)            |
| - Serves the monitoring dashboard as static files          |
+---------------------------+------------------------------+
                            |
                            v
+----------------------------------------------------------+
|                 Monitoring Dashboard / Interface           |
| - Served by the backend at http://<host>:8000/            |
| - Polls latest telemetry for the safety score and metrics |
| - Pulls the live proof frame directly from the node       |
| - Operational feedback / alerts                           |
+----------------------------------------------------------+

## 4. Component Responsibilities

### 4.1 Sensor Acquisition Layer
This layer is responsible for collecting real-world water indicators from the intake site.

Main data sources include:
- water temperature,
- suspended particle density or turbidity,
- visual imagery of the water flow or sample area.

The sensor node captures the inputs required for determining whether the source water is safe for distribution.

### 4.2 Edge Processing Layer
The edge layer reduces raw measurements into meaningful features before transmission.

Typical tasks include:
- filtering noise and normalizing data,
- extracting color and illumination signals from an image,
- transforming raw sensor values into structured telemetry,
- minimizing bandwidth by sending compact machine-readable data rather than large video streams.

This layer is important because it keeps the system lightweight and suitable for deployment in practical field conditions.

### 4.3 Backend Analytics Layer
The backend acts as the decision support component of the system.

It receives telemetry plus extracted optical features and then:
- validates the inputs,
- combines multiple signals,
- runs a safety evaluation model,
- returns a score such as a 0–100 water safety metric.

This is where the predictive assessment happens and where the system can categorize the water as safe, caution, or unsafe.

### 4.4 Monitoring Interface
The user-facing layer presents system results clearly and quickly.

It can show:
- real-time water status,
- safety trends over time,
- image or visual proof frames,
- alerts for abnormal readings.

This makes the system usable for operators, facility managers, and communities that need immediate situational awareness.

### 4.5 Wiring and Configuration

The three layers are connected through a small amount of configuration:

- The firmware publishes to the backend through the `BACKEND_URL` constant in `firmware/src/main.cpp`.
- The dashboard reads the API from the same origin (it is served by the backend), and talks to the node directly for the live camera frame through `CONFIG.ESP32_HOST` in `dashboard/index.html`.
- No cross-origin setup is required when every component is on the same LAN.

## 5. Data Flow

The data flow follows a single direction from collection to decision support:

1. The embedded system reads physical and visual inputs.
2. Edge logic converts those inputs into structured telemetry (temperature, turbidity, hue, colour label).
3. The node publishes the telemetry over HTTP to the backend ingestion endpoint `/api/v1/telemetry`.
4. Backend processing combines the readings, runs the prediction model, and caches the latest state.
5. The dashboard — served by the backend — polls `/api/v1/telemetry/latest` every few seconds for the score and metrics.
6. The dashboard pulls the live camera proof frame directly from the node's `/capture` endpoint.
7. Operators can decide whether further action is needed.

## 6. Design Principles

Hydromind was designed around a few key principles:

- Local relevance: monitor the exact entry point that affects the community
- Low bandwidth: avoid heavy video streaming and use compact telemetry
- Multimodal insight: combine sensor and vision data for more robust detection
- Fast response: detect risk before water enters household systems
- Practical deployment: use accessible hardware and simple system flow

## 7. Scalability Considerations

The current version demonstrates a viable prototype architecture. For broader deployment, the system can be expanded by:

- adding more sensor nodes across multiple intake points,
- connecting multiple communities to a shared monitoring network,
- adding remote alerting and automation,
- upgrading to industrial-grade hardware and more robust edge intelligence.

## 8. Summary

Hydromind uses a layered architecture that separates acquisition, local processing, predictive analytics, and user visualization. This allows it to monitor water safety at a shared source point in a practical, efficient, and scalable way while keeping the system responsive to contamination events.
