--
-- PostgreSQL database dump
--

-- Dumped from database version 15.4 (Debian 15.4-1.pgdg110+1)
-- Dumped by pg_dump version 15.4 (Debian 15.4-1.pgdg110+1)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: spatial_ref; Type: SCHEMA; Schema: -; Owner: rocco_admin
--

CREATE SCHEMA spatial_ref;


ALTER SCHEMA spatial_ref OWNER TO rocco_admin;

--
-- Name: postgis; Type: EXTENSION; Schema: -; Owner: -
--

CREATE EXTENSION IF NOT EXISTS postgis WITH SCHEMA public;


--
-- Name: EXTENSION postgis; Type: COMMENT; Schema: -; Owner: 
--

COMMENT ON EXTENSION postgis IS 'PostGIS geometry and geography spatial types and functions';


--
-- Name: check_supply_exposure(double precision, double precision); Type: FUNCTION; Schema: public; Owner: rocco_admin
--

CREATE FUNCTION public.check_supply_exposure(ev_lat double precision, ev_lon double precision) RETURNS TABLE(line_id integer, type character varying, dist_km double precision)
    LANGUAGE plpgsql
    AS $$
BEGIN
    RETURN QUERY
    SELECT 
        slg.line_id, 
        slg.line_type, 
        ST_Distance(
            slg.geom::geography, 
            ST_SetSRID(ST_MakePoint(ev_lon, ev_lat), 4326)::geography
        ) / 1000.0 AS dist_km
    FROM supply_line_geometries slg
    WHERE ST_DWithin(
        slg.geom::geography, 
        ST_SetSRID(ST_MakePoint(ev_lon, ev_lat), 4326)::geography, 
        150000 
    );
END;
$$;


ALTER FUNCTION public.check_supply_exposure(ev_lat double precision, ev_lon double precision) OWNER TO rocco_admin;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: active_flights; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.active_flights (
    icao24 text NOT NULL,
    callsign text,
    latitude numeric(10,6),
    longitude numeric(10,6),
    is_military boolean DEFAULT false,
    last_seen timestamp without time zone DEFAULT CURRENT_TIMESTAMP
);


ALTER TABLE public.active_flights OWNER TO rocco_admin;

--
-- Name: aircraft_discovery_logs; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.aircraft_discovery_logs (
    id integer NOT NULL,
    icao_hex text NOT NULL,
    station_id text,
    lat numeric(10,6),
    lon numeric(10,6),
    altitude_ft double precision,
    velocity_kts double precision,
    detected_at timestamp without time zone DEFAULT CURRENT_TIMESTAMP
);


ALTER TABLE public.aircraft_discovery_logs OWNER TO rocco_admin;

--
-- Name: aircraft_discovery_logs_id_seq; Type: SEQUENCE; Schema: public; Owner: rocco_admin
--

CREATE SEQUENCE public.aircraft_discovery_logs_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.aircraft_discovery_logs_id_seq OWNER TO rocco_admin;

--
-- Name: aircraft_discovery_logs_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: rocco_admin
--

ALTER SEQUENCE public.aircraft_discovery_logs_id_seq OWNED BY public.aircraft_discovery_logs.id;


--
-- Name: aircraft_profiles; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.aircraft_profiles (
    icao_hex text NOT NULL,
    tail_number text,
    owner_entity text,
    category text,
    typical_route_start text,
    typical_route_end text,
    is_watchlist boolean DEFAULT true,
    last_seen_at timestamp without time zone DEFAULT CURRENT_TIMESTAMP
);


ALTER TABLE public.aircraft_profiles OWNER TO rocco_admin;

--
-- Name: aircraft_registry; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.aircraft_registry (
    icao24 character varying(50) NOT NULL,
    registration text,
    manufacturer text,
    model text,
    typecode text,
    operator text,
    owner text
);


ALTER TABLE public.aircraft_registry OWNER TO rocco_admin;

--
-- Name: asset_ownership; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.asset_ownership (
    asset_id integer,
    ticker text NOT NULL,
    stake_percentage numeric(5,2)
);


ALTER TABLE public.asset_ownership OWNER TO rocco_admin;

--
-- Name: asset_states; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.asset_states (
    asset_id character varying(50) NOT NULL,
    op_health double precision,
    fin_health double precision,
    threat_level double precision,
    last_update bigint
);


ALTER TABLE public.asset_states OWNER TO rocco_admin;

--
-- Name: asset_supply_mapping; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.asset_supply_mapping (
    asset_id integer NOT NULL,
    line_id character varying(50) NOT NULL
);


ALTER TABLE public.asset_supply_mapping OWNER TO rocco_admin;

--
-- Name: assets; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.assets (
    id integer NOT NULL,
    name text NOT NULL,
    commodity_types text[],
    latitude numeric(10,6) NOT NULL,
    longitude numeric(10,6) NOT NULL,
    sensitivity_radius_km double precision DEFAULT 15.0,
    geom public.geometry(Point,4326)
);


ALTER TABLE public.assets OWNER TO rocco_admin;

--
-- Name: assets_id_seq; Type: SEQUENCE; Schema: public; Owner: rocco_admin
--

CREATE SEQUENCE public.assets_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.assets_id_seq OWNER TO rocco_admin;

--
-- Name: assets_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: rocco_admin
--

ALTER SEQUENCE public.assets_id_seq OWNED BY public.assets.id;


--
-- Name: baselines_aviation; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.baselines_aviation (
    route_key text NOT NULL,
    avg_altitude double precision,
    stddev_altitude double precision,
    avg_velocity double precision,
    avg_ascent_rate double precision,
    weekly_frequency double precision,
    last_updated timestamp without time zone DEFAULT CURRENT_TIMESTAMP
);


ALTER TABLE public.baselines_aviation OWNER TO rocco_admin;

--
-- Name: earthquake_stations; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.earthquake_stations (
    id integer NOT NULL,
    network character varying(10) NOT NULL,
    station character varying(10) NOT NULL,
    location character varying(10),
    frequency character varying(10),
    latitude numeric(10,6),
    longitude numeric(10,6),
    active boolean DEFAULT true,
    last_contact timestamp without time zone,
    sensitivity double precision,
    sensfreq double precision,
    units text
);


ALTER TABLE public.earthquake_stations OWNER TO rocco_admin;

--
-- Name: earthquake_stations_id_seq; Type: SEQUENCE; Schema: public; Owner: rocco_admin
--

CREATE SEQUENCE public.earthquake_stations_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.earthquake_stations_id_seq OWNER TO rocco_admin;

--
-- Name: earthquake_stations_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: rocco_admin
--

ALTER SEQUENCE public.earthquake_stations_id_seq OWNED BY public.earthquake_stations.id;


--
-- Name: earthquakes; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.earthquakes (
    id text NOT NULL,
    magnitude double precision,
    intensity double precision,
    lat double precision,
    lon double precision,
    start_time bigint,
    geom public.geometry(Point,4326),
    history jsonb
);


ALTER TABLE public.earthquakes OWNER TO rocco_admin;

--
-- Name: flight_legs; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.flight_legs (
    leg_id uuid DEFAULT gen_random_uuid() NOT NULL,
    icao24 character varying(6) NOT NULL,
    callsign text,
    trajectory public.geometry(LineString,4326),
    start_time timestamp with time zone DEFAULT CURRENT_TIMESTAMP,
    last_seen timestamp with time zone DEFAULT CURRENT_TIMESTAMP,
    max_alt real,
    max_v_rate real,
    avg_velocity real,
    status character varying(20) DEFAULT 'ACTIVE'::character varying,
    predicted_cat text,
    confidence_score real DEFAULT 0.0
);


ALTER TABLE public.flight_legs OWNER TO rocco_admin;

--
-- Name: military_zones; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.military_zones (
    id integer NOT NULL,
    name text,
    zone_type text,
    latitude numeric(10,6),
    longitude numeric(10,6),
    radius_km double precision,
    CONSTRAINT military_zones_zone_type_check CHECK ((zone_type = ANY (ARRAY['conflict'::text, 'base'::text])))
);


ALTER TABLE public.military_zones OWNER TO rocco_admin;

--
-- Name: military_zones_id_seq; Type: SEQUENCE; Schema: public; Owner: rocco_admin
--

CREATE SEQUENCE public.military_zones_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.military_zones_id_seq OWNER TO rocco_admin;

--
-- Name: military_zones_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: rocco_admin
--

ALTER SEQUENCE public.military_zones_id_seq OWNED BY public.military_zones.id;


--
-- Name: seismic_events; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.seismic_events (
    id integer NOT NULL,
    event_time timestamp without time zone DEFAULT (now() AT TIME ZONE 'utc'::text),
    network character varying(10),
    station character varying(10),
    amplitude double precision,
    duration double precision,
    raw_data jsonb
);


ALTER TABLE public.seismic_events OWNER TO rocco_admin;

--
-- Name: seismic_events_id_seq; Type: SEQUENCE; Schema: public; Owner: rocco_admin
--

CREATE SEQUENCE public.seismic_events_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.seismic_events_id_seq OWNER TO rocco_admin;

--
-- Name: seismic_events_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: rocco_admin
--

ALTER SEQUENCE public.seismic_events_id_seq OWNED BY public.seismic_events.id;


--
-- Name: signal_logs; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.signal_logs (
    id integer NOT NULL,
    station_id text,
    classification text,
    confidence double precision,
    impacted_asset_id integer,
    is_voided boolean DEFAULT false,
    created_at timestamp without time zone DEFAULT CURRENT_TIMESTAMP,
    flight_icao text,
    flight_category text
);


ALTER TABLE public.signal_logs OWNER TO rocco_admin;

--
-- Name: signal_logs_id_seq; Type: SEQUENCE; Schema: public; Owner: rocco_admin
--

CREATE SEQUENCE public.signal_logs_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.signal_logs_id_seq OWNER TO rocco_admin;

--
-- Name: signal_logs_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: rocco_admin
--

ALTER SEQUENCE public.signal_logs_id_seq OWNED BY public.signal_logs.id;


--
-- Name: supply_line_geometries; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.supply_line_geometries (
    id integer NOT NULL,
    line_id integer NOT NULL,
    line_name character varying(255),
    line_type character varying(50),
    geom public.geometry(LineString,4326)
);


ALTER TABLE public.supply_line_geometries OWNER TO rocco_admin;

--
-- Name: supply_line_geometries_id_seq; Type: SEQUENCE; Schema: public; Owner: rocco_admin
--

CREATE SEQUENCE public.supply_line_geometries_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public.supply_line_geometries_id_seq OWNER TO rocco_admin;

--
-- Name: supply_line_geometries_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: rocco_admin
--

ALTER SEQUENCE public.supply_line_geometries_id_seq OWNED BY public.supply_line_geometries.id;


--
-- Name: supply_states; Type: TABLE; Schema: public; Owner: rocco_admin
--

CREATE TABLE public.supply_states (
    line_id character varying(50) NOT NULL,
    type character varying(50),
    state_data jsonb,
    last_update bigint
);


ALTER TABLE public.supply_states OWNER TO rocco_admin;

--
-- Name: world_cities; Type: TABLE; Schema: spatial_ref; Owner: rocco_admin
--

CREATE TABLE spatial_ref.world_cities (
    geonameid integer,
    name text,
    asciiname text,
    alternatenames text,
    latitude double precision,
    longitude double precision,
    feature_class character(1),
    feature_code text,
    country_code text,
    cc2 text,
    admin1_code text,
    admin2_code text,
    admin3_code text,
    admin4_code text,
    population bigint,
    elevation integer,
    dem integer,
    timezone text,
    modification_date date,
    coords public.geometry(Point,4326)
);


ALTER TABLE spatial_ref.world_cities OWNER TO rocco_admin;

--
-- Name: aircraft_discovery_logs id; Type: DEFAULT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.aircraft_discovery_logs ALTER COLUMN id SET DEFAULT nextval('public.aircraft_discovery_logs_id_seq'::regclass);


--
-- Name: assets id; Type: DEFAULT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.assets ALTER COLUMN id SET DEFAULT nextval('public.assets_id_seq'::regclass);


--
-- Name: earthquake_stations id; Type: DEFAULT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.earthquake_stations ALTER COLUMN id SET DEFAULT nextval('public.earthquake_stations_id_seq'::regclass);


--
-- Name: military_zones id; Type: DEFAULT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.military_zones ALTER COLUMN id SET DEFAULT nextval('public.military_zones_id_seq'::regclass);


--
-- Name: seismic_events id; Type: DEFAULT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.seismic_events ALTER COLUMN id SET DEFAULT nextval('public.seismic_events_id_seq'::regclass);


--
-- Name: signal_logs id; Type: DEFAULT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.signal_logs ALTER COLUMN id SET DEFAULT nextval('public.signal_logs_id_seq'::regclass);


--
-- Name: supply_line_geometries id; Type: DEFAULT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.supply_line_geometries ALTER COLUMN id SET DEFAULT nextval('public.supply_line_geometries_id_seq'::regclass);


--
-- Name: active_flights active_flights_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.active_flights
    ADD CONSTRAINT active_flights_pkey PRIMARY KEY (icao24);


--
-- Name: aircraft_discovery_logs aircraft_discovery_logs_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.aircraft_discovery_logs
    ADD CONSTRAINT aircraft_discovery_logs_pkey PRIMARY KEY (id);


--
-- Name: aircraft_profiles aircraft_profiles_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.aircraft_profiles
    ADD CONSTRAINT aircraft_profiles_pkey PRIMARY KEY (icao_hex);


--
-- Name: aircraft_registry aircraft_registry_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.aircraft_registry
    ADD CONSTRAINT aircraft_registry_pkey PRIMARY KEY (icao24);


--
-- Name: asset_states asset_states_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.asset_states
    ADD CONSTRAINT asset_states_pkey PRIMARY KEY (asset_id);


--
-- Name: asset_supply_mapping asset_supply_mapping_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.asset_supply_mapping
    ADD CONSTRAINT asset_supply_mapping_pkey PRIMARY KEY (asset_id, line_id);


--
-- Name: assets assets_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.assets
    ADD CONSTRAINT assets_pkey PRIMARY KEY (id);


--
-- Name: baselines_aviation baselines_aviation_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.baselines_aviation
    ADD CONSTRAINT baselines_aviation_pkey PRIMARY KEY (route_key);


--
-- Name: earthquake_stations earthquake_stations_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.earthquake_stations
    ADD CONSTRAINT earthquake_stations_pkey PRIMARY KEY (id);


--
-- Name: earthquakes earthquakes_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.earthquakes
    ADD CONSTRAINT earthquakes_pkey PRIMARY KEY (id);


--
-- Name: flight_legs flight_legs_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.flight_legs
    ADD CONSTRAINT flight_legs_pkey PRIMARY KEY (leg_id);


--
-- Name: military_zones military_zones_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.military_zones
    ADD CONSTRAINT military_zones_pkey PRIMARY KEY (id);


--
-- Name: seismic_events seismic_events_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.seismic_events
    ADD CONSTRAINT seismic_events_pkey PRIMARY KEY (id);


--
-- Name: signal_logs signal_logs_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.signal_logs
    ADD CONSTRAINT signal_logs_pkey PRIMARY KEY (id);


--
-- Name: supply_line_geometries supply_line_geometries_line_id_key; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.supply_line_geometries
    ADD CONSTRAINT supply_line_geometries_line_id_key UNIQUE (line_id);


--
-- Name: supply_line_geometries supply_line_geometries_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.supply_line_geometries
    ADD CONSTRAINT supply_line_geometries_pkey PRIMARY KEY (id);


--
-- Name: supply_states supply_states_pkey; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.supply_states
    ADD CONSTRAINT supply_states_pkey PRIMARY KEY (line_id);


--
-- Name: assets unique_mine_name; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.assets
    ADD CONSTRAINT unique_mine_name UNIQUE (name);


--
-- Name: earthquake_stations unique_network_station; Type: CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.earthquake_stations
    ADD CONSTRAINT unique_network_station UNIQUE (network, station);


--
-- Name: idx_assets_geom; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_assets_geom ON public.assets USING gist (geom);


--
-- Name: idx_baseline_route; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_baseline_route ON public.baselines_aviation USING btree (route_key);


--
-- Name: idx_earthquakes_geom; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_earthquakes_geom ON public.earthquakes USING gist (public.st_setsrid(public.st_makepoint(lon, lat), 4326));


--
-- Name: idx_flight_coords; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_flight_coords ON public.active_flights USING btree (latitude, longitude);


--
-- Name: idx_icao_status; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_icao_status ON public.flight_legs USING btree (icao24, status);


--
-- Name: idx_registry_operator; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_registry_operator ON public.aircraft_registry USING btree (operator);


--
-- Name: idx_registry_owner; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_registry_owner ON public.aircraft_registry USING btree (owner);


--
-- Name: idx_supply_line_geom; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_supply_line_geom ON public.supply_line_geometries USING gist (geom);


--
-- Name: idx_trajectory_spatial; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE INDEX idx_trajectory_spatial ON public.flight_legs USING gist (trajectory);


--
-- Name: idx_unique_active_icao; Type: INDEX; Schema: public; Owner: rocco_admin
--

CREATE UNIQUE INDEX idx_unique_active_icao ON public.flight_legs USING btree (icao24) WHERE ((status)::text = 'ACTIVE'::text);


--
-- Name: idx_world_cities_coords; Type: INDEX; Schema: spatial_ref; Owner: rocco_admin
--

CREATE INDEX idx_world_cities_coords ON spatial_ref.world_cities USING gist (coords);


--
-- Name: idx_world_cities_pop; Type: INDEX; Schema: spatial_ref; Owner: rocco_admin
--

CREATE INDEX idx_world_cities_pop ON spatial_ref.world_cities USING btree (population);


--
-- Name: asset_ownership asset_ownership_asset_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.asset_ownership
    ADD CONSTRAINT asset_ownership_asset_id_fkey FOREIGN KEY (asset_id) REFERENCES public.assets(id);


--
-- Name: asset_supply_mapping asset_supply_mapping_asset_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.asset_supply_mapping
    ADD CONSTRAINT asset_supply_mapping_asset_id_fkey FOREIGN KEY (asset_id) REFERENCES public.assets(id);


--
-- Name: seismic_events fk_station; Type: FK CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.seismic_events
    ADD CONSTRAINT fk_station FOREIGN KEY (network, station) REFERENCES public.earthquake_stations(network, station);


--
-- Name: signal_logs signal_logs_impacted_asset_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: rocco_admin
--

ALTER TABLE ONLY public.signal_logs
    ADD CONSTRAINT signal_logs_impacted_asset_id_fkey FOREIGN KEY (impacted_asset_id) REFERENCES public.assets(id);


--
-- PostgreSQL database dump complete
--

