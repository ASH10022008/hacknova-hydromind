# Hydromind

Hydromind is an AI-assisted water safety monitoring system designed for shared sump and intake infrastructure in residential communities. It combines sensor data, visual inspection, and predictive analysis to detect contamination risks before water reaches household pipelines.

## Overview

In gated communities, a single master underground sump often serves multiple homes and apartments. If contaminated water enters this intake, the impact spreads quickly and is often noticed only after it reaches taps and storage tanks. Hydromind addresses this by creating a continuous monitoring layer at the source point, where the system evaluates water condition in near real time.

The platform fuses:
- physical telemetry such as temperature and turbidity,
- optical analysis of captured water imagery,
- machine learning-based risk scoring to classify water safety.

## Problem Statement

Traditional water monitoring approaches are often centralized, delayed, or too broad to respond to real local risks. Water quality can vary significantly across distribution points, tankers, borewells, and underground storage systems. In a shared residential environment, a contaminated intake can affect many households at once.

Hydromind focuses on the exact operational point where risk begins: the master sump or intake chamber. This allows facility managers and residents to act before unsafe water moves deeper into the network.

## Solution

Hydromind uses a low-cost edge-based monitoring setup that captures and evaluates water conditions in real time. The system combines a sensor node with processing logic and a lightweight backend to estimate water safety.

Key capabilities:
- Collects water temperature and turbidity readings
- Captures water visuals for optical interpretation
- Converts image data into analysable color features
- Sends telemetry to a backend for risk evaluation
- Produces a safety score from 0 to 100
- Highlights safe, caution, and unsafe conditions

## System Workflow

1. A sensor-enabled node captures environmental and water data.
2. Camera input is processed locally to extract visual indicators.
3. Sensor and image features are packaged into telemetry payloads.
4. The node publishes the payload over HTTP to a backend service.
5. The backend service validates and interprets the incoming data.
6. A machine learning model estimates the current water safety level.
7. Both the score and the live proof frame are shown on a monitoring dashboard.

## Running the Prototype

The system runs as three cooperating components:

1. **Backend service** — from the repository root:
   ```bash
   pip install -r backend/requirements.txt
   python -m backend.server
   ```
   This exposes the API on port `8000` and also serves the dashboard at `http://<host>:8000/`.

2. **Sensor node (firmware)** — flash `firmware/` with PlatformIO. Before flashing, set `BACKEND_URL` in `firmware/src/main.cpp` to the backend address (for example `http://192.168.1.50:8000`). The node publishes readings every update cycle and serves its camera proof frame on its own address (`http://hydromind.local` over mDNS, or its IP).

3. **Dashboard** — no separate server needed. Open `http://<host>:8000/`. It polls the backend's latest telemetry every few seconds for the score and streamed metrics, and pulls the live camera frame directly from the node.

## Why it Matters

Hydromind is designed to improve public health, reduce resource waste, and strengthen local water infrastructure resilience.

Its impact areas include:
- protecting households from unsafe water exposure,
- reducing unnecessary water loss during contamination events,
- improving transparency in residential utility management,
- supporting faster response in community water systems.

## Technical Approach

Hydromind combines embedded sensing with intelligent analysis:

- Embedded hardware collects physical and visual data from the water source.
- Edge-side processing reduces raw data into meaningful features.
- A backend service handles ingestion, scoring, and response logic.
- A predictive model estimates risk based on combined telemetry.

This hybrid design keeps the system practical, lightweight, and deployable in a prototype environment without requiring high-bandwidth video streaming.

## Project Impact

Hydromind aligns with the broader goals of clean water access, healthier communities, and resilient urban infrastructure. It is especially relevant for societies, residential complexes, and institutional facilities that depend on a shared water intake or tank system.

## Future Scope

The current prototype demonstrates the core concept and operational feasibility. Future development could include:
- industrial-grade sensors for higher accuracy,
- more robust edge processing for continuous monitoring,
- remote connectivity for off-site alerts,
- integration with pump control and water shutoff systems,
- deployment in community-wide utility monitoring networks.

## Architecture

The system architecture and component interactions are documented separately in [architecture.md](./architecture.md).
