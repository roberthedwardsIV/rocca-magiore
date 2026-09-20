import psycopg2
import time
from datetime import datetime
from utils.db_config import db_config

DB_CONFIG = db_config()

def update_baselines():
    print(f"[{datetime.now()}] [SYSTEM] Recalculating Aviation Baselines (90-Day Window)...", flush=True)
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()

        upsert_query = """
        INSERT INTO baselines_aviation (route_key, avg_altitude, stddev_altitude, avg_velocity, weekly_frequency)
        SELECT 
            'REGIONAL_ASSET_LOGISTICS' as route_key,
            AVG(max_alt), 
            STDDEV(max_alt), 
            AVG(avg_velocity),
            COUNT(*) / 12.8 
        FROM flight_legs 
        WHERE status = 'COMPLETED' 
          AND last_seen > NOW() - INTERVAL '90 days'
          AND max_alt BETWEEN 5000 AND 40000
        ON CONFLICT (route_key) DO UPDATE SET
            avg_altitude = EXCLUDED.avg_altitude,
            stddev_altitude = EXCLUDED.stddev_altitude,
            avg_velocity = EXCLUDED.avg_velocity,
            weekly_frequency = EXCLUDED.weekly_frequency,
            last_updated = NOW();
        """
        cur.execute(upsert_query)
        conn.commit()
        print(f"[{datetime.now()}] [SUCCESS] Baselines updated.", flush=True)
    except Exception as e:
        print(f"[{datetime.now()}] [ERR] Baseline Update Failed: {e}", flush=True)
    finally:
        if 'conn' in locals(): conn.close()

if __name__ == "__main__":
    print("[SYSTEM] Aviation Baseline Manager Online.", flush=True)
    while True:
        update_baselines()
        # Sleep for 24 hours (86,400 seconds)
        print("[SYSTEM] Baseline update complete. Sleeping for 24 hours...", flush=True)
        time.sleep(86400)