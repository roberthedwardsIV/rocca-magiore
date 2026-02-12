#include "PumpingStationChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

PumpingStationChokePoint::PumpingStationChokePoint(int id, std::string name, double lat, double lon, 
                                                   float max_psi, float max_flow)
    : BaseChokePoint(id, name, "pumping_station", lat, lon), 
      max_pressure_psi(max_psi), 
      max_flow_rate_bpd(max_flow) {
    
    // Valeurs par défaut
    current_pressure_psi = max_psi * 0.8f; // 80% pression nominale
    total_pumps = 4;        // Standard pour une station moyenne
    active_pumps = 4;       // Toutes pompes actives
    is_leaking = false;
}

void PumpingStationChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Télémétrie (Pression / Débit)
    if (category == "telemetry" || category == "scada") {
        if (sig.contains("pressure_psi")) {
            current_pressure_psi = sig["pressure_psi"].get<float>();
        }
        if (sig.contains("active_pumps")) {
            active_pumps = sig["active_pumps"].get<int>();
        }
    }
    
    // 2. Intégrité (Détection de fuite acoustique / Chute de pression brutale)
    else if (category == "integrity" || category == "leak_detection") {
        // "severity" > 0.0 indique une fuite confirmée
        if (sig.value("severity", 0.0f) > 0.1f) {
            is_leaking = true;
        } else {
            is_leaking = false; // Réparation confirmée
        }
    }

    // 3. Maintenance
    else if (category == "maintenance") {
        // Si maintenance, on arrête souvent une ou plusieurs pompes
        if (sig.value("active", false)) {
            active_pumps = std::max(0, active_pumps - 1);
        }
    }

    last_update = sig.value("timestamp", 0LL);
}

float PumpingStationChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Arrêt d'Urgence (Fuite)
    // Si le pipeline fuit, on ferme les vannes immédiatement.
    if (is_leaking) return 0.0f;

    // 2. Panne de Pompe (Réduction mécanique)
    // Le débit est directement lié à la capacité de pompage.
    float pump_efficiency = (float)active_pumps / (float)total_pumps;

    // 3. Pression Insuffisante (Problème en amont)
    // Si la pression d'entrée est trop basse (ex: < 10% du max), 
    // les pompes cavitent et doivent être ralenties.
    float pressure_factor = 1.0f;
    if (current_pressure_psi < (max_pressure_psi * 0.1f)) {
        pressure_factor = 0.5f; 
    }

    return pump_efficiency * pressure_factor;
}

json PumpingStationChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"pressure_psi", current_pressure_psi},
            {"active_pumps", active_pumps},
            {"leaking", is_leaking}
        }},
        {"capacity", {
            {"max_psi", max_pressure_psi},
            {"max_flow_bpd", max_flow_rate_bpd}
        }},
        {"throughput_mod", const_cast<PumpingStationChokePoint*>(this)->calculate_throughput_modifier()}
    };
}