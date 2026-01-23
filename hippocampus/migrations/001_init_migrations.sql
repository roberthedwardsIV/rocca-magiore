CREATE TABLE earthquake_stations (
    id SERIAL PRIMARY KEY,
    network VARCHAR(10) NOT NULL,
    station VARCHAR(10) NOT NULL,
    location VARCHAR(10),   
    frequency VARCHAR(10),
    latitude NUMERIC(10, 6),
    longitude NUMERIC(10, 6),
    active BOOLEAN DEFAULT TRUE,
    last_contact TIMESTAMP
);

ALTER TABLE earthquake_stations 
ADD CONSTRAINT unique_network_station UNIQUE (network, station);

CREATE TABLE IF NOT EXISTS seismic_events (
    id SERIAL PRIMARY KEY,
    event_time TIMESTAMP WITHOUT TIME ZONE DEFAULT (NOW() AT TIME ZONE 'utc'),
    network VARCHAR(10),
    station VARCHAR(10),
    amplitude FLOAT,
    duration FLOAT,
    raw_data JSONB,
    CONSTRAINT fk_station
        FOREIGN KEY(network, station) 
        REFERENCES earthquake_stations(network, station)
);


-- MIGRATION 003
-- Asset Registry (Mines, Refineries, Ports)
CREATE TABLE IF NOT EXISTS assets (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    commodity_types TEXT[], -- ARRAY['Copper', 'Gold']
    latitude NUMERIC(10, 6) NOT NULL,
    longitude NUMERIC(10, 6) NOT NULL,
    sensitivity_radius_km FLOAT DEFAULT 15.0
);



-- Asset Ownership (Equity/Options Mapping)
CREATE TABLE IF NOT EXISTS asset_ownership (
    asset_id INTEGER REFERENCES assets(id),
    ticker TEXT NOT NULL, -- 'BHP', 'RIO'
    stake_percentage NUMERIC(5, 2)
);

-- Military Zones (Conflict vs. Base)
CREATE TABLE IF NOT EXISTS military_zones (
    id SERIAL PRIMARY KEY,
    name TEXT,
    zone_type TEXT CHECK (zone_type IN ('conflict', 'base')),
    latitude NUMERIC(10, 6),
    longitude NUMERIC(10, 6),
    radius_km FLOAT
);



-- MIGRATION 004
CREATE TABLE IF NOT EXISTS active_flights (
    icao24 TEXT PRIMARY KEY,
    callsign TEXT,
    latitude NUMERIC(10, 6),
    longitude NUMERIC(10, 6),
    is_military BOOLEAN DEFAULT FALSE,
    last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Index for fast proximity lookups
CREATE INDEX idx_flight_coords ON active_flights (latitude, longitude);



-- MIGRATION 005
CREATE TABLE IF NOT EXISTS signal_logs (
    id SERIAL PRIMARY KEY,
    station_id TEXT,
    classification TEXT,
    confidence FLOAT,
    impacted_asset_id INTEGER REFERENCES assets(id),
    is_voided BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);



-- MIGRATION 006
CREATE TABLE IF NOT EXISTS aircraft_profiles (
    icao_hex TEXT PRIMARY KEY,
    tail_number TEXT,
    owner_entity TEXT, -- 'BHP', 'Rio Tinto', 'Glencore'
    category TEXT CHECK (category IN ('corporate', 'logistics_heavy', 'worker_transport', 'military')),
    typical_route_start TEXT, -- Links to asset name/ID
    typical_route_end TEXT,   
    is_watchlist BOOLEAN DEFAULT TRUE,
    last_seen_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS aircraft_discovery_logs (
    id SERIAL PRIMARY KEY,
    icao_hex TEXT NOT NULL,
    station_id TEXT,
    lat NUMERIC(10,6),
    lon NUMERIC(10,6),
    altitude_ft FLOAT,
    velocity_kts FLOAT,
    detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

ALTER TABLE signal_logs ADD COLUMN flight_icao TEXT;
ALTER TABLE signal_logs ADD COLUMN flight_category TEXT;




-- MIGRATION 007
CREATE EXTENSION IF NOT EXISTS postgis;

CREATE TABLE flight_legs (
    leg_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    icao24 VARCHAR(6) NOT NULL,
    callsign TEXT,
    trajectory GEOMETRY(LineString, 4326), 
    start_time TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
    last_seen TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
    max_alt REAL,
    max_v_rate REAL,
    avg_velocity REAL,
    status VARCHAR(20) DEFAULT 'ACTIVE',
    predicted_cat TEXT, -- M&A, Supply, Military, Exploration
    confidence_score REAL DEFAULT 0.0
);

CREATE INDEX idx_trajectory_spatial ON flight_legs USING GIST (trajectory);
CREATE INDEX idx_icao_status ON flight_legs (icao24, status);


CREATE UNIQUE INDEX idx_unique_active_icao 
ON flight_legs (icao24) 
WHERE status = 'ACTIVE';


CREATE TABLE IF NOT EXISTS asset_supply_mapping (
    asset_id INTEGER REFERENCES assets(id),
    line_id INTEGER,
    PRIMARY KEY (asset_id, line_id)
);
CREATE TABLE IF NOT EXISTS asset_states (
    asset_id INTEGER PRIMARY KEY,
    op_health FLOAT,
    fin_health FLOAT,
    threat_level FLOAT,
    last_update BIGINT
);
CREATE TABLE IF NOT EXISTS supply_states (
    line_id INTEGER PRIMARY KEY,
    type VARCHAR(50),
    state_data JSONB,
    last_update BIGINT
);