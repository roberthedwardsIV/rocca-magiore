import pulp
import pandas as pd
import psycopg2 
from psycopg2.extras import execute_batch
import time
import redis
import json
import threading
import math

# --- CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "hippocampus",
    "port": "5432"
}

# Redis connection for pushing ground-truth filings to the C++ Core
r_bus = redis.Redis(host='corpus_callosum', port=6379, db=0)

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
        if not cur.description: return pd.DataFrame()
        columns = [desc[0] for desc in cur.description]
        data = cur.fetchall()
        cur.close()
        return pd.DataFrame(data, columns=columns)
    except Exception as e:
        print(f"[NET SOLVER] Data Fetch Error: {e}", flush=True)
        if conn: conn.rollback()
        return pd.DataFrame()

# --- FAST DISTANCE & REGION LOGIC ---
def haversine_km(lat1, lon1, lat2, lon2):
    R = 6371.0 
    lat1, lon1, lat2, lon2 = map(math.radians, [lat1, lon1, lat2, lon2])
    dlat = lat2 - lat1
    dlon = lon2 - lon1
    a = math.sin(dlat/2)**2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon/2)**2
    c = 2 * math.asin(math.sqrt(a))
    return R * c

def get_region(lat, lon):
    if lat > 15 and lon < -45: 
        return 'North America West' if lon < -100 else 'North America East'
    if lat <= 15 and lon < -30: 
        return 'South America West' if lon < -65 else 'South America East'
    if lat > 35 and -30 <= lon <= 45: return 'Europe'
    if -35 <= lat <= 35 and -20 <= lon <= 50: return 'Africa'
    if lat > -10 and lon > 45: return 'Asia'
    return 'Global_Default'

def fetch_live_logistics_rates(conn):
    rates = {}
    df = fetch_as_dataframe(conn, "SELECT origin_zone, dest_zone, transport_mode, rate_usd FROM freight_rates")
    if not df.empty:
        for _, row in df.iterrows():
            rates[(row['origin_zone'], row['dest_zone'], row['transport_mode'])] = float(row['rate_usd'])
    return rates

def broadcast_sec_filings_to_thalamus(df_merged):
    """
    Acts as the telemetry bridge. Pushes SEC ground-truth parameters to 
    the C++ Core so Assets can update their NPV and EV calculations.
    """
    print(f"   -> [TELEMETRY] Broadcasting {len(df_merged)} updated SEC models to Thalamus Core...", flush=True)
    for _, row in df_merged.iterrows():
        cogs = row.get('cogs_usd')
        capex = row.get('capex_usd')
        prod = row.get('reported_production_tonnes')
        
        unit_cost = None
        if pd.notnull(cogs) and pd.notnull(prod) and prod > 0:
            unit_cost = float(cogs) / float(prod)
            unit_cost = max(500.0, min(unit_cost, 25000.0))

        signal = {
            "entity_type": row['type'],
            "asset_id": row['id'],
            "category": "filing",
            "timestamp": int(time.time() * 1000)
        }
        
        if pd.notnull(prod): signal["production_rate"] = float(prod)
        if pd.notnull(prod): signal["throughput"] = float(prod) 
        if unit_cost: signal["cost_per_unit"] = unit_cost
        if pd.notnull(capex): signal["fixed_costs"] = float(capex)

        r_bus.lpush("raw_signals", json.dumps(signal))

def solve_network(use_live_signals=True):
    print("\n[NET SOLVER] Waking up. Assessing data completeness...", flush=True)
    
    conn = get_raw_connection()
    if not conn: return

    try:
        # 1. FETCH ASSETS
        query_assets = """
            SELECT id, name, type, 
                   ST_Y(ST_Centroid(geom)) as latitude, 
                   ST_X(ST_Centroid(geom)) as longitude
            FROM assets 
            WHERE geom IS NOT NULL AND is_private = FALSE
        """
        df_assets = fetch_as_dataframe(conn, query_assets)
        
        if df_assets.empty:
            print("[NET SOLVER] No public assets found. Skipping cycle.", flush=True)
            return

        coords_map = {row['id']: (row['latitude'], row['longitude']) for _, row in df_assets.iterrows()}
        ports_df = df_assets[df_assets['type'] == 'port'].copy()
        ports_tuples = list(zip(ports_df['id'], ports_df['latitude'], ports_df['longitude']))

        mines = df_assets[df_assets['type'] == 'mine'].copy()
        refineries = df_assets[df_assets['type'].isin(['refinery', 'smelter'])].copy()

        # 2. FETCH MACRO FINANCIALS & PHYSICAL CONSTRAINTS (The New Schema)
        query_fin = """
            SELECT asset_id, reported_production_tonnes, cash_cost_per_unit_usd, cogs_usd, capex_usd
            FROM quarterly_financials 
            WHERE quarter = '2025-CURRENT'
        """
        df_fin = fetch_as_dataframe(conn, query_fin)
        
        total_mines = len(mines)
        if df_fin.empty:
            print("[NET SOLVER] ABORT: 0% financial data parsed. Waiting for Auditor.", flush=True)
            return
            
        mines = mines.merge(df_fin, left_on='id', right_on='asset_id', how='inner')
        coverage = len(mines) / total_mines if total_mines > 0 else 0
        
        if coverage < 0.20:
            print(f"[NET SOLVER] ABORT: Only {coverage:.1%} of mines have SEC data. Waiting for NLP.", flush=True)
            return

        print(f"   -> GATE PASSED: Proceeding with {len(mines)} fully verified Mines.", flush=True)

        broadcast_sec_filings_to_thalamus(mines)

        # 4. BUILD COST MATRIX & CALCULATE IMPLIED MARGINS
        live_rates = fetch_live_logistics_rates(conn)
        routes = []
        costs = {}
        mines_margin_map = {}
        MAX_DIST_KM = 8000.0 
        
        print(f"   -> Calculating spatial distances and unit economics...", flush=True)
        
        # Calculate implied margin per mine for the LP Objective
        for _, m_row in mines.iterrows():
            m_id = m_row['id']
            cogs = m_row.get('cogs_usd')
            prod = m_row.get('reported_production_tonnes')
            
            margin_per_tonne = 5000.0 # Safe default global price assumption
            if pd.notnull(cogs) and pd.notnull(prod) and prod > 0:
                cost_per_tonne = float(cogs) / float(prod)
                margin_per_tonne = max(100.0, margin_per_tonne - cost_per_tonne) # Prevent negative margins
                
            mines_margin_map[m_id] = margin_per_tonne

        # Spatial Routing
        mines_tuples = list(zip(mines['id'], mines['latitude'], mines['longitude']))
        refs_tuples = list(zip(refineries['id'], refineries['latitude'], refineries['longitude']))
        
        for m_id, m_lat, m_lon in mines_tuples:
            m_region = get_region(m_lat, m_lon)
            distances = []
            
            for r_id, r_lat, r_lon in refs_tuples:
                dist = haversine_km(m_lat, m_lon, r_lat, r_lon)
                if dist < MAX_DIST_KM:
                    distances.append((r_id, dist, r_lat, r_lon))
                    
            distances.sort(key=lambda x: x[1])
            top_5 = distances[:5]
            
            for r_id, dist, r_lat, r_lon in top_5:
                mode = "maritime" if dist > 800 else "rail"
                if dist < 100: mode = "truck"
                
                r_region = get_region(r_lat, r_lon)
                if mode == 'maritime':
                    feu_rate = live_rates.get((m_region, r_region, 'maritime'), live_rates.get(('Global_Default', 'Global_Default', 'maritime'), 1946.00))
                    unit_cost = feu_rate / 22.0
                else:
                    m_land = m_region.replace(' East', '').replace(' West', '')
                    r_land = r_region.replace(' East', '').replace(' West', '')
                    ton_km_rate = live_rates.get((m_land, r_land, mode), live_rates.get(('Global_Default', 'Global_Default', mode), 0.05))
                    unit_cost = dist * ton_km_rate
                    
                costs[(m_id, r_id)] = unit_cost
                routes.append((m_id, r_id))
            
        if not routes:
            return

        print(f"   -> Graph Pruned. Solving LP Matrix for {len(routes)} viable edges...", flush=True)

        # 5. OPTIMIZATION (Margin-Weighted Objective)
        # 5. OPTIMIZATION (Margin-Weighted Objective)
        prob = pulp.LpProblem("Supply_Chain_Retraining", pulp.LpMaximize)
        flow_vars = pulp.LpVariable.dicts("Flow", routes, lowBound=0, cat='Continuous')
        
        # NEW OBJECTIVE: Maximize Volume * (SEC Implied Margin - Global Freight Cost)
        prob += pulp.lpSum([flow_vars[r] * (mines_margin_map[r[0]] - costs[r]) for r in routes])

        # THE FIX: Removed the dead freight_expense constraint. Constrain strictly by production volume.
        mines_constraint_tuples = list(zip(mines['id'], mines['reported_production_tonnes']))
        
        for mid, prod_tonnes in mines_constraint_tuples:
            valid_routes = [r for r in routes if r[0] == mid]
            
            if valid_routes and pd.notnull(prod_tonnes) and prod_tonnes > 0:
                prob += pulp.lpSum([flow_vars[r] for r in valid_routes]) <= prod_tonnes

        prob.solve(pulp.PULP_CBC_CMD(msg=0, timeLimit=60))
        
        if pulp.LpStatus[prob.status] == 'Optimal':
            target_quarter = "LIVE" if use_live_signals else "OFFICIAL"
            cur = conn.cursor()
            cur.execute("DELETE FROM asset_trade_links WHERE effective_quarter = %s", (target_quarter,))
            
            insert_batch = []
            
            # Post-Processing Splitter
            for r in routes:
                vol = flow_vars[r].varValue
                if vol and vol > 500:
                    m_id, r_id = r
                    m_lat, m_lon = coords_map[m_id]
                    r_lat, r_lon = coords_map[r_id]
                    total_dist = haversine_km(m_lat, m_lon, r_lat, r_lon)

                    if total_dist > 800 and ports_tuples:
                        origin_port = min(ports_tuples, key=lambda p: haversine_km(m_lat, m_lon, p[1], p[2]))
                        dest_port = min(ports_tuples, key=lambda p: haversine_km(r_lat, r_lon, p[1], p[2]))
                        insert_batch.append((m_id, origin_port[0], vol, costs[r]*0.15, 0.90, target_quarter))
                        insert_batch.append((origin_port[0], dest_port[0], vol, costs[r]*0.70, 0.90, target_quarter))
                        insert_batch.append((dest_port[0], r_id, vol, costs[r]*0.15, 0.90, target_quarter))
                    else:
                        insert_batch.append((m_id, r_id, vol, costs[r], 0.95, target_quarter))
            
            if insert_batch:
                insert_query = """
                    INSERT INTO asset_trade_links 
                    (origin_asset_id, target_asset_id, estimated_volume_tonnes, implied_freight_cost, confidence_score, effective_quarter)
                    VALUES (%s, %s, %s, %s, %s, %s)
                """
                execute_batch(cur, insert_query, insert_batch)
                conn.commit()
                print(f"[NET SOLVER] Solved. {len(insert_batch)} complex route segments mapped based on SEC financials.", flush=True)
            cur.close()
        else:
            print(f"[NET SOLVER] Optimization Failed. Status: {pulp.LpStatus[prob.status]}", flush=True)

    except Exception as e:
        print(f"[NET SOLVER] Execution Error: {e}", flush=True)
        if conn: conn.rollback()
    finally:
        if conn: conn.close()

def run_scheduler():
    while True:
        time.sleep(3600) 
        print("[NET SOLVER] Running scheduled 1-hour baseline solve...", flush=True)
        solve_network(use_live_signals=False)

def listen_for_triggers():
    print("[NET SOLVER] Listening for dynamic Redis triggers...", flush=True)
    pubsub = r_bus.pubsub()
    pubsub.subscribe('raw_signals')
    
    last_solve_time = 0
    DEBOUNCE_SECONDS = 300 

    for message in pubsub.listen():
        if message['type'] == 'message':
            try:
                data = json.loads(message['data'].decode('utf-8'))
                
                category = data.get('category', '')
                severity = data.get('severity', 0.0)
                e_type = data.get('entity_type', '')
                
                trigger = False
                if e_type in ['earthquake', 'wildfire', 'financial_filing']:
                    trigger = True
                elif category in ['threat', 'integrity', 'seismic'] and severity >= 0.5:
                    trigger = True
                    
                if trigger:
                    now = time.time()
                    if now - last_solve_time > DEBOUNCE_SECONDS:
                        print(f"\n[NET SOLVER] ⚠️ DYNAMIC TRIGGER ACTIVATED BY: {e_type.upper()}!", flush=True)
                        solve_network(use_live_signals=True)
                        last_solve_time = time.time()
            except Exception:
                pass 

def run_growth_monitor():
    print("[NET SOLVER] Monitoring database for significant asset growth...", flush=True)
    last_count = 0
    while True:
        time.sleep(60) 
        conn = get_raw_connection()
        if conn:
            try:
                cur = conn.cursor()
                cur.execute("SELECT count(*) FROM assets WHERE geom IS NOT NULL AND is_private = FALSE")
                count = cur.fetchone()[0]
                
                if last_count == 0:
                    last_count = count
                elif count - last_count >= 500: 
                    print(f"\n[NET SOLVER] ⚠️ MASS INGESTION TRIGGER! (+{count - last_count} new assets)", flush=True)
                    solve_network(use_live_signals=False)
                    last_count = count
            except Exception:
                pass
            finally:
                conn.close()

if __name__ == "__main__":
    time.sleep(5) 
    
    growth_thread = threading.Thread(target=run_growth_monitor, daemon=True)
    growth_thread.start()
    solve_network()
    
    scheduler_thread = threading.Thread(target=run_scheduler, daemon=True)
    scheduler_thread.start()
    
    while True:
        try:
            listen_for_triggers()
        except Exception as e:
            print(f"[NET SOLVER] Listener crashed: {e}. Restarting in 10s...", flush=True)
            time.sleep(10)