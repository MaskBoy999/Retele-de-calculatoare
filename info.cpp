#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;

#define AGENT_PORT 9001

//gateway pentru a stii parintele in topologie
string get_gateway() {
    ifstream f("/proc/net/route");
    string line;
    if (!f.is_open()) return "unknown";
    getline(f, line); //sare peste prima linie inutile
    while (getline(f, line)) {
        char iface[16];
        unsigned long dest, gw;
        if (sscanf(line.c_str(), "%s\t%lX\t%lX", iface, &dest, &gw) >= 3) {
            if (dest == 0 && gw != 0) {
                struct in_addr addr;
                addr.s_addr = gw;
                return string(inet_ntoa(addr));
            }
        }
    }
    return "unknown";
}

json get_local_neighbors() {
    json neighbors = json::array();
    FILE* pipe = popen("arp -n | grep ':' | awk '{print $1, $3}'", "r");
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        char ip[64], mac[64];
        if (sscanf(buf, "%s %s", ip, mac) == 2) {
            json n;
            n["ip"] = ip;
            n["mac"] = mac;
            neighbors.push_back(n);
        }
    }
    pclose(pipe);
    return neighbors;
}

//sistem de operare
string get_os_name() {
    ifstream f("/etc/os-release");
    string line;
    if (!f.is_open()) return "Linux";
    while (getline(f, line)) {
        if (line.find("PRETTY_NAME=") == 0) {
            string os = line.substr(13);
            if (!os.empty() && os.back() == '"') os.pop_back();
            return os;
        }
    }
    return "Linux";
}

//informatii hardware
double get_cpu_load() {
    double load = 0.0;
    ifstream f("/proc/loadavg");
    if (f.is_open()) f >> load;
    return load;
}

string get_ram_info() {
    ifstream f("/proc/meminfo");
    string token;
    unsigned long long total = 0, available = 0;

    while (f >> token) {
        if (token == "MemTotal:") f >> total;
        else if (token == "MemAvailable:") f >> available;
    }

    if (total == 0) return "N/A";

    //convertire din kb in gb
    double total_gb = total / 1024.0 / 1024.0;
    double used_gb = (total - available) / 1024.0 / 1024.0;

    stringstream ss;
    ss << fixed << setprecision(1) << used_gb << "/" << total_gb << " GB";
    return ss.str();
}

void send_to_agent(const string& agent_ip) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct timeval tv;
    tv.tv_sec = 2; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(AGENT_PORT);
    if (inet_pton(AF_INET, agent_ip.c_str(), &addr.sin_addr) <= 0) {
        close(sock); return;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        json j;
        char host[256]; gethostname(host, 256);
        
        j["hostname"] = string(host);
        j["gateway"] = get_gateway();
        j["os"] = get_os_name();
        j["cpu_cores"] = sysconf(_SC_NPROCESSORS_ONLN);
        
        j["ram_usage"] = get_ram_info(); 
        
        j["load"] = get_cpu_load();
        j["type"] = "managed_device";
        j["neighbors"] = get_local_neighbors();
        j["last_seen"] = time(nullptr); 

        string p = j.dump();
        send(sock, p.c_str(), p.size(), 0);
    }
    close(sock); //inchid ca sa nu ramana fisiere zombie deoarece poate atinge o anumita limita
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Utilizare: " << argv[0] << " <IP_AGENT>" << endl;
        return 1;
    }
    string ip = argv[1];
    while (true) {
        send_to_agent(ip);
        this_thread::sleep_for(chrono::seconds(10));
    }
    return 0;
}