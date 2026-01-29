# Rocco-Maggiore Distributed Intelligence System

## Overview

Rocco-Maggiore is a multi-modal, event-driven infrastructure monitoring system designed to model, predict, and simulate the resilience of global supply chains. The system employs a biological architecture metaphor to ingest raw sensory data (seismic, aviation, radio, and social), process it through machine learning models, and maintain a stateful "consciousness" of industrial assets and supply lines.

The core objective of the system is to detect physical threats (e.g., earthquakes, industrial accidents) and logistical anomalies, calculate their impact on connected infrastructure (mines, refineries, railways, ports), and propagate these effects through a dependency graph to estimate operational and financial health.

## System Architecture

The system is composed of four distinct subsystems, connected via a central message bus (Redis) and a persistent spatial database (PostgreSQL/PostGIS).

### 1. Thalamus (Core State Engine)
**Language:** C++17
**Role:** Central processing, state management, and signal routing.

The Thalamus serves as the supervisor of the system. It maintains the real-time state of all registered assets and supply lines.
* **Dispatcher:** Routes incoming raw signals to the appropriate handlers based on entity type. It manages the creation of new event trackers (e.g., `EarthquakeTracker`).
* **Global Registry:** A thread-safe in-memory store of all active events, assets, and supply lines.
* **Propagator:** The physics engine of the system. It calculates shockwave propagation (logarithmic decay over distance) and dependency cascades (e.g., if a rail line fails, connected mines lose operational health).
* **Archiver:** Manages memory lifecycle, archiving stale events to the database and snapshotting system state.

### 2. Sensory Receptors (Data Ingestion)
**Language:** C++17
**Role:** High-performance data acquisition and normalization.

* **Seismic Monitors:** Connects to IRIS SeedLink servers. Implements STA/LTA (Short Term Average / Long Term Average) algorithms to trigger on seismic spikes and stream waveform data.
* **Aviation Ingest:** Polls the OpenSky Network API. Pushes geolocated aircraft data to the spatial database and message bus.
* **Radio Supervisor:** Manages concurrent audio streams from global news sources (BBC, NPR, Al Jazeera). Utilizes `whisper.cpp` for local, real-time Speech-to-Text transcription.
* **Twitter Recon:** Performs targeted social media searches (via BlueSky/Twitter APIs) based on geolocation triggers to verify physical events.
* **Weather Ingest:** Downloads and parses ECMWF GRIB2 forecast data.

### 3. Frontal Lobe (Analysis & Inference)
**Language:** Python 3.10 (TensorFlow, Scikit-Learn, Spacy)
**Role:** Pattern recognition, heuristic analysis, and machine learning inference.

* **Seismic Brain:** Listens to raw waveform sockets from the Sensory Receptors. Uses a CNN (TensorFlow) to classify signals (Earthquake vs. Explosion vs. Logistics noise) and estimate intensity (MMI).
* **Aviation Brain:** Applies a suite of 10 heuristic gates to flight data, detecting anomalies such as "Dark Arrivals," "Tax Haven Shuttles," and "Capacity Drops."
* **Aviation Trainer:** automated Random Forest classifier that builds aircraft profiles, distinguishing between commercial logistics, corporate executive transport, and exploration surveys.
* **Radio/News Brain:** Uses Spacy (NLP) for Named Entity Recognition (NER) to map transcribed news reports to specific database asset IDs.

### 4. Infrastructure (Memory & Storage)
* **Corpus Callosum (Redis):** Acts as the central IPC (Inter-Process Communication) bus. Handles pub/sub channels for signals and stream buffers for high-volume data.
* **Hippocampus (PostgreSQL + PostGIS):** Persistent storage for asset registries, historical event logs, and geospatial calculations.

## Technical Specifications

### Asset & Supply Line Modeling
The system models infrastructure using a Kalman filter-like approach (`process_noise`, `measurement_noise`) to update state vectors based on incoming signal reliability.
* **Assets:** Mines, Refineries.
* **Supply Lines:** Rail Lines, Rail Yards, Maritime Routes, Ports, Pipelines, Highways, Airports, Airspace, Canals, Locks.

### Propagation Logic
Impact propagation is calculated dynamically:
1.  **Seismic Impact:** `Intensity = 1.0 / log(distance_km)`.
2.  **Cascading Failure:** If an asset's operational health drops below a threshold (0.6), connected supply lines receive "Flow" penalties. Conversely, if a supply line fails, connected assets receive "Financial" penalties.

## Dependencies

### C++ Components
* **CMake (3.10+)**
* **libpqxx** (PostgreSQL client)
* **hiredis** (Redis client)
* **nlohmann_json** (JSON parsing)
* **libmseed** (MiniSEED seismic data)
* **whisper.cpp** (OpenAI Whisper implementation)
* **FFmpeg (libav*)** (Audio processing)
* **Boost** (System libraries)

### Python Components
* **TensorFlow** (2.15.0)
* **Obspy** (Seismology)
* **Spacy** (NLP)
* **Scikit-learn** (Random Forest)
* **Psycopg2** (DB adapter)
* **Redis** (Python client)

## Building and Installation

The system is designed to run in a containerized environment.

### Docker Build
The project is split into three main containers.

1.  **Thalamus (Core):**
    ```bash
    docker build -t thalamus_engine -f ./thalamus/Dockerfile .
    ```

2.  **Sensory Receptors (Ingest):**
    ```bash
    docker build -t sensory_receptors -f ./sensory_receptors/Dockerfile .
    ```

3.  **Frontal Lobe (ML):**
    ```bash
    docker build -t frontal_lobe -f ./frontal_lobe/Dockerfile .
    ```