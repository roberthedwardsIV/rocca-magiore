# ROCCO MAGGIORE

Event-driven system that links **physical infrastructure** (mines, ports, pipelines, seismic/fire risk, logistics) to **financial instruments**, then surfaces dislocations between ground truth and market pricing.

Named subsystems follow a loose brain metaphor: sensors ingest, a thalamus fuses state, a brainstem executes, and a visual cortex renders the live picture.

## What it does

1. Ingest physical and market signals (seismic, wildfire, AIS, aviation, filings, quotes).
2. Maintain a geospatial registry of assets and supply-chain links (PostGIS).
3. Estimate sensitivity of tickers to physical event types (beta / confidence matrix).
4. Propagate shocks through the dependency graph and emit strategy packets when fair value diverges from the market.
5. Optionally route packets through risk checks to Interactive Brokers.
6. Stream the whole loop into a globe UI over Redis pub/sub.

## Architecture

| Service | Role |
|---|---|
| `sensory_receptors` (C++) | High-throughput ingest → Redis |
| `frontal_lobe` (Python) | Classification, LP flow estimates, NLP on filings/news |
| `thalamus` (C++) | State fusion, valuation, signal generation |
| `brainstem` (C++) | Risk gates + IBKR execution |
| `hippocampus` | PostgreSQL / PostGIS |
| `corpus_callosum` | Redis message bus |
| `visual_cortex` | FastAPI + React (Deck.gl) ops UI |

Core Redis channels: `raw_signals`, `system_logs`, `global_sky`, `maritime_ais`, `execution_signals`, `state_vectors`.

## Limitations

The system can surface a real edge: physical events and logistics stress often move commodity-linked names *before* the tape fully prices them. In practice that edge was **not useful for our trading** — we lacked the capital and low-latency infrastructure to get into positions fast enough after a signal. Latency from event → fill ate the opportunity.

**More useful framing today:** a **risk / situational-awareness tool** — monitoring exposure of a book or physical footprint to fires, quakes, port congestion, and supply-chain breaks — rather than a standalone alpha engine.

Paper execution and live IBKR wiring are present but should be treated as experimental.

## Quick start (Visual Cortex demo)

Minimal stack for the UI + Redis feed (no full brain required):

```bash
cp .env.example .env   # fill DB_USER / DB_PASS at minimum
docker compose up -d --build corpus_callosum hippocampus visual_cortex_backend visual_cortex_frontend
```

- UI: http://localhost:3000  
- API / WebSocket: http://localhost:8001 (`/api/*`, `ws://…/ws/stream`)

Publish demo events onto Redis (no host Redis port — run inside the network):

```bash
docker cp visual_cortex/demo_redis_feed.py visual_cortex_backend:/app/demo_redis_feed.py
docker exec visual_cortex_backend python /app/demo_redis_feed.py burst
# or: stream | quake | fire | exec | signal | plane | ship
```

## Full stack

Requires Docker, and for `brainstem` the external `market_net` network used by a separate market-data Timescale stack (optional).

```bash
cp .env.example .env
# fill API keys listed below
docker compose up -d --build
```

### Environment

See `.env.example`. Typical keys:

- `DB_USER` / `DB_PASS` — Postgres
- `NASA_FIRMS_KEY`, `AIS_API_KEY`, `OPENSKY_*`, `FRED_API_KEY` — ingest
- `BSKY_HANDLE` / `BSKY_PASSWORD` — optional social recon
- `HF_TOKEN` — optional model downloads
- `TSDB_*` — brainstem market database
- `TWS_USERID` / `TWS_PASSWORD` — IBKR gateway (keep out of git)

Never commit `.env`.

## Dependencies (high level)

**C++:** libpqxx, hiredis, nlohmann_json, libmseed, TWS API, Intel Decimal Math (brainstem), Whisper/FFmpeg (radio path).

**Python:** TensorFlow, scikit-learn, PuLP, spaCy, BeautifulSoup, asyncpg / FastAPI (visual cortex).

## Safety notes

- Rotate any credentials that ever lived in a committed `.env` or source file before opening the repo.
- Brainstem risk limits (drawdown, sector caps, sizing) are a starting point — verify before live capital.
- This is research software, not investment advice.
