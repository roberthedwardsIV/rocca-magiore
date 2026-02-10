# Rocco-Maggiore Distributed Intelligence System

## Overview

[cite_start]Rocco-Maggiore is a multi-modal, event-driven quantitative trading and infrastructure monitoring system[cite: 1, 142]. [cite_start]It operates on a "Physical-to-Financial" thesis: by modeling the physical resilience of global supply chains in real-time, the system can predict financial market dislocations before they are reflected in asset prices[cite: 200, 206].

[cite_start]The system employs a biological architecture metaphor to ingest raw sensory data (seismic, aviation, maritime, radio, financial filings, and market feeds), process it through machine learning models, and maintain a stateful "consciousness" of industrial assets[cite: 1, 842]. [cite_start]It then autonomously generates and executes trading strategies based on the delta between physical reality and market pricing[cite: 814, 823].

## System Architecture

[cite_start]The system is composed of five distinct subsystems, connected via a central message bus (Redis) and a persistent spatial database (PostgreSQL/PostGIS)[cite: 1, 6].

### 1. Thalamus (Core State & Valuation Engine)
[cite_start]**Language:** C++17 [cite: 13]
[cite_start]**Role:** Central processing, state management, financial valuation, and signal generation[cite: 4, 140].

The Thalamus serves as the "conscious" center of the system. It fuses physical data with market data to generate alpha.
* [cite_start]**Global Registry:** A thread-safe in-memory store of all active physical entities (Mines, Refineries, Supply Lines) and financial instruments (Stocks, Futures, Options)[cite: 90, 93].
* **Propagator:** The physics engine. [cite_start]It calculates shockwave propagation from events (e.g., earthquakes) and cascades failure through dependency graphs (e.g., Port congestion $\rightarrow$ Refinery input shortage)[cite: 181, 206].
* **Ticker Registry:** Manages live financial data. [cite_start]It links physical assets to financial tickers (e.g., a Copper Mine to `HG` Futures or a Mining Corp Stock)[cite: 778, 789].
* **Signal Engine:** The alpha generator. [cite_start]It continuously calculates the "Fair Value" of assets based on their physical health (NPV, Cost of Carry, Black-Scholes)[cite: 792, 804]. [cite_start]If the Market Price diverges significantly from this Physical Fair Value ($Z\text{-score} > 1.5$), it emits a `StrategyPacket` for execution[cite: 814].
* [cite_start]**Archiver:** Manages memory lifecycle, archiving stale events to the database and snapshotting system state for persistence[cite: 210, 214].

### 2. Sensory Receptors (Data Ingestion)
[cite_start]**Language:** C++17 [cite: 843]
[cite_start]**Role:** High-performance data acquisition and normalization.

* **Seismic Monitors:** Connects to IRIS SeedLink. [cite_start]Uses STA/LTA algorithms to trigger on seismic spikes near industrial assets[cite: 902, 920].
* **Market Data (IBKR):** Connects to the Interactive Brokers TWS Gateway. [cite_start]Streams live quotes for Stocks, Futures, and Options[cite: 1239, 1241].
* [cite_start]**Macro Data (FRED):** Polls Federal Reserve economic data (Risk-Free Rates, Corporate Spreads) to update WACC (Weighted Average Cost of Capital) models dynamically[cite: 1197, 1219].
* **Aviation Ingest:** Polls OpenSky Network. [cite_start]Tracks logistics and corporate executive movements[cite: 961, 973].
* **Maritime Ingest:** Streams AIS data via WebSockets. [cite_start]Tracks vessel movements, drafts, and port calls[cite: 994, 1000].
* **Filings Watchdog:** Polls SEC EDGAR RSS feeds. [cite_start]Downloads and parses 10-K/10-Q filings for production numbers and costs[cite: 1113, 1127].
* [cite_start]**Fire Ingest:** Polls NASA FIRMS data to detect wildfires threatening infrastructure[cite: 1153, 1184].
* [cite_start]**Radio/Twitter:** Transcribes global radio news (Whisper.cpp) and performs recon on social media to verify physical events[cite: 1031, 1074].

### 3. Frontal Lobe (Analysis & Inference)
[cite_start]**Language:** Python 3.10 (TensorFlow, Scikit-Learn, Spacy, Pulp) [cite: 1274, 1276]
**Role:** Pattern recognition, heuristic analysis, and optimization.

* [cite_start]**Financial Parser:** Uses Regex and BeautifulSoup to extract unstructured data (Production Tonnes, Cash Costs, Reserves) from SEC filings to update the fundamental truth of the Thalamus[cite: 1372, 1388].
* [cite_start]**Asset Network Solver:** Uses Linear Programming (`Pulp`) to solve the "flow" of commodities between mines and refineries, estimating trade volumes based on reported financials and live logistics rates[cite: 1494, 1499].
* [cite_start]**Maritime Brain:** Analyzes vessel behaviors to detect "Dark Fleet" activity (AIS gaps), loitering, and supply chain links[cite: 1395, 1408].
* [cite_start]**Aviation Trainer/Brain:** Uses Random Forest classifiers to identify unregistered logistics flights and corporate executive shuttles (e.g., to tax havens)[cite: 1457, 1469].
* [cite_start]**Seismic Brain:** Uses a CNN to classify raw seismic waveforms, distinguishing between actual earthquakes and industrial noise (blasts, trains)[cite: 1276, 1283].

### 4. Brainstem (Execution & Risk)
[cite_start]**Language:** C++17 [cite: 1525]
[cite_start]**Role:** Order execution, risk management, and trade logging[cite: 5].

The Brainstem acts as the "hand" of the system. [cite_start]It receives `StrategyPackets` from the Thalamus and executes them via IBKR, subject to strict safety checks[cite: 1559, 1593].
* **Execution Engine:** Manages the connection to the exchange. [cite_start]Handles order placement (Bracket Orders with Stop Loss/Take Profit) and order lifecycle management[cite: 1526, 1582].
* [cite_start]**Risk Manager (The Gatekeeper):** A strict veto system that validates every trade before execution[cite: 1637]. It enforces:
    * [cite_start]**Max Daily Drawdown:** Stops trading if losses exceed 2%[cite: 1650].
    * [cite_start]**Sector Exposure:** Limits exposure to any single sector (e.g., Metals) to 20%[cite: 1642].
    * [cite_start]**Position Sizing:** Calculates trade size based on volatility (ATR) and account equity[cite: 1656].
    * [cite_start]**Volatility Checks:** Rejects trades where market volatility disagrees with model forecasts[cite: 1654].
* [cite_start]**Trade Logger:** Records all simulated and live executions for backtesting and audit[cite: 1621].

### 5. Infrastructure (Memory & Storage)
* **Corpus Callosum (Redis):** The central nervous system. [cite_start]Handles high-throughput pub/sub channels (`raw_signals`, `execution_signals`, `market_ticks`) and streams[cite: 1, 822, 849].
* **Hippocampus (PostgreSQL + PostGIS):** Persistent memory. [cite_start]Stores geospatial registries of world cities, mines, and supply lines, as well as historical financial data and trade logs[cite: 6, 49].

## Technical Specifications

### Physical-to-Financial Valuation
The system prices assets based on "Ground Truth" rather than market sentiment:
1.  [cite_start]**Mines:** Valued via Net Present Value (NPV) models calculated dynamically using live production rates, ore grades, and WACC (driven by live interest rates)[cite: 262, 270].
2.  [cite_start]**Futures:** Valued via Cost of Carry models, where the "Storage Cost" is a function of supply chain health (Port/Rail integrity)[cite: 702, 704].
3.  [cite_start]**Options:** Valued via Black-Scholes, where the "Fair Volatility" is derived from the physical risk profile of the underlying asset's supply chain[cite: 749, 751].

### Impact Propagation
Physical events ripple through the graph:
* [cite_start]**Event:** Earthquake damages a specific Rail Line[cite: 193].
* [cite_start]**Propagation:** The Rail Line's `flow_capacity` drops[cite: 203].
* [cite_start]**Cascade:** Connected Mines cannot ship ore; their `inventory` spikes and `revenue` projections drop[cite: 207].
* [cite_start]**Signal:** Thalamus recalculates the Mine's NPV, sees it is lower than the Stock Price, and generates a **SELL** signal for the mining company and a **BUY** signal for the commodity (due to scarcity)[cite: 814].

## Dependencies

### C++ Components
* [cite_start]**Intel Decimal Math Library (libbid):** Required for high-precision financial calculations.
* [cite_start]**TWS API (10.19.04):** Interactive Brokers connectivity[cite: 830].
* [cite_start]**libpqxx:** PostgreSQL client[cite: 14].
* [cite_start]**hiredis:** Redis client[cite: 14].
* [cite_start]**nlohmann_json:** JSON parsing[cite: 13].
* **libmseed:** Seismic data processing[cite: 844].
* [cite_start]**whisper.cpp:** Speech-to-text[cite: 841].
* [cite_start]**FFmpeg (libav*):** Audio decoding[cite: 843].

### Python Components
* **TensorFlow:** Seismic classification[cite: 1276].
* [cite_start]**Scikit-learn:** Logistics classification[cite: 1457].
* [cite_start]**Pulp:** Linear programming/Optimization[cite: 1494].
* **Spacy:** Named Entity Recognition[cite: 1327].
* [cite_start]**BeautifulSoup/Requests:** Web scraping[cite: 1386].

## Building and Installation

The system is designed to run in a distributed Docker environment.

### 1. Build the Infrastructure (Redis/Postgres)
[cite_start]Ensure your `docker-compose.yml` is configured with the correct network and volume settings.

### 2. Build the Thalamus (Core)
[cite_start]The Thalamus requires the `twsapi` and `libbid` libraries (handled inside the Dockerfile/CMake)[cite: 11, 14].
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
