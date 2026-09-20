import psycopg2
from utils.db_config import db_config

DB_CONFIG = db_config()

def seed_freight_lanes():
    print("[FREIGHT ORACLE] Connecting to Database...")
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()

        # 1. Create a Lane-Based Freight Table
        cur.execute("""
            CREATE TABLE IF NOT EXISTS freight_rates (
                origin_zone VARCHAR(50),
                dest_zone VARCHAR(50),
                transport_mode VARCHAR(50),
                rate_usd NUMERIC,
                rate_unit VARCHAR(50),  -- e.g., 'FEU', 'mile', 'ton-mile'
                last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                PRIMARY KEY (origin_zone, dest_zone, transport_mode)
            );
        """)

        # 2. YOUR EXACT MANUAL INPUTS (Freightos, DAT, STB)
        rates = [
            # --- MARITIME (Freightos FBX Lanes) ---
            ('Asia', 'North America West', 'maritime', 1834.00, 'FEU'),
            ('North America West', 'Asia', 'maritime', 349.00, 'FEU'), 
            ('Asia', 'North America East', 'maritime', 3027.00, 'FEU'),
            ('North America East', 'Asia', 'maritime', 484.00, 'FEU'),
            ('Asia', 'Europe', 'maritime', 2482.00, 'FEU'),
            ('Europe', 'Asia', 'maritime', 461.00, 'FEU'),
            ('Asia', 'Mediterranean', 'maritime', 3707.00, 'FEU'),
            ('Mediterranean', 'Asia', 'maritime', 592.00, 'FEU'),
            ('North America East', 'Europe', 'maritime', 544.00, 'FEU'),
            ('Europe', 'North America East', 'maritime', 1606.00, 'FEU'),
            ('Europe', 'South America East', 'maritime', 999.00, 'FEU'),
            ('Europe', 'South America West', 'maritime', 2331.00, 'FEU'),
            ('Global_Default', 'Global_Default', 'maritime', 1946.00, 'FEU'),

            # --- TRUCKING (DAT / Regional Spot) ---
            ('North America', 'North America', 'truck', 2.71, 'mile'),
            ('Europe', 'Europe', 'truck', 2.80, 'mile'),
            ('South America', 'South America', 'truck', 1.80, 'mile'),
            ('Global_Default', 'Global_Default', 'truck', 2.20, 'mile'),

            # --- RAIL (STB / Bulk) ---
            ('North America', 'North America', 'rail', 0.045, 'ton-mile'),
            ('Global_Default', 'Global_Default', 'rail', 0.050, 'ton-mile')
        ]

        # 3. THE BOOTSTRAPPER (Auto-calculate the missing overland regions)
        # We extract your provided baseline rates to calculate regional discounts
        base_truck = next(r[3] for r in rates if r[0] == 'Europe' and r[2] == 'truck') # 2.80
        base_rail = next(r[3] for r in rates if r[0] == 'North America' and r[2] == 'rail') # 0.045

        bootstrapped_rates = [
            # Missing Trucking (Discounted from EU Baseline)
            ('Asia', 'Asia', 'truck', base_truck * 0.50, 'mile'),
            ('Africa', 'Africa', 'truck', base_truck * 0.70, 'mile'),
            ('Middle East', 'Middle East', 'truck', base_truck * 0.60, 'mile'),
            
            # Missing Rail (Discounted/Marked-up from US Baseline)
            ('Europe', 'Europe', 'rail', base_rail * 1.20, 'ton-mile'),
            ('South America', 'South America', 'rail', base_rail * 0.80, 'ton-mile'),
            ('Asia', 'Asia', 'rail', base_rail * 0.70, 'ton-mile'),
            ('Africa', 'Africa', 'rail', base_rail * 0.90, 'ton-mile')
        ]

        # Combine your inputs with the auto-generated ones
        all_rates = rates + bootstrapped_rates

        # 4. UPSERT TO DATABASE
        for orig, dest, mode, rate, unit in all_rates:
            cur.execute("""
                INSERT INTO freight_rates (origin_zone, dest_zone, transport_mode, rate_usd, rate_unit, last_updated)
                VALUES (%s, %s, %s, %s, %s, CURRENT_TIMESTAMP)
                ON CONFLICT (origin_zone, dest_zone, transport_mode) DO UPDATE 
                SET rate_usd = EXCLUDED.rate_usd,
                    rate_unit = EXCLUDED.rate_unit,
                    last_updated = CURRENT_TIMESTAMP;
            """, (orig, dest, mode, round(rate, 4), unit))

        conn.commit()
        cur.close()
        print(f"[FREIGHT ORACLE] SUCCESS: {len(all_rates)} realistic Trade Lane rates have been seeded.")

    except Exception as e:
        print(f"[FREIGHT ORACLE] ERROR: {e}")
    finally:
        if conn:
            conn.close()

if __name__ == "__main__":
    seed_freight_lanes()