# Rocco-Maggiore Distributed Intelligence System

## Overview

Rocco-Maggiore is a multi-modal, event-driven quantitative trading and infrastructure monitoring system. It operates on a "Physical-to-Financial" thesis: by modeling the physical resilience of global supply chains in real-time, the system can predict financial market dislocations before they are reflected in asset prices.

The system employs a biological architecture metaphor to ingest raw sensory data (seismic, aviation, maritime, radio, financial filings, and market feeds), process it through machine learning models, and maintain a stateful "consciousness" of industrial assets. It then autonomously generates and executes trading strategies based on the delta between physical reality and market pricing.

## System Architecture

The system is composed of five distinct subsystems, connected via a central message bus (Redis) and a persistent spatial database (PostgreSQL/PostGIS).

### 1. Thalamus (Core State & Valuation Engine)
**Language:** C++17 
**Role:** Central processing, state management, financial valuation, and signal generation.

The Thalamus serves as the "conscious" center of the system. It fuses physical data with market data to generate alpha.
* **Global Registry:** A thread-safe in-memory store of all active physical entities (Mines, Refineries, Supply Lines) and financial instruments (Stocks, Futures, Options).
* **Propagator:** The physics engine. It calculates shockwave propagation from events (e.g., earthquakes) and cascades failure through dependency graphs (e.g., Port congestion $\rightarrow$ Refinery input shortage).
* **Ticker Registry:** Manages live financial data. It links physical assets to financial tickers (e.g., a Copper Mine to `HG` Futures or a Mining Corp Stock).
* **Signal Engine:** The alpha generator. It continuously calculates the "Fair Value" of assets based on their physical health (NPV, Cost of Carry, Black-Scholes). If the Market Price diverges significantly from this Physical Fair Value ($Z\text{-score} > 1.5$), it emits a `StrategyPacket` for execution.
* **Archiver:** Manages memory lifecycle, archiving stale events to the database and snapshotting system state for persistence.

### 2. Sensory Receptors (Data Ingestion)
**Language:** C++17
**Role:** High-performance data acquisition and normalization.

* **Seismic Monitors:** Connects to IRIS SeedLink. Uses STA/LTA algorithms to trigger on seismic spikes near industrial assets.
* **Market Data (IBKR):** Connects to the Interactive Brokers TWS Gateway. Streams live quotes for Stocks, Futures, and Options.
* **Macro Data (FRED):** Polls Federal Reserve economic data (Risk-Free Rates, Corporate Spreads) to update WACC (Weighted Average Cost of Capital) models dynamically.
* **Aviation Ingest:** Polls OpenSky Network. Tracks logistics and corporate executive movements.
* **Maritime Ingest:** Streams AIS data via WebSockets. Tracks vessel movements, drafts, and port calls.
* **Filings Watchdog:** Polls SEC EDGAR RSS feeds. Downloads and parses 10-K/10-Q filings for production numbers and costs.
* **Fire Ingest:** Polls NASA FIRMS data to detect wildfires threatening infrastructure.
* **Radio/Twitter:** Transcribes global radio news (Whisper.cpp) and performs recon on social media to verify physical events.

### 3. Frontal Lobe (Analysis & Inference)
**Language:** Python 3.10 (TensorFlow, Scikit-Learn, Spacy, Pulp) 
**Role:** Pattern recognition, heuristic analysis, and optimization.

* **Financial Parser:** Uses Regex and BeautifulSoup to extract unstructured data (Production Tonnes, Cash Costs, Reserves) from SEC filings to update the fundamental truth of the Thalamus.
* **Asset Network Solver:** Uses Linear Programming (`Pulp`) to solve the "flow" of commodities between mines and refineries, estimating trade volumes based on reported financials and live logistics rates.
* **Maritime Brain:** Analyzes vessel behaviors to detect "Dark Fleet" activity (AIS gaps), loitering, and supply chain links.
* **Aviation Trainer/Brain:** Uses Random Forest classifiers to identify unregistered logistics flights and corporate executive shuttles (e.g., to tax havens).
* **Seismic Brain:** Uses a CNN to classify raw seismic waveforms, distinguishing between actual earthquakes and industrial noise (blasts, trains).

### 4. Brainstem (Execution & Risk)
**Language:** C++17 
**Role:** Order execution, risk management, and trade logging.

The Brainstem acts as the "hand" of the system. It receives `StrategyPackets` from the Thalamus and executes them via IBKR, subject to strict safety checks.

* **Execution Engine:** Manages the connection to the exchange. Handles order placement (Bracket Orders with Stop Loss/Take Profit) and order lifecycle management.
* **Risk Manager (The Gatekeeper):** A strict veto system that validates every trade before execution. It enforces:
    * **Max Daily Drawdown:** Stops trading if losses exceed 2%.
    * **Sector Exposure:** Limits exposure to any single sector (e.g., Metals) to 20%.
    * **Position Sizing:** Calculates trade size based on volatility (ATR) and account equity.
    * **Volatility Checks:** Rejects trades where market volatility disagrees with model forecasts.
* **Trade Logger:** Records all simulated and live executions for backtesting and audit.

### 5. Infrastructure (Memory & Storage)
* **Corpus Callosum (Redis):** The central nervous system.Handles high-throughput pub/sub channels (`raw_signals`, `execution_signals`, `market_ticks`) and streams.
* **Hippocampus (PostgreSQL + PostGIS):** Persistent memory. Stores geospatial registries of world cities, mines, and supply lines, as well as historical financial data and trade logs.

## Technical Specifications

### Physical-to-Financial Valuation
The system prices assets based on "Ground Truth" rather than market sentiment:
1.  **Mines:** Valued via Net Present Value (NPV) models calculated dynamically using live production rates, ore grades, and WACC (driven by live interest rates).
2.  **Futures:** Valued via Cost of Carry models, where the "Storage Cost" is a function of supply chain health (Port/Rail integrity).
3.  **Options:** Valued via Black-Scholes, where the "Fair Volatility" is derived from the physical risk profile of the underlying asset's supply chain.
### Impact Propagation
Physical events ripple through the graph:
* **Event:** Earthquake damages a specific Rail Line.
* **Propagation:** The Rail Line's `flow_capacity` drops.
* **Cascade:** Connected Mines cannot ship ore; their `inventory` spikes and `revenue` projections drop.
* **Signal:** Thalamus recalculates the Mine's NPV, sees it is lower than the Stock Price, and generates a **SELL** signal for the mining company and a **BUY** signal for the commodity (due to scarcity).

## Dependencies

### C++ Components
* **Intel Decimal Math Library (libbid):** Required for high-precision financial calculations.
* **TWS API (10.19.04):** Interactive Brokers connectivity.
* **libpqxx:** PostgreSQL client.
* **hiredis:** Redis client.
* **nlohmann_json:** JSON parsing.
* **libmseed:** Seismic data processing.
* **whisper.cpp:** Speech-to-text.
* **FFmpeg (libav*):** Audio decoding.

### Python Components
* **TensorFlow:** Seismic classification.
* **Scikit-learn:** Logistics classification.
* **Pulp:** Linear programming/Optimization.
* **Spacy:** Named Entity Recognition.
* **BeautifulSoup/Requests:** Web scraping.

## Building and Installation

The system is designed to run in a distributed Docker environment.

### 1. Build the Infrastructure (Redis/Postgres)
Ensure your `docker-compose.yml` is configured with the correct network and volume settings.

### 2. Build the Thalamus (Core)
The Thalamus requires the `twsapi` and `libbid` libraries (handled inside the Dockerfile/CMake).
```bash
docker build -t thalamus_engine -f ./thalamus/Dockerfile .
```

### 3. Build the Sensory Receptors (Ingest)
This image is heavy; it compiles FFmpeg, Whisper.cpp, and TWS API from source.
```bash
docker build -t sensory_receptors -f ./sensory_receptors/Dockerfile .
```

### 4. Build the Frontal Lobe (Analysis)
Includes Python machine learning libraries and NLP models.
```bash
docker build -t frontal_lobe -f ./frontal_lobe/Dockerfile .
```

### 5. Build the Brainstem (Execution)
Strict compilation with Intel Math libraries for financial precision.
```bash
docker build -t brainstem_engine -f ./brainstem/Dockerfile .
```

### 6. Run the System
Start the infrastructure and core logic.
```bash
docker-compose up -d
docker logs -f thalamus
```

### Environment Variables
Ensure the following are set in your `.env` file or `docker-compose.yml`:

* `NASA_FIRMS_KEY`: NASA Fire Data API key.
* `FRED_API_KEY`: St. Louis Fed API key.
* `AIS_API_KEY`: AISStream.io key.
* `DB_USER` / `DB_PASS`: Postgres credentials.
