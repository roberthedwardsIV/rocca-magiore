import asyncio
import json
import os
import asyncpg
from fastapi import FastAPI, WebSocket
from fastapi.middleware.cors import CORSMiddleware
import redis.asyncio as redis

app = FastAPI()

# Enable CORS for the frontend
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Configuration
REDIS_URL = f"redis://{os.getenv('REDIS_HOST', 'corpus_callosum')}:6379"
DB_DSN = f"postgresql://{os.getenv('DB_USER')}:{os.getenv('DB_PASS')}@{os.getenv('DB_HOST')}:5432/rocco_commodities"

@app.on_event("startup")
async def startup():
    app.state.redis = redis.from_url(REDIS_URL)
    app.state.db = await asyncpg.create_pool(DB_DSN)

@app.on_event("shutdown")
async def shutdown():
    await app.state.redis.close()
    await app.state.db.close()

# --- REST ENDPOINTS (Initial State Hydration) ---

@app.get("/api/assets")
async def get_static_assets():
    """Fetches the 'Static Reality' - Mines, Ports, etc."""
    async with app.state.db.acquire() as conn:
        rows = await conn.fetch("""
            SELECT id, name, type, commodity_types, 
                   ST_X(geom::geometry) as lon, ST_Y(geom::geometry) as lat,
                   op_health, threat_level
            FROM assets
        """)
        return [dict(r) for r in rows]

@app.get("/api/market_snapshot")
async def get_market_snapshot():
    """Fetches the 'Financial Reality' - Active Tickers"""
    async with app.state.db.acquire() as conn:
        rows = await conn.fetch("""
            SELECT symbol, instrument_type, exchange, active 
            FROM ticker_registry WHERE active = TRUE
        """)
        return [dict(r) for r in rows]

# --- WEBSOCKETS (The Firehose) ---

@app.websocket("/ws/stream")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    pubsub = app.state.redis.pubsub()
    
    # Subscribe to the Neural Network
    await pubsub.subscribe(
        "global_sky",       # Aviation Brain output
        "maritime_ais",     # Maritime Ingest
        "raw_signals",      # Thalamus Inputs
        "execution_signals" # Brainstem Outputs
    )

    try:
        while True:
            message = await pubsub.get_message(ignore_subscribe_messages=True)
            if message:
                channel = message['channel'].decode()
                data = message['data'].decode()
                
                # Forward to frontend with channel context
                payload = {
                    "channel": channel,
                    "payload": json.loads(data) if data.startswith('{') else data
                }
                await websocket.send_json(payload)
            await asyncio.sleep(0.01) # Prevent CPU burn
    except Exception as e:
        print(f"WS Error: {e}")
    finally:
        await pubsub.close()