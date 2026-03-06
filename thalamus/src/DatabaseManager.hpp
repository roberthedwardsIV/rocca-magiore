#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <string>
#include <vector>

// Pre-calculated sensitivity matrix parameters from backtester
struct MatrixParam {
    std::string ticker;            
    double beta;                    
    int lag_minutes;
    double confidence;
    long long last_backtested_ts; 
};

// Spacial relationship between events + assets
struct AssetDistance {
    int asset_id;
    double distance_km;
};

// Helper methods (DB initialization, parameter fetching and network fetching)
bool init_database();
std::vector<MatrixParam> get_matrix_parameters(int asset_id, const std::string& signal_category);
std::vector<int> get_downstream_assets(int asset_id);
std::vector<AssetDistance> get_assets_near_location(double lat, double lon, double radius_km);

#endif