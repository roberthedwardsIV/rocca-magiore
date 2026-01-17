CREATE EXTENSION IF NOT EXISTS postgis;

CREATE TABLE IF NOT EXISTS earthquakes (
    id TEXT PRIMARY KEY,
    magnitude FLOAT,
    intensity FLOAT,
    lat FLOAT,
    lon FLOAT,
    start_time BIGINT,
    geom GEOMETRY(Point, 4326)
);

-- Index for spatial queries (the ST_Distance part)
CREATE INDEX IF NOT EXISTS idx_earthquakes_geom ON earthquakes USING GIST (ST_MakePoint(lon, lat));