#include "Dispatcher.hpp"
#include "SignalEngine.hpp"
#include "DatabaseManager.hpp"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>

void broadcast_log(const std::string& message) {
    // 1. Standard terminal output
    std::cout << message << std::endl;
    
    // 2. Broadcast to UI
    static redisContext* redis_ctx = nullptr;
    
    // Lazy connect / reconnect logic
    if (redis_ctx == nullptr || redis_ctx->err) {
        if (redis_ctx) {
            redisFree(redis_ctx);
        }
        redis_ctx = redisConnect("corpus_callosum", 6379);
        if (redis_ctx == nullptr || redis_ctx->err) {
            std::cerr << "[Dispatcher.cpp] Redis connection error. Log dropped." << std::endl;
            return;
        }
    }

    nlohmann::json log_json;
    log_json["container"] = "thalamus";
    log_json["log"] = message;
    
    std::string payload = log_json.dump();
    
    // Execute the PUBLISH command
    redisReply* reply = (redisReply*)redisCommand(redis_ctx, "PUBLISH system_logs %s", payload.c_str());
    
    if (reply == nullptr) {
        std::cerr << "[Dispatcher.cpp] Redis publish failed. Resetting connection." << std::endl;
        redisFree(redis_ctx);
        redis_ctx = nullptr;
    } else {
        freeReplyObject(reply); // Prevent memory leaks
    }
}

double Dispatcher::calculate_local_mmi(double epicenter_mag, double epicenter_mmi, double distance_km, double depth_km) {
    double I_0 = epicenter_mmi;
    
    // if we only have magnitude estimate Epicenter MMI
    if (I_0 <= 0.0 && epicenter_mag > 0) {
        I_0 = (1.5 * epicenter_mag) - 1.5; 
    }
    
    if (I_0 <= 0.0) return 0.0;

    double r_hypo = std::sqrt(std::pow(distance_km, 2) + std::pow(depth_km, 2));

    double local_mmi = I_0 - 2.7 * std::log10((r_hypo / 10.0) + 1.0);
    
    return std::max(1.0, std::min(12.0, local_mmi));
}

void Dispatcher::route_signal(const nlohmann::json& sig) {
    std::string type = sig.value("entity_type", "unknown");

    if (type == "earthquake" || type == "wildfire") {
        if (!sig.contains("data")) return;
        
        double lat = sig["data"].value("lat", 999.0);
        double lon = sig["data"].value("lon", 999.0);
        
        if (lat == 999.0 || lon == 999.0) return;

        // --- WILDFIRE ROUTING ---
        if (type == "wildfire") {
            double frp = sig["data"].value("frp", 0.0);
            if (frp <= 0) return;

            // Strict 50km radius based on backtest assumptions
            std::vector<AssetDistance> hit_assets = get_assets_near_location(lat, lon, 50.0);
            
            for (const auto& asset : hit_assets) {
                std::cout << "[Dispatcher.cpp] WILDFIRE hit Asset " << asset.asset_id 
                          << " | Dist: " << asset.distance_km << "km. Routing..." << std::endl;
                          
                // For fires intensity = raw FRP
                SignalEngine::evaluate_physical_shock(asset.asset_id, "WILDFIRE", frp);

                std::ostringstream log_msg;
                log_msg << "[Dispatcher.cpp] WILDFIRE hit Asset " << asset.asset_id 
                        << " | Dist: " << asset.distance_km << "km. Routing...";
                
                broadcast_log(log_msg.str());
            }
        } 
        
        // --- EARTHQUAKE ROUTING ---
        else if (type == "earthquake") {
            double mag = sig["data"].value("mag", 0.0);
            double mmi = sig["data"].value("mmi", 0.0); 
            double depth = sig["data"].value("depth", 10.0);
            
            if (mag <= 0 && mmi <= 0) return;

            // pull everything within 300km then filter based on attenuation
            std::vector<AssetDistance> hit_assets = get_assets_near_location(lat, lon, 300.0);
            
            for (const auto& asset : hit_assets) {
                double local_mmi = calculate_local_mmi(mag, mmi, asset.distance_km, depth);
                
                // Only trigger the matrix if the asset actually felt it (MMI >= 1.5)
                if (local_mmi >= 1.5) {
                    std::cout << "[Dispatcher.cpp] SEISMIC hit Asset " << asset.asset_id 
                        << " | Dist: " << asset.distance_km 
                        << "km | Depth: " << depth << "km"
                        << " | Local MMI: " << local_mmi << ". Routing..." << std::endl;
                
            SignalEngine::evaluate_physical_shock(asset.asset_id, "SEISMIC", local_mmi);
            std::ostringstream log_msg;
                    log_msg << "[Dispatcher.cpp] SEISMIC hit Asset " << asset.asset_id 
                            << " | Dist: " << asset.distance_km 
                            << "km | Depth: " << depth << "km"
                            << " | Local MMI: " << local_mmi << ". Routing...";
                    
                    broadcast_log(log_msg.str());
        }
        }
    }
}
}