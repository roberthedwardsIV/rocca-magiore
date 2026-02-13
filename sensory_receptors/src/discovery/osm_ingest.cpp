#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/geom/wkb.hpp>
#include <osmium/area/assembler.hpp>            
#include <osmium/area/multipolygon_manager.hpp> 
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

using json = nlohmann::json;

// --- CONFIGURATION ---
const std::string DB_CONN = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";

// Helper: Safely get a tag or return empty string
std::string get_tag(const osmium::TagList& tags, const char* key) {
    const char* val = tags[key];
    return val ? std::string(val) : "";
}

class MegaLogisticsHandler : public osmium::handler::Handler {
    osmium::geom::WKBFactory<> m_factory;
    pqxx::connection m_db;
    pqxx::work* m_work;
    int m_counter = 0;
    const int BATCH_SIZE = 5000;

public:
    MegaLogisticsHandler() : m_db(DB_CONN), m_factory(osmium::geom::wkb_type::wkb, osmium::geom::out_type::hex) {
        m_work = new pqxx::work(m_db);
        // Prepare statements with ON CONFLICT DO NOTHING to handle overlaps
        m_db.prepare("insert_asset", "INSERT INTO assets (name, type, commodity_types, geom, source, last_update) VALUES ($1, $2, $3, ST_GeomFromEWKB(decode($4, 'hex')), 'OSM_ULTRA', 1) ON CONFLICT DO NOTHING");
        m_db.prepare("insert_hub", "INSERT INTO supply_hubs (name, type, geom, capacity_rating, last_updated) VALUES ($1, $2, ST_GeomFromEWKB(decode($3, 'hex')), $4, NOW()) ON CONFLICT DO NOTHING");
        m_db.prepare("insert_route", "INSERT INTO supply_lines (name, type, geom, state_data, last_update) VALUES ($1, $2, ST_GeomFromEWKB(decode($3, 'hex')), $4, 1) ON CONFLICT DO NOTHING");
        m_db.prepare("insert_choke", "INSERT INTO supply_chokepoints (name, type, geom, max_weight_tons, structural_health, last_updated) VALUES ($1, $2, ST_Centroid(ST_GeomFromEWKB(decode($3, 'hex'))), $4, 1.0, NOW()) ON CONFLICT DO NOTHING");
    }

    ~MegaLogisticsHandler() { commit_batch(); }

    void commit_batch() {
        if (m_work) {
            m_work->commit();
            delete m_work;
            m_work = new pqxx::work(m_db);
            m_counter = 0;
        }
    }

    // =================================================================================
    // SECTION 1: EXTRACTION (The Source)
    // Captures: Mines, Quarries, Wells, Rigs, Platforms, Tailings (Risk)
    // =================================================================================
    void process_extraction(const osmium::Area& area) {
        std::string landuse = get_tag(area.tags(), "landuse");
        std::string industrial = get_tag(area.tags(), "industrial");
        std::string man_made = get_tag(area.tags(), "man_made");
        std::string resource = get_tag(area.tags(), "resource");
        std::string name = get_tag(area.tags(), "name");

        std::vector<std::string> comms;
        if (!resource.empty()) comms.push_back(resource);

        // 1.1 Mines & Quarries
        if (landuse == "quarry" || industrial == "mine" || industrial == "mining" || landuse == "salt_pond") {
            save_asset(area, name, "mine", comms);
        }
        // 1.2 Offshore Platforms (Oil/Gas) [Cite: OSM Wiki Map Features]
        else if (man_made == "offshore_platform") {
            comms.push_back("Oil/Gas");
            save_asset(area, name.empty() ? "Offshore Platform" : name, "mine", comms);
        }
        // 1.3 Fracking / Well Sites
        else if (industrial == "wellsite" || industrial == "well_cluster") {
            comms.push_back("Hydrocarbons");
            save_asset(area, name, "mine", comms);
        }
        // 1.4 Tailings Ponds (CRITICAL RISK ASSET)
        // These aren't "active mines" but they are massive liabilities. We map them as Hubs (Waste Storage).
        else if (man_made == "tailings_pond") {
            save_hub(area, name.empty() ? "Tailings Dam" : name, "tailings_storage", 0.0); // 0 capacity = full risk
        }
    }

    // =================================================================================
    // SECTION 2: PROCESSING (The Transformation)
    // Captures: Factories, Smelters, Refineries, Chemical Plants
    // =================================================================================
    void process_industry(const osmium::Area& area) {
        std::string industrial = get_tag(area.tags(), "industrial");
        std::string man_made = get_tag(area.tags(), "man_made");
        std::string product = get_tag(area.tags(), "product");
        std::string name = get_tag(area.tags(), "name");

        // 2.1 Smelters (High Heat)
        if (industrial == "smelter" || industrial == "aluminium_smelting" || product == "aluminium" || product == "steel") {
            save_asset(area, name, "smelter", {});
        }
        // 2.2 Refineries (Chemical/Oil)
        else if (industrial == "refinery" || industrial == "oil" || industrial == "chemical") {
            save_asset(area, name, "refinery", {});
        }
        // 2.3 General Factories (If explicitly industrial)
        else if (man_made == "works" && !name.empty()) {
            // Only capture named works to avoid noise
            save_asset(area, name, "refinery", {}); // Default to refinery class for generic factories
        }
    }

    // =================================================================================
    // SECTION 3: STORAGE & LOGISTICS (The Buffer)
    // Captures: Warehouses, Tanks, Silos, Depots, Terminals
    // =================================================================================
    void process_storage(const osmium::Area& area) {
        std::string building = get_tag(area.tags(), "building");
        std::string man_made = get_tag(area.tags(), "man_made");
        std::string industrial = get_tag(area.tags(), "industrial");
        std::string amenity = get_tag(area.tags(), "amenity");
        std::string name = get_tag(area.tags(), "name");

        // 3.1 Warehousing
        if (building == "warehouse" || industrial == "warehouse" || industrial == "logistics") {
            save_hub(area, name, "warehouse", 0.5);
        }
        // 3.2 Liquid/Bulk Storage
        else if (man_made == "storage_tank" || man_made == "silo" || man_made == "bunker_silo") {
            std::string content = get_tag(area.tags(), "content");
            save_hub(area, content.empty() ? "Storage Tank" : content + " Tank", "storage_tank", 0.2);
        }
        // 3.3 Freight Terminals
        else if (industrial == "depot" || industrial == "terminal" || amenity == "loading_dock") {
            save_hub(area, name, "logistics_terminal", 0.8);
        }
    }

    // =================================================================================
    // SECTION 4: AVIATION (The Air Link)
    // Captures: Airports, Helipads, Runways, Aprons
    // =================================================================================
    void process_aviation(const osmium::Area& area) {
        std::string aeroway = get_tag(area.tags(), "aeroway");
        std::string name = get_tag(area.tags(), "name");
        
        if (aeroway == "aerodrome") {
            std::string iata = get_tag(area.tags(), "iata");
            float cap = iata.empty() ? 0.1 : 1.0;
            save_hub(area, name.empty() ? "Unknown Aerodrome" : name, "airport", cap);
        }
        else if (aeroway == "heliport") {
            save_hub(area, name, "heliport", 0.05);
        }
        else if (aeroway == "runway") {
            // Physical Chokepoint
            float length = 0.0;
            try { length = std::stof(get_tag(area.tags(), "length")); } catch(...) {}
            save_chokepoint_area(area, name, "runway", length); 
        }
    }

    // =================================================================================
    // SECTION 5: MARITIME (The Sea Link)
    // Captures: Ports, Docks, Quays, Dredged Channels
    // =================================================================================
    void process_maritime_area(const osmium::Area& area) {
        std::string industrial = get_tag(area.tags(), "industrial");
        std::string waterway = get_tag(area.tags(), "waterway");
        std::string man_made = get_tag(area.tags(), "man_made");
        std::string name = get_tag(area.tags(), "name");

        if (industrial == "port" || get_tag(area.tags(), "harbour") == "yes") {
            save_hub(area, name, "maritime_port", 1.0);
        }
        else if (waterway == "dock" || man_made == "quay" || man_made == "pier" || man_made == "jetty") {
            save_hub(area, name, "maritime_dock", 0.3);
        }
    }

    // =================================================================================
    // SECTION 6: TRANSPORT ROUTES (The Arteries)
    // Captures: Rail, Road, Conveyors, Pipelines, Waterways
    // =================================================================================
    void process_routes(const osmium::Way& way) {
        std::string railway = get_tag(way.tags(), "railway");
        std::string highway = get_tag(way.tags(), "highway");
        std::string man_made = get_tag(way.tags(), "man_made");
        std::string aerialway = get_tag(way.tags(), "aerialway");
        std::string waterway = get_tag(way.tags(), "waterway");
        std::string name = get_tag(way.tags(), "name");

        // 6.1 Rail (Detailed)
        if (railway == "rail" || railway == "narrow_gauge") {
            json state;
            state["gauge"] = get_tag(way.tags(), "gauge");
            state["electrified"] = get_tag(way.tags(), "electrified");
            state["usage"] = get_tag(way.tags(), "usage"); // industrial/main
            
            std::string service = get_tag(way.tags(), "service");
            std::string type = "rail_mainline";
            if (service == "spur" || service == "siding" || get_tag(way.tags(), "usage") == "industrial") {
                type = "rail_spur";
            }
            save_route(way, name, type, state.dump());
        }

        // 6.2 Road (Heavy Haul)
        else if (highway == "motorway" || highway == "trunk" || highway == "primary") {
            json state;
            state["lanes"] = get_tag(way.tags(), "lanes");
            state["surface"] = get_tag(way.tags(), "surface");
            save_route(way, name, "highway_trunk", state.dump());
        }

        // 6.3 Conveyor Belts (CRITICAL for Mining Logistics)
        //
        else if (man_made == "conveyor_belt" || aerialway == "goods") {
            json state;
            state["resource"] = get_tag(way.tags(), "resource");
            save_route(way, name.empty() ? "Industrial Conveyor" : name, "conveyor", state.dump());
        }

        // 6.4 Pipelines
        else if (man_made == "pipeline") {
            json state;
            state["substance"] = get_tag(way.tags(), "substance");
            state["diameter"] = get_tag(way.tags(), "diameter");
            save_route(way, name, "pipeline", state.dump());
        }

        // 6.5 Inland Waterways (Barges)
        else if (waterway == "river" || waterway == "canal") {
            std::string cemt = get_tag(way.tags(), "CEMT"); // European Classification
            std::string ship = get_tag(way.tags(), "ship");
            if (!cemt.empty() || ship == "yes") {
                json state;
                state["class"] = cemt;
                save_route(way, name, "inland_waterway", state.dump());
            }
        }
    }

    // =================================================================================
    // SECTION 7: CHOKEPOINTS & UTILITIES (The Nervous System)
    // Captures: Bridges, Tunnels, Pumps, Cranes, Telecom, Power
    // =================================================================================
    void process_chokepoints_linear(const osmium::Way& way) {
        std::string bridge = get_tag(way.tags(), "bridge");
        std::string tunnel = get_tag(way.tags(), "tunnel");
        std::string barrier = get_tag(way.tags(), "barrier");
        std::string name = get_tag(way.tags(), "name");

        if (bridge == "yes" || bridge == "viaduct" || bridge == "cantilever") {
            save_chokepoint_linear(way, name, "bridge");
        }
        else if (tunnel == "yes") {
            save_chokepoint_linear(way, name, "tunnel");
        }
        else if (barrier == "border_control") {
            save_chokepoint_linear(way, name, "border_crossing");
        }
    }

    void process_chokepoints_point(const osmium::Node& node) {
        // Sometimes features are just nodes
        std::string man_made = get_tag(node.tags(), "man_made");
        std::string name = get_tag(node.tags(), "name");

        // 7.1 Pumping Stations (Pipeline Chokepoints)
        if (man_made == "pumping_station" || man_made == "pumping_rig") {
            save_chokepoint_node(node, name, "pumping_station");
        }
        // 7.2 Cranes (Port Capacity Chokepoints)
        else if (man_made == "crane") {
            save_chokepoint_node(node, name, "crane");
        }
        // 7.3 Telecom (Coordination Hubs)
        else if (man_made == "mast" || man_made == "tower") {
            if (get_tag(node.tags(), "tower:type") == "communication") {
                save_hub_node(node, name, "telecom_tower");
            }
        }
    }

    // =================================================================================
    // DISPATCHERS
    // =================================================================================
    void area(const osmium::Area& area) {
        process_extraction(area);
        process_industry(area);
        process_storage(area);
        process_aviation(area);
        process_maritime_area(area);
        
        // Power Plants (Assets)
        if (get_tag(area.tags(), "power") == "plant") {
            save_asset(area, get_tag(area.tags(), "name"), "power_plant", {});
        }
        // Water Reservoirs (Hubs)
        if (get_tag(area.tags(), "landuse") == "reservoir") {
            save_hub(area, get_tag(area.tags(), "name"), "water_reservoir", 1.0);
        }
    }

    void way(const osmium::Way& way) {
        process_routes(way);
        process_chokepoints_linear(way);
        
        // Power Lines (High Voltage)
        if (get_tag(way.tags(), "power") == "line") {
            save_route(way, get_tag(way.tags(), "name"), "power_grid", "{}");
        }
    }

    void node(const osmium::Node& node) {
        process_chokepoints_point(node);
    }

private:
    // --- DB HELPERS ---
    void save_asset(const osmium::Area& area, std::string name, std::string type, std::vector<std::string> comms) {
        try {
            std::string wkb = m_factory.create_multipolygon(area);
            if (name.empty()) name = "Unknown " + type;
            std::string c_str = "{";
            for (auto& c : comms) c_str += "\"" + c + "\",";
            if (c_str.length() > 1) c_str.pop_back();
            c_str += "}";
            m_work->exec_prepared("insert_asset", name, type, c_str, wkb);
            check_commit();
        } catch (...) {}
    }

    void save_hub(const osmium::Area& area, std::string name, std::string type, float capacity) {
        try {
            std::string wkb = m_factory.create_multipolygon(area);
            if (name.empty()) name = "Unknown " + type;
            m_work->exec_prepared("insert_hub", name, type, wkb, capacity);
            check_commit();
        } catch (...) {}
    }

    void save_hub_node(const osmium::Node& node, std::string name, std::string type) {
        try {
            std::string wkb = m_factory.create_point(node);
            if (name.empty()) name = "Unknown " + type;
            m_work->exec_prepared("insert_hub", name, type, wkb, 0.1);
            check_commit();
        } catch (...) {}
    }

    void save_route(const osmium::Way& way, std::string name, std::string type, std::string json) {
        try {
            std::string wkb = m_factory.create_linestring(way);
            if (name.empty()) name = "Unnamed " + type;
            m_work->exec_prepared("insert_route", name, type, wkb, json);
            check_commit();
        } catch (...) {}
    }

    void save_chokepoint_linear(const osmium::Way& way, std::string name, std::string type) {
        try {
            std::string wkb = m_factory.create_linestring(way);
            if (name.empty()) name = "Unnamed " + type;
            m_work->exec_prepared("insert_choke", name, type, wkb, 0.0);
            check_commit();
        } catch (...) {}
    }

    void save_chokepoint_area(const osmium::Area& area, std::string name, std::string type, float weight) {
        try {
            std::string wkb = m_factory.create_multipolygon(area); // SQL uses Centroid
            if (name.empty()) name = "Unnamed " + type;
            m_work->exec_prepared("insert_choke", name, type, wkb, weight);
            check_commit();
        } catch (...) {}
    }

    void save_chokepoint_node(const osmium::Node& node, std::string name, std::string type) {
        try {
            std::string wkb = m_factory.create_point(node);
            if (name.empty()) name = "Unnamed " + type;
            m_work->exec_prepared("insert_choke", name, type, wkb, 0.0);
            check_commit();
        } catch (...) {}
    }

    void check_commit() {
        if (++m_counter >= BATCH_SIZE) {
            commit_batch();
            std::cout << "." << std::flush;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <planet-latest.osm.pbf>" << std::endl;
        return 1;
    }

    std::cout << "[TOTAL AWARENESS] Initializing Global Logistics Scanner..." << std::endl;
    std::cout << "[TARGET] " << argv[1] << std::endl;
    
    try {
        // Read Nodes (for pumps/cranes), Ways (Routes), Relations (Multipolygons)
        osmium::io::File input_file{argv[1]};
        osmium::io::Reader reader{input_file, osmium::osm_entity_bits::node | osmium::osm_entity_bits::way | osmium::osm_entity_bits::relation};

        MegaLogisticsHandler handler;
        
        osmium::area::Assembler::config_type assembler_config;
        osmium::area::MultipolygonManager<osmium::area::Assembler> mp_manager{assembler_config};
        
        std::cout << "[PHASE 1] Assembling Geometry (Relations)..." << std::endl;
        
        // FIX: Manual read loop to avoid missing header dependency
        while (osmium::memory::Buffer buffer = reader.read()) {
            osmium::apply(buffer, mp_manager);
        }
        reader.close();

        std::cout << "[PHASE 2] Ingesting The World..." << std::endl;
        osmium::io::Reader reader2{input_file};
        
        osmium::apply(reader2, mp_manager.handler([&handler](const osmium::memory::Buffer& area_buffer) {
            osmium::apply(area_buffer, handler);
        }));
        
        reader2.close();
        std::cout << "\n[SUCCESS] Planetary Ingest Complete." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL ERROR] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}