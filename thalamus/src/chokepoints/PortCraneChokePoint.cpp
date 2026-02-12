#include "PortCraneChokePoint.hpp"
#include <iostream>
#include <algorithm>

PortCraneChokePoint::PortCraneChokePoint(int id, std::string name, double lat, double lon, 
                                         float swl, float outreach)
    : BaseChokePoint(id, name, "port_crane", lat, lon), 
      max_swl_tons(swl), 
      outreach_meters(outreach) {
    
    // Valeurs par défaut
    wind_limit_kmh = 72.0f;     // Seuil de sécurité standard (approx 40 knots)
    current_wind_speed = 0.0f;
    hoist_efficiency = 1.0f;
    is_operational = true;
    is_in_use = false;
}

void PortCraneChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Météo (Vitesse du vent au sommet de la flèche)
    if (category == "weather") {
        if (sig.contains("wind_speed")) {
            current_wind_speed = sig["wind_speed"].get<float>();
        }
    }
    
    // 2. État Mécanique (Moteurs de levage / Électronique)
    else if (category == "mechanical" || category == "integrity") {
        float damage = sig.value("severity", 0.0f);
        hoist_efficiency -= damage;
        if (hoist_efficiency < 0.0f) hoist_efficiency = 0.0f;
        
        // Si l'efficacité tombe trop bas, la grue est hors service
        if (hoist_efficiency < 0.4f) is_operational = false;
    }

    // 3. Opérations Portuaires
    else if (category == "ops") {
        is_operational = sig.value("operational", true);
        is_in_use = sig.value("in_use", false);
    }

    last_update = sig.value("timestamp", 0LL);
}

float PortCraneChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Arrêt Critique (Panne ou Sécurité)
    if (!is_operational) return 0.0f;

    // 2. Sécurité Vent (Wind-Off)
    // Si le vent dépasse la limite, la grue est mise en sécurité (stowed).
    if (current_wind_speed > wind_limit_kmh) {
        return 0.0f; 
    }

    // 3. Ralentissement lié au vent (Approche de la limite)
    // Entre 50 km/h et 72 km/h, on ralentit les mouvements pour garder le contrôle du container.
    float wind_factor = 1.0f;
    if (current_wind_speed > 50.0f) {
        wind_factor = 1.0f - ((current_wind_speed - 50.0f) / (wind_limit_kmh - 50.0f) * 0.7f);
    }

    // 4. Efficacité Mécanique
    // Une grue fatiguée bouge moins de containers par heure (TEU/hr).
    return hoist_efficiency * wind_factor;
}

json PortCraneChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"operational", is_operational},
            {"efficiency", hoist_efficiency},
            {"wind_speed", current_wind_speed},
            {"in_use", is_in_use}
        }},
        {"specs", {
            {"max_lift_tons", max_swl_tons},
            {"outreach_m", outreach_meters}
        }},
        {"throughput_mod", const_cast<PortCraneChokePoint*>(this)->calculate_throughput_modifier()}
    };
}