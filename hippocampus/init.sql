-- --------------------------------------------------------------------------------------
-- ROCCO COMMODITIES - MASTER SCHEMA (v6.0 - FULL PRODUCTION)
-- --------------------------------------------------------------------------------------
-- Target: Postgres 15+ with PostGIS
-- Purpose: The comprehensive reality layer for Thalamus, Frontal Lobe, and Brainstem.
-- --------------------------------------------------------------------------------------

-- [LAYER 0] CONFIGURATION & EXTENSIONS
-- --------------------------------------------------------------------------------------
CREATE EXTENSION IF NOT EXISTS postgis;
CREATE SCHEMA IF NOT EXISTS spatial_ref;

-- DEBUG: If this table doesn't exist after boot, the "Nuke" failed.
CREATE TABLE IF NOT EXISTS _schema_version_check (
    version TEXT PRIMARY KEY,
    applied_at TIMESTAMP DEFAULT NOW()
);
INSERT INTO _schema_version_check (version) VALUES ('v6.0_production') ON CONFLICT DO NOTHING;

-- [LAYER 1] GEOMETRY AUTOMATION (Triggers)
-- --------------------------------------------------------------------------------------
-- Automates geometry creation to prevent NULL coordinates in Visual Cortex.

CREATE OR REPLACE FUNCTION auto_geom_point() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.latitude IS NOT NULL AND NEW.longitude IS NOT NULL THEN
        NEW.geom = ST_SetSRID(ST_MakePoint(NEW.longitude, NEW.latitude), 4326);
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION auto_geom_log() RETURNS TRIGGER AS $$
BEGIN
    IF NEW.lat IS NOT NULL AND NEW.lon IS NOT NULL THEN
        NEW.geom = ST_SetSRID(ST_MakePoint(NEW.lon, NEW.lat), 4326);
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- [LAYER 2] PHYSICAL ASSETS (The "Hard" Reality)
-- --------------------------------------------------------------------------------------

-- 2.1 CORE ASSETS
CREATE TABLE IF NOT EXISTS assets (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    type TEXT NOT NULL,             -- 'mine', 'refinery', 'smelter', 'port'
    source TEXT DEFAULT 'OSM',      -- 'OSM_ULTRA', 'SEC_FILING', 'MANUAL'
    
    -- Spatial Data
    latitude FLOAT,                 
    longitude FLOAT,                
    geom GEOMETRY(Geometry, 4326),  -- Flexible for Points or Polygons
    sensitivity_radius_km FLOAT DEFAULT 50.0,
    
    -- Operational State
    op_health FLOAT DEFAULT 1.0,    -- 0.0 (Destroyed) -> 1.0 (Perfect)
    commodity_types TEXT[],         -- e.g. {'Copper', 'Gold'}
    
    -- Metadata
    metadata JSONB DEFAULT '{}'::jsonb,
    last_update BIGINT DEFAULT 0    -- Unix Timestamp
);
CREATE INDEX IF NOT EXISTS idx_assets_geom ON assets USING GIST(geom);
CREATE INDEX IF NOT EXISTS idx_assets_type ON assets(type);
CREATE INDEX IF NOT EXISTS idx_assets_name ON assets(name);

-- Trigger: Auto-update geometry from lat/lon if provided manually
DROP TRIGGER IF EXISTS trg_assets_geom ON assets;
CREATE TRIGGER trg_assets_geom BEFORE INSERT OR UPDATE ON assets
FOR EACH ROW EXECUTE FUNCTION auto_geom_point();

-- 2.2 SMELTER SPECIFICS
CREATE TABLE IF NOT EXISTS smelter_specs (
    asset_id INT REFERENCES assets(id) ON DELETE CASCADE,
    furnace_type TEXT,
    primary_product TEXT,
    so2_capture_rate FLOAT DEFAULT 0.95,
    energy_source TEXT,
    PRIMARY KEY (asset_id)
);

-- [LAYER 3] INFRASTRUCTURE (The Graph)
-- --------------------------------------------------------------------------------------

-- 3.1 SUPPLY HUBS
CREATE TABLE IF NOT EXISTS supply_hubs (
    id BIGSERIAL PRIMARY KEY,
    osm_id BIGINT UNIQUE,           
    name TEXT,
    type TEXT NOT NULL,             -- 'port', 'rail_yard', 'distribution', 'airport'
    geom GEOMETRY(Geometry, 4326),  
    capacity_rating FLOAT DEFAULT 1.0,
    status TEXT DEFAULT 'ACTIVE',
    state_data JSONB DEFAULT '{}'::jsonb, 
    last_updated TIMESTAMP DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_hubs_geom ON supply_hubs USING GIST(geom);

-- 3.2 SUPPLY LINES (The Edges)
CREATE TABLE IF NOT EXISTS supply_lines (
    line_id BIGINT PRIMARY KEY,     -- Linked to OSM ID
    name TEXT,
    type TEXT NOT NULL,             -- 'rail', 'road', 'pipeline', 'shipping_lane'
    geom GEOMETRY(Geometry, 4326),  
    origin_hub_id INT REFERENCES supply_hubs(id),
    destination_hub_id INT REFERENCES supply_hubs(id),
    state_data JSONB DEFAULT '{}'::jsonb,
    metadata JSONB,
    last_update BIGINT DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_supply_lines_geom ON supply_lines USING GIST(geom);

-- 3.3 CHOKEPOINTS
CREATE TABLE IF NOT EXISTS supply_chokepoints (
    id SERIAL PRIMARY KEY,
    name TEXT,
    type TEXT NOT NULL,             -- 'bridge', 'border', 'tunnel', 'lock'
    geom GEOMETRY(Geometry, 4326),
    max_weight_tons FLOAT,
    max_height_meters FLOAT,
    structural_health FLOAT DEFAULT 1.0,
    political_status FLOAT DEFAULT 1.0,
    metadata JSONB,
    last_updated TIMESTAMP DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_chokepoints_geom ON supply_chokepoints USING GIST(geom);

-- 3.4 TOPOLOGY LINKS
CREATE TABLE IF NOT EXISTS route_dependencies (
    route_id BIGINT REFERENCES supply_lines(line_id) ON DELETE CASCADE,
    chokepoint_id INT REFERENCES supply_chokepoints(id) ON DELETE CASCADE,
    impact_factor FLOAT DEFAULT 1.0,
    distance_from_origin_km FLOAT,
    PRIMARY KEY (route_id, chokepoint_id)
);

-- [LAYER 4] CONNECTIVITY (The Logic)
-- --------------------------------------------------------------------------------------

-- 4.1 PHYSICAL LOGISTICS
CREATE TABLE IF NOT EXISTS asset_supply_mapping (
    asset_id INT REFERENCES assets(id) ON DELETE CASCADE,
    line_id BIGINT REFERENCES supply_lines(line_id) ON DELETE CASCADE,
    hub_id BIGINT REFERENCES supply_hubs(id) ON DELETE CASCADE,
    distance_km FLOAT,
    confidence_score FLOAT DEFAULT 1.0,
    estimated_usage_pct FLOAT DEFAULT 0.0,
    last_inferred TIMESTAMP DEFAULT NOW(),
    PRIMARY KEY (asset_id, line_id)
);

-- 4.2 COMMERCIAL TRADE LINKS
CREATE TABLE IF NOT EXISTS asset_trade_links (
    link_id SERIAL PRIMARY KEY,
    origin_asset_id INT REFERENCES assets(id),
    target_asset_id INT REFERENCES assets(id),
    estimated_volume_tonnes FLOAT,
    implied_freight_cost FLOAT,
    confidence_score FLOAT,
    effective_quarter VARCHAR(10),
    active BOOLEAN DEFAULT TRUE,
    UNIQUE(origin_asset_id, target_asset_id, effective_quarter)
);

-- [LAYER 5] DYNAMIC STATE VECTORS (The Memory)
-- --------------------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS asset_states (
    asset_id INT PRIMARY KEY REFERENCES assets(id),
    op_health FLOAT DEFAULT 1.0,
    fin_health FLOAT DEFAULT 1.0,
    threat_level FLOAT DEFAULT 0.0,
    last_update BIGINT
);

CREATE TABLE IF NOT EXISTS supply_states (
    line_id BIGINT PRIMARY KEY REFERENCES supply_lines(line_id),
    type TEXT,
    state_data JSONB,
    last_update BIGINT
);

-- [LAYER 6] FINANCIAL CORE (The Brainstem)
-- --------------------------------------------------------------------------------------

-- 6.1 TICKER REGISTRY
CREATE TABLE IF NOT EXISTS ticker_registry (
    symbol TEXT PRIMARY KEY,        -- 'HG', 'LME_CU'
    instrument_type TEXT NOT NULL,  -- 'future', 'stock', 'commodity_spot'
    exchange TEXT,
    multiplier FLOAT DEFAULT 1.0,
    active BOOLEAN DEFAULT TRUE,
    metadata JSONB,                 -- Stores 'underlying', 'strike', 'company'
    last_updated TIMESTAMP DEFAULT NOW()
);

-- 6.2 TICKER STATES (Time Series)
CREATE TABLE IF NOT EXISTS ticker_states (
    symbol TEXT REFERENCES ticker_registry(symbol),
    time_bucket TIMESTAMP DEFAULT NOW(),
    price FLOAT,
    volatility FLOAT,
    trend_score FLOAT,              -- Critical for Associative Memory
    greeks JSONB
);
CREATE INDEX IF NOT EXISTS idx_ticker_states_lookup ON ticker_states(symbol, time_bucket DESC);

-- 6.3 NERVOUS SYSTEM MAPPING
CREATE TABLE IF NOT EXISTS ticker_sensitivity (
    ticker_symbol TEXT REFERENCES ticker_registry(symbol),
    entity_id TEXT NOT NULL, 
    weight FLOAT NOT NULL,   
    PRIMARY KEY (ticker_symbol, entity_id)
);

-- 6.4 QUARTERLY FINANCIALS
CREATE TABLE IF NOT EXISTS quarterly_financials (
    report_id SERIAL PRIMARY KEY,
    asset_id INT REFERENCES assets(id),
    quarter TEXT,                   
    revenue FLOAT,
    ebitda FLOAT,
    production_vol FLOAT,           
    reported_production_tonnes FLOAT, 
    reported_freight_expense_usd FLOAT,
    reported_realized_price FLOAT,
    reported_tcrc_expense FLOAT,
    source_doc TEXT,
    report_json JSONB,
    UNIQUE(asset_id, quarter)
);

-- [LAYER 7] SENSORY INGEST (Raw Data Logs)
-- --------------------------------------------------------------------------------------

-- 7.1 SEISMIC STATIONS
CREATE TABLE IF NOT EXISTS earthquake_stations (
    network TEXT,
    station TEXT,
    frequency TEXT DEFAULT 'BH?',
    active BOOLEAN DEFAULT TRUE,
    latitude FLOAT,
    longitude FLOAT,
    sensitivity FLOAT,
    units TEXT,
    geom GEOMETRY(Point, 4326), 
    PRIMARY KEY (network, station)
);
DROP TRIGGER IF EXISTS trg_stations_geom ON earthquake_stations;
CREATE TRIGGER trg_stations_geom BEFORE INSERT OR UPDATE ON earthquake_stations
FOR EACH ROW EXECUTE FUNCTION auto_geom_point();

-- 7.2 EARTHQUAKES
CREATE TABLE IF NOT EXISTS earthquakes (
    id TEXT PRIMARY KEY,            
    magnitude FLOAT,
    intensity FLOAT,                
    lat FLOAT,
    lon FLOAT,
    start_time BIGINT,
    history JSONB,
    geom GEOMETRY(Point, 4326)
);
CREATE INDEX IF NOT EXISTS idx_earthquakes_geom ON earthquakes USING GIST(geom);
CREATE INDEX IF NOT EXISTS idx_earthquakes_time ON earthquakes(start_time);

-- 7.3 AVIATION REGISTRY
CREATE TABLE IF NOT EXISTS aircraft_registry (
    icao24 TEXT PRIMARY KEY,
    registration TEXT,
    manufacturer TEXT,
    model TEXT,
    typecode TEXT,
    operator TEXT,
    owner TEXT
);

CREATE TABLE IF NOT EXISTS aircraft_profiles (
    icao_hex TEXT PRIMARY KEY,
    owner_entity TEXT,
    category TEXT,
    is_watchlist BOOLEAN DEFAULT FALSE,
    tail_number TEXT,
    typical_route_start TEXT,
    typical_route_end TEXT
);

-- 7.4 FLIGHT LEGS
CREATE TABLE IF NOT EXISTS flight_legs (
    id SERIAL PRIMARY KEY,
    icao24 TEXT,
    callsign TEXT,
    lat FLOAT,
    lon FLOAT,
    max_alt FLOAT,
    max_v_rate FLOAT,
    avg_velocity FLOAT,
    heading FLOAT,
    trajectory GEOMETRY(LineString, 4326),
    status TEXT, 
    last_seen TIMESTAMP DEFAULT NOW(),
    geom GEOMETRY(Point, 4326) 
);
CREATE INDEX IF NOT EXISTS idx_flight_legs_traj ON flight_legs USING GIST(trajectory);
CREATE INDEX IF NOT EXISTS idx_flights_icao ON flight_legs(icao24);
-- Trigger for flight geometry
DROP TRIGGER IF EXISTS trg_flight_geom ON flight_legs;
CREATE TRIGGER trg_flight_geom BEFORE INSERT OR UPDATE ON flight_legs
FOR EACH ROW EXECUTE FUNCTION auto_geom_log();

CREATE TABLE IF NOT EXISTS baselines_aviation (
    route_key TEXT PRIMARY KEY,
    avg_altitude FLOAT,
    stddev_altitude FLOAT,
    avg_velocity FLOAT,
    weekly_frequency FLOAT,
    last_updated TIMESTAMP
);

-- 7.5 MARITIME PROFILES
CREATE TABLE IF NOT EXISTS maritime_vessel_profiles (
    mmsi BIGINT PRIMARY KEY,
    vessel_name TEXT,
    vessel_type INT, 
    category TEXT DEFAULT 'Unknown', 
    is_watchlist BOOLEAN DEFAULT FALSE,
    confidence_score FLOAT DEFAULT 0.0,
    last_updated TIMESTAMP DEFAULT NOW()
);

-- 7.6 MARITIME LOGS (The source of your current errors)
-- Fixed: timestamp is TIMESTAMP (was causing 'abort transaction' when passed NOW())
CREATE TABLE IF NOT EXISTS maritime_voyage_logs (
    id SERIAL PRIMARY KEY,
    mmsi BIGINT,
    lat FLOAT,
    lon FLOAT,
    speed FLOAT,
    heading FLOAT,
    nav_status INT, 
    draft FLOAT,    
    timestamp TIMESTAMP, -- Type Fixed
    geom GEOMETRY(Point, 4326)
);
CREATE INDEX IF NOT EXISTS idx_maritime_geom ON maritime_voyage_logs USING GIST(geom);
CREATE INDEX IF NOT EXISTS idx_maritime_mmsi_ts ON maritime_voyage_logs (mmsi, timestamp);
-- Trigger for maritime geometry
DROP TRIGGER IF EXISTS trg_maritime_geom ON maritime_voyage_logs;
CREATE TRIGGER trg_maritime_geom BEFORE INSERT OR UPDATE ON maritime_voyage_logs
FOR EACH ROW EXECUTE FUNCTION auto_geom_log();

-- [LAYER 8] REFERENCE DATA
-- --------------------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS spatial_ref.world_cities (
    geonameid         int,
    name              text,
    asciiname         text,
    alternatenames    text,
    latitude          float,
    longitude         float,
    feature_class     char(1),
    feature_code      text,
    country_code      text,
    cc2               text,
    admin1_code       text,
    admin2_code       text,
    admin3_code       text,
    admin4_code       text,
    population        bigint,
    elevation         int,
    dem               int,
    timezone          text,
    modification_date date,
    coords            geometry(Point, 4326)
);
CREATE INDEX IF NOT EXISTS idx_world_cities_coords ON spatial_ref.world_cities USING GIST (coords);

-- [LAYER 9] HELPER FUNCTIONS
-- --------------------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION check_supply_exposure(event_lat FLOAT, event_lon FLOAT)
RETURNS TABLE (line_id BIGINT, type TEXT, dist_km FLOAT) AS $$
BEGIN
    RETURN QUERY
    SELECT 
        sl.line_id, 
        sl.type, 
        (ST_Distance(sl.geom, ST_SetSRID(ST_MakePoint(event_lon, event_lat), 4326)::geography) / 1000.0)::FLOAT
    FROM supply_lines sl
    WHERE ST_DWithin(sl.geom, ST_SetSRID(ST_MakePoint(event_lon, event_lat), 4326)::geography, 100000); 
END;
$$ LANGUAGE plpgsql;

-- [LAYER 10] SEED DATA
-- --------------------------------------------------------------------------------------

-- SEISMIC STATIONS (GSN)
INSERT INTO earthquake_stations (network, station, frequency, active, latitude, longitude, sensitivity, units) VALUES
('IU', 'ANMO', 'BH?', TRUE, 34.9459, -106.4572, 5.2e8, 'm/s'),
('IU', 'MAJO', 'BH?', TRUE, 36.5427, 138.2070, 5.2e8, 'm/s'),
('IU', 'KEV',  'BH?', TRUE, 69.7553, 27.0066, 5.2e8, 'm/s'),
('IU', 'TRQA', 'BH?', TRUE, -34.00, -64.00, 5.2e8, 'm/s'),
('II', 'PFO',  'BH?', TRUE, 33.6107, -116.455, 5.2e8, 'm/s');