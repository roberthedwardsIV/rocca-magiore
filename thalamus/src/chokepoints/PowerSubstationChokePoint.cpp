#include "PowerSubstationChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

PowerSubstationChokePoint::PowerSubstationChokePoint(int id, std::string name, double lat, double lon, 
                                                     float voltage, float capacity_mva)
    : BaseChokePoint(id, name, "power_substation", lat, lon), 
      max_voltage_kv(voltage), 
      max_load_mva(capacity_mva) {
    
    // Valeurs par défaut (État nominal)
    current_load_mva = capacity_mva * 0.5f; // 50% de charge
    transformer_temp_c = 65.0f;             // Température de fonctionnement normale
    is_tripped = false;
    is_maintenance = false;
}

void PowerSubstationChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Signal SCADA (Données du réseau en temps réel)
    if (category == "scada" || category == "grid_status") {
        if (sig.contains("load_mva")) {
            current_load_mva = sig["load_mva"].get<float>();
        }
        if (sig.contains("temp_c")) {
            transformer_temp_c = sig["temp_c"].get<float>();
        }
        // Détection de déclenchement (Circuit Breaker Trip)
        if (sig.contains("breaker_status")) {
            std::string status = sig["breaker_status"];
            is_tripped = (status == "OPEN" || status == "TRIPPED");
        }
    }
    
    // 2. Maintenance Planifiée
    else if (category == "maintenance") {
        is_maintenance = sig.value("active", false);
    }

    // 3. Intégrité Physique (Sabotage / Incendie)
    else if (category == "integrity" || category == "security") {
        float damage = sig.value("severity", 0.0f);
        if (damage > 0.5f) is_tripped = true; // Arrêt d'urgence si dégâts majeurs
    }

    last_update = sig.value("timestamp", 0LL);
}

float PowerSubstationChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Coupure Totale (Blackout)
    // Si le disjoncteur est ouvert (tripped) ou en maintenance lourde, rien ne passe.
    if (is_tripped) return 0.0f;
    if (is_maintenance) return 0.0f; // On suppose ici aucune redondance (N-0) pour ce nœud spécifique

    // 2. Contrainte Thermique (Derating)
    // Les transformateurs perdent de l'efficacité et risquent la panne au-delà de 90°C.
    // 90°C -> 100% capacité
    // 110°C -> 0% capacité (Arrêt de sécurité imminent)
    if (transformer_temp_c > 110.0f) {
        return 0.0f; // Sécurité thermique déclenchée
    }
    else if (transformer_temp_c > 90.0f) {
        // Déclassement linéaire entre 90C et 110C
        float thermal_factor = 1.0f - ((transformer_temp_c - 90.0f) / 20.0f);
        return thermal_factor;
    }

    // 3. Surcharge de Capacité (Overload)
    // Si la demande dépasse la capacité nominale (MVA), la tension s'effondre (Brownout).
    if (current_load_mva > max_load_mva) {
        // Le réseau ne peut pas fournir plus que max_load. 
        // Le débit effectif est limité par la physique.
        return max_load_mva / current_load_mva; 
    }

    return 1.0f; // Fonctionnement nominal
}

json PowerSubstationChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"load_mva", current_load_mva},
            {"temp_c", transformer_temp_c},
            {"tripped", is_tripped},
            {"maintenance", is_maintenance}
        }},
        {"capacity", {
            {"voltage_kv", max_voltage_kv},
            {"max_mva", max_load_mva}
        }},
        {"throughput_mod", const_cast<PowerSubstationChokePoint*>(this)->calculate_throughput_modifier()}
    };
}