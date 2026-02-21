import docker
import threading
import asyncio
import json
import os
import asyncpg
from fastapi import FastAPI, WebSocket, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
import redis.asyncio as redis

app = FastAPI()

try:
    docker_client = docker.from_env()
    print("[BACKEND] Docker socket successfully connected.")
except Exception as e:
    print(f"[BACKEND] Docker socket error: {e}")
    docker_client = None

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

REDIS_URL = f"redis://{os.getenv('REDIS_HOST', 'corpus_callosum')}:6379"
DB_DSN = f"postgresql://{os.getenv('DB_USER')}:{os.getenv('DB_PASS')}@{os.getenv('DB_HOST')}:5432/rocco_commodities"

@app.on_event("startup")
async def startup():
    app.state.redis = redis.from_url(REDIS_URL)
    for i in range(5):
        try:
            app.state.db = await asyncpg.create_pool(DB_DSN)
            print(f"[BACKEND] Initialized Hippocampus Connection (Attempt {i+1}).")
            break
        except Exception as e:
            print(f"[BACKEND] Database connection failed: {e}")
            await asyncio.sleep(2)

@app.on_event("shutdown")
async def shutdown():
    if hasattr(app.state, 'redis'):
        await app.state.redis.close()
    if hasattr(app.state, 'db') and app.state.db:
        await app.state.db.close()

@app.get("/api/world_state")
async def get_world_state(
    min_lon: float = Query(-180.0), 
    min_lat: float = Query(-90.0), 
    max_lon: float = Query(180.0), 
    max_lat: float = Query(90.0),
    zoom: float = Query(1.0)
):
    """
    LOD-Enabled World State Fetcher.
    Only returns entities within the viewport.
    Filters complexity based on zoom level.
    """
    if not hasattr(app.state, 'db') or not app.state.db:
        return {"error": "DATABASE_NOT_READY"}

    # --- LOD LOGIC ---
    # Zoom 1-5: Strategic View (Mines, Ports, Pipelines, Mainlines)
    # Zoom 6-10: Tactical View (Refineries, Regional Rail)
    # Zoom 11+: Operational View (Local Roads, Substations)
    
    line_filter = ""
    hub_filter = ""
    
    if zoom < 6:
        # High Level: Only critical infrastructure
        line_filter = "AND type IN ('pipeline', 'shipping_lane', 'rail_mainline', 'highway_trunk')"
        hub_filter = "AND type IN ('maritime_port', 'power_plant')" 
    elif zoom < 11:
        # Mid Level: Add standard rail and distribution
        line_filter = "AND type NOT IN ('power_grid', 'road')" 
    
    # Bounding Box WKT for PostGIS
    bbox_sql = f"ST_MakeEnvelope({min_lon}, {min_lat}, {max_lon}, {max_lat}, 4326)"

    async with app.state.db.acquire() as conn:
        try:
            # 1. ASSETS (Always show Mines/Refineries, but only in view)
            assets = await conn.fetch(f"""
                SELECT a.id, a.name, a.type,
                       COALESCE(a.commodity_types[1], 'unknown') as commodity,
                       ST_X(ST_Centroid(a.geom)) as lon, 
                       ST_Y(ST_Centroid(a.geom)) as lat,
                       COALESCE(s.op_health, 1.0) as op_health
                FROM assets a
                LEFT JOIN asset_states s ON a.id = s.asset_id
                WHERE a.geom && {bbox_sql}
                LIMIT 3000
            """)

            # 2. ROUTES (Apply Type Filter + Spatial Filter)
            lines = await conn.fetch(f"""
                SELECT line_id as id, name, type, 
                       ST_AsGeoJSON(geom)::json as geojson,
                       COALESCE(state_data, '{{}}'::jsonb) as state
                FROM supply_lines
                WHERE geom && {bbox_sql}
                {line_filter}
                LIMIT 2000 -- Hard cap per viewport to prevent browser crash
            """)

            # 3. HUBS
            hubs = await conn.fetch(f"""
                SELECT id, name, type,
                       ST_X(ST_Centroid(geom)) as lon,
                       ST_Y(ST_Centroid(geom)) as lat,
                       capacity_rating
                FROM supply_hubs
                WHERE geom && {bbox_sql}
                {hub_filter}
                LIMIT 1500
            """)
            
            # 4. CHOKEPOINTS
            chokes = await conn.fetch(f"""
                SELECT id, name, type,
                       ST_X(ST_Centroid(geom)) as lon,
                       ST_Y(ST_Centroid(geom)) as lat,
                       structural_health
                FROM supply_chokepoints
                WHERE geom && {bbox_sql}
                LIMIT 1000
            """)

            return {
                "assets": [dict(r) for r in assets],
                "lines": [dict(r) for r in lines],
                "hubs": [dict(r) for r in hubs],
                "chokepoints": [dict(r) for r in chokes]
            }
        except Exception as e:
            print(f"[API ERROR] World State Failed: {e}")
            raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/portfolio_pulse")
async def get_portfolio_pulse():
    if not hasattr(app.state, 'db') or not app.state.db:
        return {"pnl": 0, "tickers": []}

    # 1. Fetch live account state from Redis (populated by the C++ Brainstem)
    account_pnl = 0.0
    account_balance = 0.0
    try:
        if hasattr(app.state, 'redis'):
            raw_state = await app.state.redis.get("account_state")
            if raw_state:
                state_data = json.loads(raw_state)
                account_pnl = state_data.get("pnl", 0.0)
                account_balance = state_data.get("balance", 0.0)
    except Exception as e:
        print(f"[API ERROR] Redis Account State Fetch Failed: {e}")

    # 2. Fetch the physical infrastructure & market data from Postgres
    async with app.state.db.acquire() as conn:
        try:
            tickers = await conn.fetch("""
                SELECT t.symbol, t.instrument_type, 
                       COALESCE(ts.price, 0.0) as price, 
                       COALESCE(ts.volatility, 0.0) as vol,
                       COALESCE(ts.trend_score, 0.0) as trend
                FROM ticker_registry t
                LEFT JOIN (
                    SELECT DISTINCT ON (symbol) symbol, price, volatility, trend_score 
                    FROM ticker_states ORDER BY symbol, time_bucket DESC
                ) ts ON t.symbol = ts.symbol
                WHERE t.active = TRUE
            """)

            asset_health = await conn.fetchrow("""
                SELECT AVG(op_health) as avg_health, 
                       COUNT(*) FILTER (WHERE op_health < 0.5) as critical_count
                FROM asset_states
            """)
            
            health_val = asset_health['avg_health'] if asset_health else None
            safe_avg_health = float(health_val) if health_val is not None else 1.0
            safe_critical = asset_health['critical_count'] if asset_health and asset_health['critical_count'] else 0
            
            return {
                "pnl": account_pnl,                 
                "balance": account_balance,         # <--- NEW: Passing balance down
                "avg_health": safe_avg_health,
                "critical_threats": safe_critical,
                "tickers": [dict(t) for t in tickers]
            }

        except Exception as e:
            print(f"[API ERROR] Portfolio Pulse Failed: {e}")
            return {"pnl": account_pnl, "balance": account_balance, "tickers": [], "error": str(e)}

@app.websocket("/ws/stream")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    pubsub = app.state.redis.pubsub()
    await pubsub.subscribe("global_sky", "maritime_ais", "raw_signals", "execution_signals", "state_vectors")
    
    try:
        while True:
            message = await pubsub.get_message(ignore_subscribe_messages=True)
            if message:
                channel = message['channel'].decode()
                data = message['data'].decode()
                if "keepalive" in data: continue
                try:
                    payload = json.loads(data)
                except json.JSONDecodeError:
                    payload = data  # Fallback for plain text messages
                    
                await websocket.send_json({"channel": channel, "payload": payload})
                
            await asyncio.sleep(0.01)
    except Exception as e:
        print(f"[WS_ERR] WebSocket disconnected: {e}")
    finally:
        await pubsub.close()


@app.websocket("/ws/logs")
async def logs_websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    
    if not docker_client:
        await websocket.send_json({"container": "SYSTEM", "log": "Docker socket not mounted."})
        return

    # The 6 core services we want to monitor
    target_containers = [
        "frontal_lobe", 
        "sensory_receptors", 
        "ibkr_gateway", 
        "thalamus", 
        "hippocampus", 
        "visual_cortex_backend"
    ]

    queue = asyncio.Queue()
    loop = asyncio.get_running_loop()
    stop_event = threading.Event()

    # Worker function that runs in a background thread for each container
    # Worker function that runs in a background thread for each container
    def tail_container_logs(container_name):
        try:
            container = docker_client.containers.get(container_name)
            buffer = "" # Add a string buffer to catch byte chunks
            for chunk in container.logs(stream=True, follow=True, tail=20):
                if stop_event.is_set():
                    break
                if chunk:
                    # Decode and append to buffer
                    buffer += chunk.decode('utf-8', errors='ignore')
                    # Only yield when we have a complete line
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        clean_line = line.strip()
                        if clean_line:
                            asyncio.run_coroutine_threadsafe(
                                queue.put({"container": container_name, "log": clean_line}),
                                loop
                            )
        except docker.errors.NotFound:
            asyncio.run_coroutine_threadsafe(
                queue.put({"container": container_name, "log": f"[WARN] Container {container_name} not found."}),
                loop
            )
        except Exception as e:
            asyncio.run_coroutine_threadsafe(
                queue.put({"container": container_name, "log": f"[ERROR] {str(e)}"}), 
                loop
            )

    # Spawn a listener thread for each container
    for name in target_containers:
        t = threading.Thread(target=tail_container_logs, args=(name,), daemon=True)
        t.start()

    try:
        # Funnel queue data into the WebSocket to the React frontend
        while True:
            msg = await queue.get()
            await websocket.send_json(msg)
    except Exception:
        # Fires when the user closes the diagnostics modal or refreshes
        pass 
    finally:
        stop_event.set() # Signal threads to die