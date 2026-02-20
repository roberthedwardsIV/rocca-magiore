import pulp
import pandas as pd
import psycopg2 
from psycopg2.extras import execute_batch
from geopy.distance import geodesic
import time
import os
import redis
import json
import threading

# --- CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "hippocampus",
    "port": "5432"
}

def get_raw_connection():
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        return conn
    except Exception as e:
        print(f"[NET SOLVER] Connection Failed: {e}", flush=True)
        return None

def fetch_as_dataframe(conn, query, params=None):
    try:
        cur = conn.cursor()
        cur.execute(query, params)
        columns = [desc[0] for desc in cur.description]
        data = cur.fetchall()
        cur.close()
        return pd.DataFrame(data, columns=columns)
    except Exception as e:
        print(f"[NET SOLVER] Data Fetch Error: {e}", flush=True)
        return pd.DataFrame()

def fetch_live_logistics_rates(conn):
    rates = {'maritime': 0.008, 'rail': 0.045, 'truck': 0.12}
    df = fetch_as_dataframe(conn, "SELECT type, state_data->>'cost_per_km' as rate FROM supply_states")
    if not df.empty:
        for _, row in df.iterrows():
            if row['rate'] and float(row['rate']) > 0:
                rates[row['type']] = float(row['rate'])
    return rates

def solve_network(use_live_signals=True):
    print("[NET SOLVER] Building Asset Graph (Manual Mode)...", flush=True)
    
    conn = get_raw_connection()
    if not conn: return

    try:
        # 1. FETCH ASSETS (FIXED: Use PostGIS geometry, ignoring NULL lat/lon cols)
        query_assets = """
            SELECT id, name, 
                   ST_Y(ST_Centroid(geom)) as latitude, 
                   ST_X(ST_Centroid(geom)) as longitude,
                   commodity_types::text as commodity_types 
            FROM assets 
            WHERE geom IS NOT NULL
        """
        df_assets = fetch_as_dataframe(conn, query_assets)
        
        if df_assets.empty:
            print("[NET SOLVER] No assets found with valid geometry. Skipping cycle.", flush=True)
            return

        live_rates = fetch_live_logistics_rates(conn)

        mines = df_assets[df_assets['commodity_types'].str.contains("Ore|Concentrate|Mine", case=False, na=False)].copy()
        refineries = df_assets[df_assets['commodity_types'].str.contains("Cathode|Metal|Refinery|Smelter", case=False, na=False)].copy()

        print(f"   -> Nodes identified: {len(mines)} Mines, {len(refineries)} Refineries", flush=True)

        # 2. PRIORITY 1: FILINGS
        query_fin = "SELECT * FROM quarterly_financials WHERE quarter = '2025-CURRENT'"
        df_fin = fetch_as_dataframe(conn, query_fin)
        
        if not df_fin.empty:
            mines = mines.merge(df_fin, left_on='id', right_on='asset_id', how='left')
            mines['reported_production_tonnes'] = mines['reported_production_tonnes'].fillna(50000)
            mines['reported_freight_expense_usd'] = mines['reported_freight_expense_usd'].fillna(0)
        else:
            mines['reported_production_tonnes'] = 50000
            mines['reported_freight_expense_usd'] = 0

        # 3. BUILD COST MATRIX
        routes = []
        costs = {}
        
        for _, mine in mines.iterrows():
            for _, ref in refineries.iterrows():
                try:
                    dist = geodesic((mine['latitude'], mine['longitude']), (ref['latitude'], ref['longitude'])).km
                    mode = "maritime" if dist > 800 else "rail"
                    if dist < 100: mode = "truck"
                    unit_cost = dist * live_rates.get(mode, 0.05)
                    costs[(mine['id'], ref['id'])] = unit_cost
                    routes.append((mine['id'], ref['id']))
                except: continue

        if not routes:
            print("[NET SOLVER] No viable routes found.", flush=True)
            return

        # 4. OPTIMIZATION
        prob = pulp.LpProblem("Supply_Chain_Retraining", pulp.LpMinimize)
        flow_vars = pulp.LpVariable.dicts("Flow", routes, lowBound=0, cat='Continuous')
        prob += pulp.lpSum([flow_vars[r] * costs[r] for r in routes])

        for _, mine in mines.iterrows():
            mid = mine['id']
            prob += pulp.lpSum([flow_vars[(mid, rid)] for rid in refineries['id']]) <= mine['reported_production_tonnes']
            if mine['reported_freight_expense_usd'] > 0:
                calc_cost = pulp.lpSum([flow_vars[(mid, rid)] * costs[(mid, rid)] for rid in refineries['id']])
                prob += calc_cost >= mine['reported_freight_expense_usd'] * 0.90
                prob += calc_cost <= mine['reported_freight_expense_usd'] * 1.10

        # 5. SOLVE & UPDATE
        prob.solve(pulp.PULP_CBC_CMD(msg=0))
        
        if pulp.LpStatus[prob.status] == 'Optimal':
            target_quarter = "LIVE_RETRAIN" if use_live_signals else "OFFICIAL"
            cur = conn.cursor()
            cur.execute("DELETE FROM asset_trade_links WHERE effective_quarter = %s", (target_quarter,))
            
            insert_batch = []
            for r in routes:
                vol = flow_vars[r].varValue
                if vol and vol > 500:
                    insert_batch.append((int(r[0]), int(r[1]), float(vol), float(costs[r]), 0.95, target_quarter))
            
            if insert_batch:
                insert_query = """
                    INSERT INTO asset_trade_links 
                    (origin_asset_id, target_asset_id, estimated_volume_tonnes, implied_freight_cost, confidence_score, effective_quarter)
                    VALUES (%s, %s, %s, %s, %s, %s)
                """
                execute_batch(cur, insert_query, insert_batch)
                conn.commit()
                print(f"[NET SOLVER] Solved. {len(insert_batch)} trade links mapped.", flush=True)
            else:
                print("[NET SOLVER] Solved, but no significant flows found.", flush=True)
            cur.close()
        else:
            print("[NET SOLVER] Optimization Failed (Infeasible).", flush=True)

    except Exception as e:
        print(f"[NET SOLVER] Execution Error: {e}", flush=True)
        if conn: conn.rollback()
    finally:
        if conn: conn.close()

def run_scheduler():
    """Fallback 6-hour loop to ensure baseline stays fresh"""
    while True:
        time.sleep(21600)
        print("[NET SOLVER] Running scheduled 6-hour baseline solve...", flush=True)
        solve_network(use_live_signals=False)

def listen_for_triggers():
    """Listens to Redis for dynamic threat events to instantly re-solve the supply chain"""
    print("[NET SOLVER] Listening for dynamic Redis triggers...", flush=True)
    r = redis.Redis(host='corpus_callosum', port=6379, db=0)
    pubsub = r.pubsub()
    pubsub.subscribe('raw_signals')
    
    last_solve_time = 0
    DEBOUNCE_SECONDS = 300 # 5-minute cooldown to prevent spam-solving during an event

    for message in pubsub.listen():
        if message['type'] == 'message':
            try:
                data = json.loads(message['data'].decode('utf-8'))
                
                category = data.get('category', '')
                severity = data.get('severity', 0.0)
                e_type = data.get('entity_type', '')
                
                # TRIGGER GATES: What justifies a massive LP recalculation?
                trigger = False
                if e_type in ['earthquake', 'wildfire']:
                    trigger = True
                elif category in ['threat', 'integrity', 'seismic'] and severity >= 0.5:
                    trigger = True
                    
                if trigger:
                    now = time.time()
                    if now - last_solve_time > DEBOUNCE_SECONDS:
                        print(f"\n[NET SOLVER] ⚠️ DYNAMIC TRIGGER ACTIVATED BY: {e_type.upper()}!", flush=True)
                        solve_network(use_live_signals=True)
                        last_solve_time = time.time()
                    else:
                        pass # Event debounced (solver already ran recently)

            except Exception:
                pass # Ignore malformed signals

if __name__ == "__main__":
    time.sleep(5) # Let DB boot
    
    # 1. Initial baseline solve on startup
    solve_network()
    
    # 2. Start the slow 6-hour background scheduler
    scheduler_thread = threading.Thread(target=run_scheduler, daemon=True)
    scheduler_thread.start()
    
    # 3. Hijack the main thread for the high-speed Redis listener
    while True:
        try:
            listen_for_triggers()
        except Exception as e:
            print(f"[NET SOLVER] Listener crashed: {e}. Restarting in 10s...", flush=True)
            time.sleep(10)