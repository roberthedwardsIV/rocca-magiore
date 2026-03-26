from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
import asyncpg
import asyncio
import json
import redis.asyncio as aioredis
import os

app = FastAPI(title="Orbital Synapse API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

DB_HOST = os.getenv("DB_HOST", "hippocampus")
DB_PORT = os.getenv("DB_PORT", "5432")
REDIS_HOST = os.getenv("REDIS_HOST", "corpus_callosum")

async def get_db_connection():
    return await asyncpg.connect(
        user="rocco_admin", password="REMOVED", 
        database="rocco_commodities", host=DB_HOST, port=DB_PORT
    )

import json # Ensure json is imported at the top of your file

@app.get("/api/assets")
async def get_assets():
    try:
        conn = await get_db_connection()
        query = """
            SELECT 
                a.id, 
                a.name, 
                a.type, 
                a.source,
                a.latitude AS lat, 
                a.longitude AS lon, 
                a.commodity_types::text AS commodity_types,
                a.metadata->>'operator' AS operator,
                a.metadata->>'company' AS company,
                a.metadata->>'owner_name_raw' AS owner_name_raw,
                CAST(a.metadata->>'is_private' AS BOOLEAN) AS is_private,
                
                -- The Height Multiplier: Sum of (Absolute Beta * Confidence)
                COALESCE(SUM(ABS(m.beta_coefficient) * m.confidence_score), 0) AS total_sensitivity,
                
                -- The HUD Data: Pack the matrix entries into a JSON array
                COALESCE(
                    json_agg(
                        json_build_object(
                            'ticker', m.ticker,
                            'event_type', m.signal_category,
                            'beta', m.beta_coefficient,
                            'conf', m.confidence_score
                        )
                    ) FILTER (WHERE m.ticker IS NOT NULL), '[]'::json
                )::text AS matrix_entries
                
            FROM assets a
            LEFT JOIN sensitivity_matrix m ON m.entity_id = 'ASSET_' || a.id
            WHERE a.latitude IS NOT NULL AND a.longitude IS NOT NULL 
            GROUP BY a.id
            LIMIT 5000
        """
        rows = await conn.fetch(query)
        await conn.close()
        
        # Parse the JSON string from Postgres into a Python list before sending to React
        assets = []
        for r in rows:
            asset_dict = dict(r)
            if asset_dict.get('matrix_entries'):
                asset_dict['matrix_entries'] = json.loads(asset_dict['matrix_entries'])
            assets.append(asset_dict)
            
        return assets
    except Exception as e:
        print(f"[DB ERROR] Failed to fetch assets: {e}")
        return []

@app.get("/api/links")
async def get_links():
    try:
        conn = await get_db_connection()
        # Added dependency_weight, transport_lag_days, and confidence_score
        query = """
            SELECT 
                l.origin_asset_id, 
                l.target_asset_id,
                l.dependency_weight,
                l.transport_lag_days,
                l.confidence_score,
                o.longitude AS origin_lon, 
                o.latitude AS origin_lat,
                t.longitude AS target_lon, 
                t.latitude AS target_lat
            FROM asset_trade_links l
            JOIN assets o ON l.origin_asset_id = o.id
            JOIN assets t ON l.target_asset_id = t.id
            WHERE l.active = TRUE
        """
        rows = await conn.fetch(query)
        await conn.close()
        return [dict(r) for r in rows]
    except Exception as e:
        print(f"[DB ERROR] Failed to fetch links: {e}")
        return []

@app.get("/api/matrix")
async def get_matrix():
    try:
        conn = await get_db_connection()
        # FIX: Changed 'event_type' to 'signal_category'.
        # Safely strips the 'ASSET_' prefix from entity_id so it can JOIN with the assets table.
        query = """
            WITH RankedMatrix AS (
                SELECT 
                    m.ticker, 
                    m.signal_category AS event_type, 
                    m.beta_coefficient, 
                    m.confidence_score, 
                    a.name as asset_name,
                    ROW_NUMBER() OVER(
                        PARTITION BY LEFT(a.name, 6) 
                        ORDER BY m.confidence_score DESC
                    ) as rn
                FROM sensitivity_matrix m
                JOIN assets a ON CAST(REPLACE(m.entity_id, 'ASSET_', '') AS INTEGER) = a.id
            )
            SELECT 
                ticker, 
                event_type, 
                beta_coefficient, 
                confidence_score, 
                asset_name
            FROM RankedMatrix
            WHERE rn = 1
            ORDER BY confidence_score DESC 
            LIMIT 200;
        """
        rows = await conn.fetch(query)
        await conn.close()
        return [dict(r) for r in rows]
    except Exception as e:
        print(f"[DB ERROR] Failed to fetch matrix: {e}")
        return []

@app.websocket("/ws/stream")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    redis = aioredis.Redis(host=REDIS_HOST, port=6379, decode_responses=True)
    pubsub = redis.pubsub()
    
    # NEW: Added execution_signals and state_vectors for the right sidebar
    await pubsub.subscribe("raw_signals", "system_logs", "global_sky", "maritime_ais", "execution_signals", "state_vectors")
    
    try:
        async for message in pubsub.listen():
            if message["type"] == "message":
                channel = message["channel"]
                try:
                    payload = json.loads(message["data"])
                except json.JSONDecodeError:
                    payload = {"raw_text": message["data"]}
                
                out_msg = {"channel": channel, "payload": payload}
                
                if channel == "system_logs":
                    out_msg["container"] = payload.get("container", "UNKNOWN")
                    out_msg["log"] = payload.get("log", str(payload))
                
                await websocket.send_json(out_msg)
    except WebSocketDisconnect:
        pass
    finally:
        await pubsub.unsubscribe()
        await redis.aclose()