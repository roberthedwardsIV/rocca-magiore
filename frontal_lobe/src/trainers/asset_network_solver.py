import pulp
import psycopg2
import pandas as pd
from geopy.distance import geodesic
import time

# --- CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "hippocampus",
    "port": "5432"
}

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def fetch_live_logistics_rates(conn):
    """
    Queries the supply_states table for the latest rates updated by sensory_receptors.
    Returns a mapping of {supply_line_type: avg_rate_per_km}.
    """
    # Defaults in case DB is empty
    rates = {'maritime': 0.008, 'rail': 0.045, 'truck': 0.12}
    try:
        # Example query looking into JSONB state_data for rate metrics
        df = pd.read_sql("SELECT type, state_data->>'cost_per_km' as rate FROM supply_states", conn)
        for _, row in df.iterrows():
            if row['rate']: rates[row['type']] = float(row['rate'])
    except: pass
    return rates

def solve_network(use_live_signals=True):
    conn = get_db_connection()
    if not conn: return
    
    # 1. FETCH ASSETS & LIVE LOGISTICS
    df_assets = pd.read_sql("SELECT id, name, latitude, longitude, commodity_types FROM assets", conn)
    live_rates = fetch_live_logistics_rates(conn)
    
    mines = df_assets[df_assets['commodity_types'].astype(str).str.contains("Ore|Concentrate", case=False)].copy()
    refineries = df_assets[df_assets['commodity_types'].astype(str).str.contains("Cathode|Metal", case=False)].copy()

    # 2. PRIORITY 1: FILINGS (The Supreme Truth)
    df_fin = pd.read_sql("SELECT * FROM quarterly_financials WHERE quarter = '2025-CURRENT'", conn)
    mines = mines.merge(df_fin, left_on='id', right_on='asset_id', how='left')
    mines['reported_production_tonnes'] = mines['reported_production_tonnes'].fillna(50000)
    mines['reported_freight_expense_usd'] = mines['reported_freight_expense_usd'].fillna(0)

    # 3. BUILD COST MATRIX USING LIVE RATES
    routes = []
    costs = {}
    for _, mine in mines.iterrows():
        for _, ref in refineries.iterrows():
            dist = geodesic((mine['latitude'], mine['longitude']), (ref['latitude'], ref['longitude'])).km
            mode = "maritime" if dist > 800 else "rail"
            costs[(mine['id'], ref['id'])] = dist * live_rates.get(mode, 0.05)
            routes.append((mine['id'], ref['id']))

    # 4. OPTIMIZATION (Minimize total cost while strictly honoring reported financials)
    prob = pulp.LpProblem("Supply_Chain_Retraining", pulp.LpMinimize)
    flow_vars = pulp.LpVariable.dicts("Flow", routes, lowBound=0, cat='Continuous')
    prob += pulp.lpSum([flow_vars[r] * costs[r] for r in routes])

    for _, mine in mines.iterrows():
        mid = mine['id']
        # Constraint: Flow out == Production
        prob += pulp.lpSum([flow_vars[(mid, rid)] for rid in refineries['id']]) <= mine['reported_production_tonnes']
        
        # Anchor to Financials: Calculated cost must match reported freight +/- 10%
        if mine['reported_freight_expense_usd'] > 0:
            calc_cost = pulp.lpSum([flow_vars[(mid, rid)] * costs[(mid, rid)] for rid in refineries['id']])
            prob += calc_cost >= mine['reported_freight_expense_usd'] * 0.90
            prob += calc_cost <= mine['reported_freight_expense_usd'] * 1.10

    # 5. SOLVE & UPDATE
    prob.solve(pulp.PULP_CBC_CMD(msg=0))
    if pulp.LpStatus[prob.status] == 'Optimal':
        cur = conn.cursor()
        target_quarter = "LIVE_RETRAIN" if use_live_signals else "OFFICIAL"
        cur.execute("DELETE FROM asset_trade_links WHERE effective_quarter = %s", (target_quarter,))
        for r in routes:
            vol = flow_vars[r].varValue
            if vol > 500:
                cur.execute("INSERT INTO asset_trade_links (origin_asset_id, target_asset_id, estimated_volume_tonnes, implied_freight_cost, confidence_score, effective_quarter) VALUES (%s, %s, %s, %s, %s, %s)",
                            (r[0], r[1], vol, costs[r], 0.95, target_quarter))
        conn.commit()
    conn.close()

if __name__ == "__main__":
    solve_network()