-- Configuration
CREATE EXTENSION IF NOT EXISTS postgis;
CREATE SCHEMA IF NOT EXISTS spatial_ref;

--------------------------------------------------------------------------------
-- PHYSICAL INFRASTRUCTURE
--------------------------------------------------------------------------------

-- Core Physical Assets (Mines, Refineries)
CREATE TABLE assets (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    commodity_types TEXT[] NOT NULL, 
    latitude FLOAT NOT NULL,
    longitude FLOAT NOT NULL,
    sensitivity_radius_km FLOAT DEFAULT 50.0,
    geom GEOMETRY(Point, 4326) GENERATED ALWAYS AS (ST_SetSRID(ST_MakePoint(longitude, latitude), 4326)) STORED
);
CREATE INDEX idx_assets_geom ON assets USING GIST(geom);

-- Supply Lines (Rail, Ports, Pipes)
CREATE TABLE supply_lines (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    type TEXT NOT NULL, 
    geom GEOMETRY(Geometry, 4326) 
);
CREATE INDEX idx_supply_geom ON supply_lines USING GIST(geom);

-- Asset to Supply Mapping (Physical Logistics Layer)
CREATE TABLE asset_supply_mapping (
    asset_id INT REFERENCES assets(id) ON DELETE CASCADE,
    line_id INT REFERENCES supply_lines(id) ON DELETE CASCADE,
    confidence_score FLOAT DEFAULT 1.0,    -- AI Confidence
    estimated_usage_pct FLOAT DEFAULT 0.0, -- Volume Allocation
    last_inferred TIMESTAMP DEFAULT NOW(),
    PRIMARY KEY (asset_id, line_id)
);

-- Asset Trade Links (Commercial/Shadow Layer)
CREATE TABLE asset_trade_links (
    link_id SERIAL PRIMARY KEY,
    origin_asset_id INT REFERENCES assets(id),  -- The Mine
    target_asset_id INT REFERENCES assets(id),  -- The Refinery
    estimated_volume_tonnes FLOAT, 
    implied_freight_cost FLOAT,    
    confidence_score FLOAT,        
    effective_quarter VARCHAR(10), 
    active BOOLEAN DEFAULT TRUE,
    UNIQUE(origin_asset_id, target_asset_id, effective_quarter)
);

-- Quarterly Financials (Ground Truth for Solver)
CREATE TABLE quarterly_financials (
    report_id SERIAL PRIMARY KEY,
    asset_id INT REFERENCES assets(id),
    quarter VARCHAR(10),
    reported_production_tonnes FLOAT,
    reported_freight_expense_usd FLOAT,
    reported_realized_price FLOAT,
    reported_tcrc_expense FLOAT,
    source_doc TEXT
);

--------------------------------------------------------------------------------
-- DYNAMIC STATES
--------------------------------------------------------------------------------

-- Live Health of Assets
CREATE TABLE asset_states (
    asset_id INT PRIMARY KEY REFERENCES assets(id),
    op_health FLOAT DEFAULT 1.0,
    fin_health FLOAT DEFAULT 1.0,
    threat_level FLOAT DEFAULT 0.0,
    last_update BIGINT
);

-- Live Health of Supply Lines
CREATE TABLE supply_states (
    line_id INT PRIMARY KEY REFERENCES supply_lines(id),
    type TEXT NOT NULL,
    state_data JSONB, 
    last_update BIGINT
);

-- Earthquake Event History
CREATE TABLE earthquakes (
    id TEXT PRIMARY KEY, 
    magnitude FLOAT,
    intensity FLOAT,
    lat FLOAT,
    lon FLOAT,
    start_time BIGINT,
    history JSONB,
    geom GEOMETRY(Point, 4326)
);
CREATE INDEX idx_eq_geom ON earthquakes USING GIST(geom);

--------------------------------------------------------------------------------
-- FINANCIAL SYSTEMS
--------------------------------------------------------------------------------

-- Master List of Tradable Instruments
CREATE TABLE ticker_registry (
    symbol TEXT PRIMARY KEY,
    instrument_type TEXT NOT NULL, 
    exchange TEXT NOT NULL,
    multiplier FLOAT DEFAULT 1.0,
    active BOOLEAN DEFAULT TRUE,
    metadata JSONB
);

-- Financial Time Series Storage
CREATE TABLE ticker_states (
    time_bucket TIMESTAMP DEFAULT NOW(),
    symbol TEXT REFERENCES ticker_registry(symbol),
    price FLOAT,
    volatility FLOAT,
    greeks JSONB 
);
CREATE INDEX idx_ticker_states_sym_time ON ticker_states(symbol, time_bucket DESC);

-- Nervous System Mapping
CREATE TABLE ticker_sensitivity (
    ticker_symbol TEXT REFERENCES ticker_registry(symbol),
    entity_id TEXT NOT NULL, -- Linked asset entity
    weight FLOAT NOT NULL, -- Correlation strength
    PRIMARY KEY (ticker_symbol, entity_id)
);

--------------------------------------------------------------------------------
-- SENSORY RECEPTORS + FRONTAL LOBE ITEMS
--------------------------------------------------------------------------------

-- Seismic Station Registry
CREATE TABLE earthquake_stations (
    network TEXT,
    station TEXT,
    frequency TEXT DEFAULT 'BH?',
    active BOOLEAN DEFAULT TRUE,
    latitude FLOAT,
    longitude FLOAT,
    sensitivity FLOAT,
    units TEXT,
    geom GEOMETRY(Point, 4326) GENERATED ALWAYS AS (ST_SetSRID(ST_MakePoint(longitude, latitude), 4326)) STORED,
    PRIMARY KEY (network, station)
);

-- Aircraft Registry (Static)
CREATE TABLE aircraft_registry (
    icao24 TEXT PRIMARY KEY,
    registration TEXT,
    manufacturer TEXT,
    model TEXT,
    typecode TEXT,
    operator TEXT,
    owner TEXT
);

-- Aircraft Profiles
CREATE TABLE aircraft_profiles (
    icao_hex TEXT PRIMARY KEY,
    owner_entity TEXT,
    category TEXT,
    is_watchlist BOOLEAN DEFAULT FALSE,
    tail_number TEXT,
    typical_route_start TEXT,
    typical_route_end TEXT
);

-- Flight Legs (Historical)
CREATE TABLE flight_legs (
    id SERIAL PRIMARY KEY,
    icao24 TEXT,
    max_alt FLOAT,
    max_v_rate FLOAT,
    avg_velocity FLOAT,
    trajectory GEOMETRY(LineString, 4326),
    status TEXT, -- 'COMPLETED', 'IN_AIR'
    last_seen TIMESTAMP DEFAULT NOW()
);
CREATE INDEX idx_flight_legs_traj ON flight_legs USING GIST(trajectory);

-- Aviation Baselines
CREATE TABLE baselines_aviation (
    route_key TEXT PRIMARY KEY,
    avg_altitude FLOAT,
    stddev_altitude FLOAT,
    avg_velocity FLOAT,
    weekly_frequency FLOAT,
    last_updated TIMESTAMP
);

-- World Cities Reference
CREATE TABLE spatial_ref.world_cities (
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
CREATE INDEX idx_world_cities_coords ON spatial_ref.world_cities USING GIST (coords);
CREATE INDEX idx_world_cities_pop ON spatial_ref.world_cities (population);

-- Maritime Vessel Watchlist
CREATE TABLE IF NOT EXISTS maritime_vessel_profiles (
    mmsi BIGINT PRIMARY KEY,
    vessel_name TEXT,
    vessel_type INT, 
    category TEXT DEFAULT 'Unknown', 
    is_watchlist BOOLEAN DEFAULT FALSE,
    confidence_score FLOAT DEFAULT 0.0,
    last_updated TIMESTAMP DEFAULT NOW()
);

-- Historical log for the Maritime Trainer to learn from
CREATE TABLE IF NOT EXISTS maritime_voyage_logs (
    id SERIAL PRIMARY KEY,
    mmsi BIGINT,
    lat FLOAT,
    lon FLOAT,
    speed FLOAT,
    heading FLOAT,
    nav_status INT, 
    draft FLOAT,    
    timestamp TIMESTAMP
);

CREATE INDEX idx_maritime_mmsi_ts ON maritime_voyage_logs (mmsi, timestamp);

DO \$\$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'asset_type') THEN
        CREATE TYPE asset_type AS ENUM ('mine', 'refinery');
    END IF;
END
\$\$;

ALTER TYPE asset_type ADD VALUE IF NOT EXISTS 'smelter';
ALTER TYPE asset_type ADD VALUE IF NOT EXISTS 'hub';
ALTER TYPE asset_type ADD VALUE IF NOT EXISTS 'choke_point';

CREATE TABLE IF NOT EXISTS assets (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    commodity_types TEXT[], -- e.g. ['Copper_Ore', 'Gold']
    latitude FLOAT,
    longitude FLOAT,
    geom GEOMETRY(Geometry, 4326),
    source TEXT,
    last_update BIGINT
);
CREATE INDEX IF NOT EXISTS idx_assets_geom ON assets USING GIST (geom);

CREATE TABLE IF NOT EXISTS supply_lines (
    line_id SERIAL PRIMARY KEY,
    name TEXT,
    type TEXT, -- 'rail_route', 'highway', 'shipping_lane'
    geom GEOMETRY(Geometry, 4326),
    state_data JSONB, -- Flexible storage for speed/capacity/etc
    last_update BIGINT
);
CREATE INDEX IF NOT EXISTS idx_supply_lines_geom ON supply_lines USING GIST (geom);

-- Supply Hubs
CREATE TABLE IF NOT EXISTS supply_hubs (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    type TEXT NOT NULL, -- 'rail_yard', 'port', 'airport'
    geom GEOMETRY(Geometry, 4326),
    capacity_rating FLOAT DEFAULT 1.0,
    status TEXT DEFAULT 'ACTIVE',
    last_updated TIMESTAMP DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_hubs_geom ON supply_hubs USING GIST (geom);

-- Supply Chokepoints
CREATE TABLE IF NOT EXISTS supply_chokepoints (
    id SERIAL PRIMARY KEY,
    name TEXT,
    type TEXT NOT NULL, -- 'bridge', 'border', 'tunnel', 'lock'
    geom GEOMETRY(Geometry, 4326),
    max_weight_tons FLOAT,
    max_height_meters FLOAT,
    structural_health FLOAT DEFAULT 1.0,
    political_status FLOAT DEFAULT 1.0,
    last_updated TIMESTAMP DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_chokepoints_geom ON supply_chokepoints USING GIST (geom);

-- Topology Link 
CREATE TABLE IF NOT EXISTS route_dependencies (
    route_id INT REFERENCES supply_lines(line_id) ON DELETE CASCADE,
    chokepoint_id INT REFERENCES supply_chokepoints(id) ON DELETE CASCADE,
    impact_factor FLOAT DEFAULT 1.0,
    distance_from_origin_km FLOAT,
    PRIMARY KEY (route_id, chokepoint_id)
);

-- Smelter Specifics
CREATE TABLE IF NOT EXISTS smelter_specs (
    asset_id INT REFERENCES assets(id) ON DELETE CASCADE,
    furnace_type TEXT,
    primary_product TEXT,
    so2_capture_rate FLOAT DEFAULT 0.95,
    energy_source TEXT
);

-- Connectivity to Supply Lines
ALTER TABLE supply_lines 
ADD COLUMN IF NOT EXISTS origin_hub_id INT REFERENCES supply_hubs(id),
ADD COLUMN IF NOT EXISTS destination_hub_id INT REFERENCES supply_hubs(id);

--------------------------------------------------------------------------------
-- HELPER FUNCTIONS
--------------------------------------------------------------------------------

CREATE OR REPLACE FUNCTION check_supply_exposure(event_lat FLOAT, event_lon FLOAT)
RETURNS TABLE (line_id INT, type TEXT, dist_km FLOAT) AS $$
BEGIN
    RETURN QUERY
    SELECT 
        sl.id, 
        sl.type, 
        (ST_Distance(sl.geom, ST_SetSRID(ST_MakePoint(event_lon, event_lat), 4326)::geography) / 1000.0)::FLOAT
    FROM supply_lines sl
    WHERE ST_DWithin(sl.geom, ST_SetSRID(ST_MakePoint(event_lon, event_lat), 4326)::geography, 100000); -- 100km radius check
END;
$$ LANGUAGE plpgsql;