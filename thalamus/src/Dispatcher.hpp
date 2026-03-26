#ifndef DISPATCHER_HPP
#define DISPATCHER_HPP

#include <nlohmann/json.hpp>

class Dispatcher {
public:
    static void route_signal(const nlohmann::json& sig);
    
private:
    static double calculate_local_mmi(double epicenter_mag, double epicenter_mmi, double distance_km, double depth_km);
};

#endif