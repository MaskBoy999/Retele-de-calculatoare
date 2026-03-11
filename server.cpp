#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <set>
#include <map>
#include <thread>
#include <mutex>
#include <sqlite3.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;

#define SHARED_FILE "shared_data.txt"
#define DB_NAME "topology_history.db"
#define SERVER_PORT 9000

mutex topo_mutex;
string current_topo_json = "{}";

//functie generica pentru executat comenzi shell si returnat rezultatul
string execCommand(const char* cmd) {
    char buffer[128];
    string result = "";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    //curatam newline de la final
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

//scrie in siguranta datele in fisier folosind flag-ul 0/1
void safeWriteShared(const json& data) {
    ofstream f(SHARED_FILE, ios::out | ios::trunc);
    if (!f.is_open()) return;
    //punem flag 0 (ocupat) si scriem datele
    f << "0" << endl << data.dump(4) << endl;
    f.flush();
    //ne intoarcem la inceput si punem flag 1 (gata)
    f.seekp(0);
    f << "1";
    f.close();
}

//extragere isotric
string get_history_from_db() {
    sqlite3* db;
    sqlite3_open(DB_NAME, &db);
    sqlite3_stmt* stmt;
    //luam doar ultimele 10
    const char* sql = "SELECT ts, data FROM history ORDER BY id DESC LIMIT 10;";
    
    json history_array = json::array();
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            json entry;
            entry["timestamp"] = (const char*)sqlite3_column_text(stmt, 0);
            entry["topology"] = json::parse((const char*)sqlite3_column_text(stmt, 1));
            history_array.push_back(entry);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return history_array.dump();
}

string get_self_ip() {
    return execCommand("hostname -I | awk '{print $1}'");
    return "127.0.0.1";
}

//aflare gateway si alte informatii
string get_gateway_of_server() {
    return execCommand("ip route | grep default | awk '{print $3}'");
}

json get_server_info() {
    json j;
    char host[256]; 
    gethostname(host, 256);
    
    j["hostname"] = string(host);
    j["type"] = "central_server";
    j["os"] = "Linux (Server)";
    j["status"] = "online";
    j["gateway"] = get_gateway_of_server();
    j["cpu_cores"] = sysconf(_SC_NPROCESSORS_ONLN);
    
    ifstream mem_file("/proc/meminfo");
    string token;
    unsigned long long total = 0, avail = 0;
    while(mem_file >> token) {
        if(token == "MemTotal:") mem_file >> total;
        else if(token == "MemAvailable:") mem_file >> avail;
    }
    
    if (total > 0) {
        double total_gb = total / 1024.0 / 1024.0;
        double used_gb = (total - avail) / 1024.0 / 1024.0;
        stringstream ss;
        ss << fixed << setprecision(1) << used_gb << "/" << total_gb << " GB";
        j["ram_usage"] = ss.str();
    } else {
        j["ram_usage"] = "N/A";
    }
    
    return j;
}

string identify_device_by_mac(string mac) {
    if (mac.empty() || mac == "<incomplete>") return "Unknown";
    for (auto & c: mac) c = toupper(c);

    //astea care leam dedus acasa
    if (mac.find("88:D7:F6") == 0) return "Sagemcom Router";
    if (mac.find("C0:39:5A") == 0) return "TP-Link Device";
    if (mac.find("EC:F4:BB") == 0) return "Samsung Electronics"; // Producatorul cipului
    if (mac.find("E0:3F:49") == 0) return "ASUS Router";

    //macuri fixate
    if (mac.find("08:00:27") == 0) return "VirtualBox VM";
    if (mac.find("00:0C:29") == 0 || mac.find("00:50:56") == 0) return "VMware VM";
    if (mac.find("B8:27:EB") == 0 || mac.find("DC:A6:32") == 0) return "Raspberry Pi";

    return "Network Device"; //termen neutru in caz ca nu apartine niciunui dintre acestea
}

//sincronizare fisier

void wait_for_file_ready() {
    while (true) {
        ifstream f(SHARED_FILE);
        if (!f) return;
        string line; getline(f, line);
        if (line == "1") return;
        this_thread::sleep_for(chrono::milliseconds(50));
    }
}

void write_server_info_to_shared() {
    string my_ip = get_self_ip();
    wait_for_file_ready();

    json state = json::object();
    ifstream f_in(SHARED_FILE);
    if (f_in.is_open()) {
        string flag; getline(f_in, flag);
        try { f_in >> state; } catch(...) {}
        f_in.close();
    }

    //scriem informatiile despre serverul nostru in shared
    state[my_ip] = get_server_info();
    state[my_ip]["ip"] = my_ip;
    state[my_ip]["last_seen"] = (long long)time(nullptr);

    safeWriteShared(state);
}

//baza de date pentru istoric

void init_db() {
    sqlite3* db;
    sqlite3_open(DB_NAME, &db);
    //sterg istoric pentru a nu arata degeaba testele care leam facut acasa
    sqlite3_exec(db, "DROP TABLE IF EXISTS history;", 0, 0, 0);
    sqlite3_exec(db, "CREATE TABLE history (id INTEGER PRIMARY KEY AUTOINCREMENT, ts DATETIME DEFAULT (datetime('now','localtime')), data TEXT);", 0, 0, 0);
    sqlite3_close(db);
    cout << "[DATABASE] Istoric resetat." << endl;
}

void save_to_db(string data) {
    sqlite3* db;
    sqlite3_open(DB_NAME, &db);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO history (data) VALUES (?);", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, data.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

//logica la topologie

void topology_engine() {
    while (true) {
        write_server_info_to_shared();
        string my_ip = get_self_ip();
        string my_gw = get_gateway_of_server();

        //scan arp (practic afisare mini retea sub routerul sau routerul la care sunt conectat)
        map<string, string> arp_results; 
        FILE* pipe = popen("arp -n | grep ':'", "r"); 
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe)) {
            stringstream ss(buf);
            string ip_val, hw_type, mac_val;
            ss >> ip_val >> hw_type >> mac_val; 
            if (!ip_val.empty() && mac_val.find(":") != string::npos) {
                arp_results[ip_val] = mac_val;
            }
        }
        pclose(pipe);

        //citesc de la agent informatiile din shared
        json managed_data = json::object();
        wait_for_file_ready();
        ifstream f_in(SHARED_FILE);
        string flag; getline(f_in, flag);
        try { f_in >> managed_data; } catch(...) {}
        f_in.close();

        time_t now = time(nullptr);
        //iterator pentru stergere pe parcurs
        for (auto it = managed_data.begin(); it != managed_data.end(); ) {
            //asta e ca sa nu sterg serverul daca dau de el in managed data
            if (it.key() == my_ip) { ++it; continue; }

            long long last_val = it.value().value("last_seen", (long long)now);
            
            if (now - last_val > 30) {
                //arat ca deviceul a devenit inactiv(a inchis clientul), cu culoare rosie (wow)
                cout << "\033[1;31m[ALERTA] Agentul " << it.key() << " a devenit INACTIV. Se scoate din ierarhie...\033[0m" << endl;
                
                it = managed_data.erase(it); 
            } else {
                it.value()["status"] = "online";
                ++it;
            }
        }


        //colectare vecini distribuiti
        set<string> unique_ips;
        unique_ips.insert(my_ip);
        for(auto const& [ip, _] : arp_results) unique_ips.insert(ip);

        //mapa ca sa ne dam seama cine vede pe cine (irarhie)
        map<string, string> custom_gateways;

        for (auto& [agent_ip, agent_info] : managed_data.items()) {
            unique_ips.insert(agent_ip); //ipul din agent

            //lista arp de la agent
            if (agent_info.contains("neighbors") && agent_info["neighbors"].is_array()) {
                for (auto& neighbor : agent_info["neighbors"]) {
                    string n_ip = neighbor.value("ip", "");
                    string n_mac = neighbor.value("mac", "");
                    
                    if (!n_ip.empty() && n_ip != agent_ip) {
                        unique_ips.insert(n_ip);
                        //daca serverul nu a vazut macul dar agentul da il salvam
                        if (arp_results.find(n_ip) == arp_results.end()) {
                            arp_results[n_ip] = n_mac;
                        }

                        if (arp_results.find(n_ip) == arp_results.end()) {
                            custom_gateways[n_ip] = agent_ip;
                        }
                    }
                }
            }
        }

        //constructie lista noduri
        json nodes = json::array();
        for (const string& ip : unique_ips) {
            json node;
            node["ip"] = ip;

            if (managed_data.contains(ip)) {
                node = managed_data[ip];
            } else {
                string mac = arp_results.count(ip) ? arp_results[ip] : "";
                node["mac"] = mac;
                node["hostname"] = "Unknown";
                node["status"] = "detected";
                node["type"] = identify_device_by_mac(mac);
                
                //ierarhie care leaga copii de parinti
                if (custom_gateways.count(ip)) {
                    node["gateway"] = custom_gateways[ip]; //legat copil de parinte in harta
                } else if (ip != my_gw) {
                    node["gateway"] = my_gw; //leg gateway de routerul principal
                } else {
                    node["type"] = "Main Router";
                    node["gateway"] = "external";
                }
            }
            nodes.push_back(node);
        }

        //salvare tot in json
        json final_topo;
        final_topo["nodes"] = nodes;
        {
            lock_guard<mutex> lock(topo_mutex);
            current_topo_json = final_topo.dump();
        }
        save_to_db(current_topo_json);
        this_thread::sleep_for(chrono::seconds(30)); 
    }
}

//comunicare cu cleintul
void handle_client(int sock) {
    char buffer[1024];
    int n = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        string cmd(buffer);
        string resp;
        
        if (cmd == "GET_TOPO") {
            lock_guard<mutex> lock(topo_mutex);
            resp = current_topo_json;
        } else if (cmd == "GET_HISTORY") {
            resp = get_history_from_db(); 
        }
        send(sock, resp.c_str(), resp.size(), 0);
    }
    close(sock);
}

int main() {
    init_db();

    {
        ofstream f_init(SHARED_FILE, ios::trunc);
        f_init << "1" << endl << "{}" << endl;
        f_init.close();
    }

    thread(topology_engine).detach();

    int s_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1; setsockopt(s_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(s_fd, (sockaddr*)&addr, sizeof(addr));
    listen(s_fd, 10);

    cout << "[SERVER] Ready on port " << SERVER_PORT << endl;
    while (true) {
        int c_sock = accept(s_fd, nullptr, nullptr);
        if (c_sock >= 0) thread(handle_client, c_sock).detach();
    }
    return 0;
}