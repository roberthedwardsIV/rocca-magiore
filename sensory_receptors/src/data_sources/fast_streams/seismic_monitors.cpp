#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <hiredis/hiredis.h>
#include <thread>
#include <vector>
#include <deque>
#include <sstream>
#include <pqxx/pqxx>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <algorithm>
#include <libmseed.h> 

using namespace pqxx;

/**
 * @brief Streams earthquake station streams and publishes waveforms + metadata to socket + redis.
          Sends the raw data and the station_id (IU.ANMO) from which it was detected. Only spikes
          when STA/LTA > 5 to inicate something is happening. Seismic_brain then monitors for this
          data to run through our neural network.
 */


// ------------------------------------------------------------------------------------------------------------
// Name: trim() | Type: FUNCTION-UTIL | Param: string | Output: string w/ lead/trail spaces rm
// Purpose: clean raw seismic data for proper processing
std::string trim(const std::string &s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) start++; 
    auto end = s.end(); 
    if (start == end) return ""; 
    do { end--; } while (std::distance(start, end) > 0 && std::isspace(*end)); 
    return std::string(start, end + 1);
}

// Name: get_start_time_string() | Type: FUNCTION-UTIL | Param: none | Output: string(current time - 4 hours)
// Purpose: avoid missing the current station streams by setting 4 hour pre-connection window
std::string get_start_time_string() {
    auto now = std::chrono::system_clock::now();
    auto earlier = now - std::chrono::hours(4);
    std::time_t tt = std::chrono::system_clock::to_time_t(earlier);
    std::tm *ptm = std::gmtime(&tt);
    char buffer[32];
    std::strftime(buffer, 32, "%Y,%m,%d,%H,%M,%S", ptm);
    return std::string(buffer);
}


// ------------------------------------------------------------------------------------------------------------
// Name: TargetStation | Type: STRUCT | Purpose: structure data from db for use in IRIS search
struct TargetStation {
    std::string network;
    std::string station;
    std::string selector;
    std::string location; 
    std::string latitude;
    std::string longitude;
};

// Name: StationState | Type: STRUCT | Purpose: structure data for tracking of energy spikes
struct StationState {
    std::deque<float> buffer; 
    double sta = 0.0;         // Short-term average
    double lta = 0.0;         // Long-term average
    const int window_size = 1000;
};


// ------------------------------------------------------------------------------------------------------------
//Name: station_map | Type: MAP | Purpose: structure map of all stations for easy querying
std::map<std::string, TargetStation> station_map;

//Name: live_states | Type: MAP | Purpose: structure memory of STA/LTA for each station
std::map<std::string, StationState> live_states;


// ------------------------------------------------------------------------------------------------------------
// CONFIGURATION VARIABLES
const char *SERVER_HOST = "rtserve.iris.washington.edu";
const int SERVER_PORT = 18000;
const int BUFFER_SIZE = 4096;
const int SEEDLINK_PACKET_SIZE = 520;
const char *UDS_PATH = "/shared/seismic_brain.sock";



// ------------------------------------------------------------------------------------------------------------
/**
 * Name: send_to_brain
 * Params: 
        - key: string for station's ID (eg. IU.ANMO)
        - samples: list of raw seismic readings (double-ended queue for fast editing)
 * Outputs: none
 * Purpose: sends the station ID and the vectorized waveform sample data to socket so brain can listen
 * Called By: process_samples
 * Calls: none
*/
void send_to_brain(const std::string& key, const std::deque<float>& samples) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, UDS_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        char header[256];
        int header_len = snprintf(header, sizeof(header), "STA:%s|", key.c_str());
        send(sock, header, header_len, 0);
        
        std::vector<float> data(samples.begin(), samples.end());
        send(sock, data.data(), data.size() * sizeof(float), 0);
    } //else {
        //fprintf(stderr, "\n[SEISMIC](SOCKET ERROR) %s: %s\n", key.c_str(), strerror(errno));
        //fflush(stderr);
    //}
    close(sock);
}



/**
 * Name: process_samples
 * Params: 
        - key: string for station's ID (eg. IU.ANMO)
        - samples: int array of raw seismic readings
        - count: int number of samples in the array
 * Outputs: none
 * Purpose: Takes a raw array of datapoints from a particular station and converts 
 *          raw ints to floats, creates deque to keep most recent 1000 samples, 
 *          calculates running STA/LTAs, and triggers when STA is 5 times stronger
 *          than LTA (indicating a spike in seismic activity).
 * Called By: run_seismic_stream
 * Calls: send_to_brain
*/
void process_samples(const std::string& key, int32_t* samples, int count) {
    auto& state = live_states[key];
    
    for (int i = 0; i < count; ++i) {
        float val = static_cast<float>(samples[i]);
        state.buffer.push_back(val);
        if (state.buffer.size() > state.window_size) state.buffer.pop_front();

        state.sta = (0.1 * std::abs(val)) + (0.9 * state.sta);
        state.lta = (0.01 * std::abs(val)) + (0.99 * state.lta);

        if (state.buffer.size() == state.window_size && state.lta > 0) {
            if ((state.sta / state.lta) > 5.0) { 
                send_to_brain(key, state.buffer);
            }
        }
    }
}



/**
 * Name: fetch_stations_from_db
 * Params: none
 * Outputs: 
 *      - targets: vector of structs containing network (eg. IU), station (eg. ANMO) and frequency (eg. BH?)
 *                 with NET_STA (eg. IU_ANMO) as they key for each struct in the vector.
 * Purpose: fetches the stations from our database, saves them to a vector and maps them to our station_map.
 * Called By: run_seismic_stream
 * Calls: none
*/
std::vector<TargetStation> fetch_stations_from_db() {
    std::vector<TargetStation> targets;
    std::string conn_str = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";
    try {
        connection C(conn_str);
        work W(C);
        result R = W.exec("SELECT network, station, frequency FROM earthquake_stations WHERE active = TRUE");
        for (auto row : R) {
            TargetStation ts;
            ts.network = row["network"].c_str();
            ts.station = row["station"].c_str();
            ts.selector = row["frequency"].is_null() ? "BH?" : row["frequency"].c_str();
            targets.push_back(ts);
            std::string key = ts.network + "_" + ts.station;
            station_map[key] = ts;
        }
    } catch (const std::exception &e) {
        std::cerr << "[SEISMIC](DB Error) " << e.what() << std::endl;
    }
    return targets;
}



/**
 * Name: run_seismic_stream
 * Params: none
 * Outputs: none
 * Purpose: Obtains target stations, connects to redis, connects to IRIS Seedlink and listens to streams
            for each target station continously. When data get recieved, it's processed and published to 
            redis for the python brain to analyze.
 * Called By: main()
 * Calls: fetch_stations_from_db, get_start_time_string, trim, process_samples 
*/
void run_seismic_stream() {
    std::vector<TargetStation> targets = fetch_stations_from_db();
    if (targets.empty()) return;

    struct hostent *server = gethostbyname(SERVER_HOST);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(SERVER_PORT);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "[SEISMIC](CRITICAL) IRIS Connection Failed" << std::endl;
        return;
    }

    std::stringstream ss;
    for (const auto &t : targets) {
        ss << "STATION " << t.station << " " << t.network << "\r\n";
        ss << "SELECT " << t.selector << "\r\n";
    }
    std::string time_cmd = "TIME " + get_start_time_string() + "\r\n";
    ss << time_cmd;
    ss << "DATA\r\n";

    std::string handshake = ss.str();
    write(sockfd, handshake.c_str(), handshake.length());
    std::cout << "[SEISMIC](DEBUG) Handshake sent. Requesting backlog..." << std::endl;

    uint8_t buf[BUFFER_SIZE];
    int buf_len = 0;

    while (true) {
        ssize_t n = read(sockfd, buf + buf_len, BUFFER_SIZE - buf_len);
        if (n <= 0) break;
        buf_len += n;
        while (buf_len >= SEEDLINK_PACKET_SIZE) {
            if (buf[0] == 'S' && buf[1] == 'L') {
                MSRecord *msr = NULL;
                int retcode = msr_unpack((char*)(buf + 8), 512, &msr, 1, 1);
                if (retcode == MS_NOERROR) {
                    std::string net = trim(msr->network);
                    std::string sta = trim(msr->station);
                    std::string key = net + "_" + sta;
                    if (msr->numsamples > 0) {
                        int32_t* samples = (int32_t*)msr->datasamples;
                        process_samples(key, samples, msr->numsamples);
                    }
                }
                if (msr) msr_free(&msr);
                memmove(buf, buf + SEEDLINK_PACKET_SIZE, buf_len - SEEDLINK_PACKET_SIZE);
                buf_len -= SEEDLINK_PACKET_SIZE;
                std::cout << "." << std::flush;
                continue;
            }
            memmove(buf, buf + 1, --buf_len);
        }
    }
    close(sockfd);
}


// ------------------------------------------------------------------------------------------------------------
// MAIN FUNCTION
int main() {
    std::cout << "[SEISMIC] ROCCO-MAGGIORE SEISMIC CORE: ONLINE" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    run_seismic_stream();
    return 0;
}