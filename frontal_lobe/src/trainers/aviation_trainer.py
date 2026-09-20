import psycopg2
import pandas as pd
import numpy as np
import time
import pickle
import os
from utils.db_config import db_config
import sys
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report

# --- CONFIGURATION ---
DB_CONFIG = db_config()

MODEL_DIR = "/app/models"
MODEL_PATH = f"{MODEL_DIR}/aviation_logistics_multiclass_v1.pkl"

if not os.path.exists(MODEL_DIR):
    os.makedirs(MODEL_DIR)

LABEL_MAP = {
    0: "Noise",
    1: "Exploration_Survey_Probable",
    2: "Logistics_MilkRun_Probable",
    3: "Corporate_Exec_Probable"
}

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def fetch_as_dataframe(conn, query):
    try:
        with conn.cursor() as cur:
            cur.execute(query)
            if cur.description:
                columns = [desc[0] for desc in cur.description]
                data = cur.fetchall()
                return pd.DataFrame(data, columns=columns)
            return pd.DataFrame()
    except Exception as e:
        print(f"[DB READ ERR] {e}", flush=True)
        return pd.DataFrame()

def generate_synthetic_noise(count_needed):
    if count_needed <= 0: return pd.DataFrame()
    
    half = int(count_needed / 2)
    data_high = {
        'max_alt': np.random.uniform(28000, 42000, half),
        'max_v_rate': np.random.uniform(0, 5, half),
        'avg_velocity': np.random.uniform(350, 500, half),
        'leg_length_m': np.random.uniform(200000, 800000, half),
        'tortuosity': np.random.uniform(1.0, 1.05, half),
        'label': [0] * half
    }
    data_low = {
        'max_alt': np.random.uniform(2000, 10000, count_needed - half),
        'max_v_rate': np.random.uniform(5, 15, count_needed - half),
        'avg_velocity': np.random.uniform(80, 180, count_needed - half),
        'leg_length_m': np.random.uniform(50000, 150000, count_needed - half),
        'tortuosity': np.random.uniform(1.0, 1.08, count_needed - half),
        'label': [0] * (count_needed - half)
    }
    return pd.concat([pd.DataFrame(data_high), pd.DataFrame(data_low)], ignore_index=True)

def fetch_training_data(conn):
    print("[TRAINER] Building Dataset...", flush=True)

    tortuosity_sql = """
        CASE 
            WHEN ST_Distance(ST_StartPoint(l.trajectory), ST_EndPoint(l.trajectory)) < 0.001 THEN 10.0
            ELSE ST_Length(l.trajectory::geography) / (ST_Distance(ST_StartPoint(l.trajectory)::geography, ST_EndPoint(l.trajectory)::geography) + 1.0)
        END as tortuosity
    """

    # 1. CLASS 1: SURVEY
    sql_survey = f"""
        SELECT l.icao24, l.max_alt, l.max_v_rate, l.avg_velocity, 
               ST_Length(l.trajectory::geography) as leg_length_m, 
               {tortuosity_sql}, 1 as label
        FROM flight_legs l
        JOIN assets a ON ST_DWithin(l.trajectory, a.geom, 0.05) 
        WHERE l.max_alt < 6000 
        AND l.avg_velocity < 150
        AND l.status = 'COMPLETED'
        LIMIT 2000
    """

    # 2. CLASS 2: LOGISTICS
    sql_logistics = f"""
        SELECT l.icao24, l.max_alt, l.max_v_rate, l.avg_velocity, 
               ST_Length(l.trajectory::geography) as leg_length_m, 
               {tortuosity_sql}, 2 as label
        FROM flight_legs l
        JOIN assets a ON (
            ST_DWithin(ST_StartPoint(l.trajectory), a.geom, 1.0) OR 
            ST_DWithin(ST_EndPoint(l.trajectory), a.geom, 1.0)
        )
        WHERE l.max_alt BETWEEN 6000 AND 35000
        AND l.avg_velocity BETWEEN 100 AND 400
        AND l.status = 'COMPLETED'
    """

    # 3. CLASS 3: EXECUTIVE
    sql_corporate = f"""
        SELECT l.icao24, l.max_alt, l.max_v_rate, l.avg_velocity, 
               ST_Length(l.trajectory::geography) as leg_length_m, 
               {tortuosity_sql}, 3 as label
        FROM flight_legs l
        JOIN assets a ON (
            ST_DWithin(ST_StartPoint(l.trajectory), a.geom, 1.0) OR 
            ST_DWithin(ST_EndPoint(l.trajectory), a.geom, 1.0)
        )
        WHERE l.max_alt > 15000 
        AND l.avg_velocity > 300
        AND l.status = 'COMPLETED'
    """

    # 0. CLASS 0: NOISE
    sql_noise = f"""
        SELECT l.icao24, l.max_alt, l.max_v_rate, l.avg_velocity, 
               ST_Length(l.trajectory::geography) as leg_length_m, 
               {tortuosity_sql}, 0 as label
        FROM flight_legs l
        WHERE NOT EXISTS (
            SELECT 1 FROM assets a WHERE ST_DWithin(l.trajectory, a.geom, 0.3)
        )
        AND l.status = 'COMPLETED'
        LIMIT 10000;
    """

    df_1 = fetch_as_dataframe(conn, sql_survey)
    if not df_1.empty: df_1 = df_1[df_1['tortuosity'] > 1.1] 
    df_2 = fetch_as_dataframe(conn, sql_logistics)
    df_3 = fetch_as_dataframe(conn, sql_corporate)
    df_0 = fetch_as_dataframe(conn, sql_noise)

    max_signal = max(len(df_1), len(df_2), len(df_3))
    noise_deficit = max(0, max_signal + 200 - len(df_0)) 
    df_synth_noise = generate_synthetic_noise(noise_deficit)

    print(f"      Survey: {len(df_1)} | Logistics: {len(df_2)} | Corporate: {len(df_3)} | Noise: {len(df_0)}", flush=True)

    return pd.concat([df_0, df_synth_noise, df_1, df_2, df_3], ignore_index=True)

def train_and_save(df):
    if df.empty: return None

    df = df.fillna(0)
    features = ['max_alt', 'max_v_rate', 'avg_velocity', 'leg_length_m', 'tortuosity']
    X = df[features]
    y = df['label']

    try:
        X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)
    except:
        X_train, y_train = X, y
        X_test, y_test = X, y

    clf = RandomForestClassifier(n_estimators=100, class_weight='balanced', random_state=42, n_jobs=-1)
    clf.fit(X_train, y_train)
    
    print(f"[TRAINER] Accuracy: {clf.score(X_test, y_test):.2f}", flush=True)
    
    with open(MODEL_PATH, 'wb') as f:
        pickle.dump(clf, f)
    
    return clf

def resolve_location_name(conn, icao):
    sql = """
        WITH points AS (
            SELECT 
                ST_StartPoint(trajectory)::geometry as start_geom,
                ST_EndPoint(trajectory)::geometry as end_geom
            FROM flight_legs
            WHERE icao24 = %s
        )
        SELECT 
            ST_X(ST_Centroid(ST_Collect(start_geom))) as start_lon,
            ST_Y(ST_Centroid(ST_Collect(start_geom))) as start_lat,
            ST_X(ST_Centroid(ST_Collect(end_geom))) as end_lon,
            ST_Y(ST_Centroid(ST_Collect(end_geom))) as end_lat
        FROM points
    """
    try:
        with conn.cursor() as cur:
            cur.execute(sql, (icao,))
            res = cur.fetchone()
            if not res: return ("Unknown", "Unknown")
            s_lon, s_lat, e_lon, e_lat = res
            
            def lookup(lat, lon):
                if not lat or not lon: return "Unknown"
                cur.execute("SELECT name FROM assets WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 1.0) LIMIT 1", (lon, lat))
                r = cur.fetchone()
                if r: return f"{r[0]} (Regional)"
                cur.execute("SELECT name FROM spatial_ref.world_cities ORDER BY coords <-> ST_SetSRID(ST_MakePoint(%s, %s), 4326) LIMIT 1", (lon, lat))
                r = cur.fetchone()
                if r: return r[0]
                return f"{lat:.2f}, {lon:.2f}"

            return (lookup(s_lat, s_lon), lookup(e_lat, e_lon))
    except Exception as e:
        print(f"[GEO ERR] {e}")
        return ("Unknown", "Unknown")

def hunt_for_suspects(conn, clf):
    print("[TRAINER] Running Inference...", flush=True)
    
    sql_unknowns = """
        SELECT DISTINCT ON (l.icao24) 
            l.icao24, l.max_alt, l.max_v_rate, l.avg_velocity, 
            ST_Length(l.trajectory::geography) as leg_length_m,
            CASE 
                WHEN ST_Distance(ST_StartPoint(l.trajectory), ST_EndPoint(l.trajectory)) < 0.001 THEN 10.0
                ELSE ST_Length(l.trajectory::geography) / (ST_Distance(ST_StartPoint(l.trajectory)::geography, ST_EndPoint(l.trajectory)::geography) + 1.0)
            END as tortuosity,
            r.owner as reg_owner,
            r.registration as reg_tail
        FROM flight_legs l
        LEFT JOIN aircraft_registry r ON l.icao24 = r.icao24
        WHERE l.icao24 NOT IN (SELECT icao_hex FROM aircraft_profiles)
        AND l.status = 'COMPLETED'
        LIMIT 50000
    """
    
    df_unk = fetch_as_dataframe(conn, sql_unknowns)
    if df_unk.empty: return

    X_unk = df_unk[['max_alt', 'max_v_rate', 'avg_velocity', 'leg_length_m', 'tortuosity']].fillna(0)
    
    probs = clf.predict_proba(X_unk)
    max_probs = np.max(probs, axis=1)
    predictions = clf.predict(X_unk)

    df_unk['pred_label'] = predictions
    df_unk['confidence'] = max_probs

    suspects = df_unk[(df_unk['pred_label'] != 0) & (df_unk['confidence'] > 0.90)]

    print(f"[TRAINER] Scanned {len(df_unk)} planes. Found {len(suspects)} suspects.", flush=True)

    if not suspects.empty:
        count = 0
        try:
            for _, row in suspects.iterrows():
                icao = row['icao24']
                cat = LABEL_MAP.get(int(row['pred_label']), "Logistics_Probable")
                start_loc, end_loc = resolve_location_name(conn, icao)
                tail = row['reg_tail'] if row['reg_tail'] else None
                
                # REQ 2: Use "Unknown" if no registry data found
                owner = row['reg_owner'] if row['reg_owner'] else 'Unknown'

                # REQ 1: Comprehensive Airline Blacklist
                COMMERCIAL_KEYWORDS = [
                    'airline', 'airways', 'qantas', 'lufthansa', 'emirates', 'southwest', 
                    'delta', 'united', 'american', 'british', 'air france', 'klm', 
                    'cathay', 'ana', 'japan airlines', 'etihad', 'qatar', 'singapore', 
                    'ryanair', 'easyjet', 'china southern', 'china eastern', 'air china',
                    'latam', 'azul', 'gol', 'avianca', 'aeromexico', 'volaris',
                    'jetblue', 'spirit', 'frontier', 'alaska', 'skywest', 'republic'
                ]
                
                # SANITY CHECK: Force Airlines -> Logistics
                if owner and any(x in owner.lower() for x in COMMERCIAL_KEYWORDS):
                    if cat == "Exploration_Survey_Probable":
                        cat = "Logistics_MilkRun_Probable"

                with conn.cursor() as cur:
                    cur.execute("""
                        INSERT INTO aircraft_profiles (
                            icao_hex, owner_entity, category, is_watchlist, 
                            tail_number, typical_route_start, typical_route_end
                        )
                        VALUES (%s, %s, %s, TRUE, %s, %s, %s)
                        ON CONFLICT (icao_hex) DO UPDATE 
                        SET category = %s, 
                            is_watchlist = TRUE,
                            owner_entity = EXCLUDED.owner_entity,
                            tail_number = EXCLUDED.tail_number,
                            typical_route_start = EXCLUDED.typical_route_start,
                            typical_route_end = EXCLUDED.typical_route_end;
                    """, (icao, owner, cat, tail, start_loc, end_loc, cat))
                    conn.commit()
                    count += 1
            print(f"[TRAINER] Watchlisted {count} aircraft.", flush=True)
        except Exception as e:
            print(f"[DB WRITE ERR] {e}", flush=True)
            conn.rollback()

def prune_database(conn):
    try:
        with conn.cursor() as cur:
            cur.execute("""
                DELETE FROM flight_legs
                WHERE status = 'COMPLETED'
                AND last_seen < NOW() - INTERVAL '7 days'
                AND icao24 NOT IN (SELECT icao_hex FROM aircraft_profiles WHERE is_watchlist = TRUE)
                AND NOT EXISTS (
                    SELECT 1 FROM assets a 
                    WHERE ST_DWithin(flight_legs.trajectory, a.geom, 0.15)
                );
            """)
            conn.commit()
    except Exception as e:
        print(f"[PRUNE ERR] {e}")

def run_cycle():
    while True:
        try:
            conn = get_db_connection()
            df = fetch_training_data(conn)
            model = train_and_save(df)
            if model:
                hunt_for_suspects(conn, model)
            prune_database(conn)
            conn.close()
            print("[TRAINER] Cycle Complete. Sleeping for 24 hours...", flush=True)
            time.sleep(86400)
        except Exception as e:
            print(f"[CRITICAL TRAINER FAILURE] {e}", flush=True)
            time.sleep(60)

if __name__ == "__main__":
    print("[SYSTEM] Aviation Trainer (Final Config).", flush=True)
    time.sleep(2)
    run_cycle()