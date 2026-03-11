#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iomanip>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;

#define SERVER_PORT 9000

//functie helper pentru a extrage ram-ul total dintr-un string formatat "U/T GB"
double extractTotalRam(const string& ramStr) {
    size_t slashPos = ramStr.find('/');
    if (slashPos != string::npos) {
        return atof(ramStr.substr(slashPos + 1).c_str());
    }
    return atof(ramStr.c_str());
}

string sendRequest(const string& ip, const string& cmd) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) return "ERR_SOCKET";

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    if(inet_pton(AF_INET, ip.c_str(), &server.sin_addr) <= 0) return "ERR_IP";

    if(connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        close(sock);
        return "ERR_CONNECT";
    }

    send(sock, cmd.c_str(), cmd.size(), 0);

    string response;
    char buffer[8192];
    int n;
    while((n = recv(sock, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[n] = '\0';
        response += buffer;
    }
    close(sock);
    return response;
}

//functie recursiva pentru a desena arborele ierarhic
void renderTree(const string& parent_ip, map<string, vector<json>>& tree, string prefix) {
    if (tree.find(parent_ip) == tree.end()) return;

    auto& children = tree[parent_ip];
    for (size_t i = 0; i < children.size(); ++i) {
        bool isLast = (i == children.size() - 1);
        
        //extragem valorile din JSON
        string ip = children[i].value("ip", "??");
        string hostname = children[i].value("hostname", "unknown");
        string type = children[i].value("type", "Generic Device");

        //logica de nume, daca hostname este unknown, folosim tipul identificat prin MAC de server
        string displayName = hostname;
        if (hostname == "unknown" || hostname == "Unknown" || hostname == "Dispozitiv_ARP") {
            displayName = type;
        }

        cout << prefix << (isLast ? "└── " : "├── ") 
             << ip << " [" << displayName << "]";
        
        //detalii suplimentare pentru nodurile cu agent(gen alea care chiar fac conexiunea prin info)
        if (children[i].contains("cpu_cores")) {
            cout << " (CPU:" << children[i]["cpu_cores"] << " Cores)";
        }
        cout << endl;

        //apel recursiv pentru copiii acestui nod
        renderTree(ip, tree, prefix + (isLast ? "    " : "│   "));
    }
}


//pentru ranking
void displayRanking(const string& json_str) {
    try {
        auto j = json::parse(json_str);
        
        if (!j.contains("nodes") || j["nodes"].empty()) {
            cout << "\n[!] Nu exista date pentru clasament.\n";
            return;
        }

        struct NodeRank {
            string name;
            double score;
            string ram;
            int cores;
            string os;
        };

        vector<NodeRank> rankingList;

        for (auto& n : j["nodes"]) {
            //filtrare identica cu tabelul de informatii
            if (n.contains("cpu_cores") || n.value("type", "") == "central_server") {
                
                string ramStr = n.value("ram_usage", "N/A");
                int cores = n.value("cpu_cores", 1);
                if (cores <= 0) cores = 1;

                double totalRam = extractTotalRam(n.value("ram_usage", "0/0 GB"));

                if (totalRam <= 0) continue;

                //(ram * cores) / 10 pentru a evidentia puterea bruta
                double score = (totalRam * (double)cores) / 10.0;

                rankingList.push_back({
                    n.value("hostname", "Unknown"),
                    score,
                    ramStr,
                    cores,
                    n.value("os", "Linux")
                });
            }
        }

        if (rankingList.empty()) return;

        sort(rankingList.begin(), rankingList.end(), [](const NodeRank& a, const NodeRank& b) {
            return a.score > b.score;
        });

        //afisare tabel
        cout << "\n========== CLASAMENT PUTERE BRUTA (Power Score) ==========\n";
        cout << string(95, '-') << "\n";
        cout << left << setw(8)  << "RANK" 
             << setw(25) << "HOSTNAME" 
             << setw(12) << "SCOR" 
             << setw(18) << "RAM (U/T)" 
             << setw(10) << "CPU" 
             << "OS" << endl;
        cout << string(95, '-') << "\n";

        for (size_t i = 0; i < rankingList.size(); ++i) {
            string rankIcon = "";
            if (i == 0) rankIcon = " 🟡";
            else if (i == 1) rankIcon = " ⚪";
            else if (i == 2) rankIcon = " 🟤";

            //afisare manuala pentru a compensa spatiul ocupat de iconite
            cout << left << i + 1 << rankIcon;
            
            //padding pentru a alinia coloana hostname indiferent de rank
            if (i < 9) cout << string(5, ' ');
            else cout << string(4, ' '); 

            cout << left << setw(25) << rankingList[i].name.substr(0, 24)
                 << setw(12) << fixed << setprecision(2) << rankingList[i].score
                 << setw(18) << rankingList[i].ram
                 << setw(10) << (to_string(rankingList[i].cores) + " c")
                 << rankingList[i].os << endl;
        }
        cout << string(95, '-') << endl;

    } catch (exception& e) {
        //evitam inchiderea programului la erori de parsare
        cout << "[!] Eroare: " << e.what() << endl;
    }
}

void displayTopology(const string& json_str) {
    try {
        auto j = json::parse(json_str);
        map<string, vector<json>> tree_map;
        vector<json> roots;
        map<string, bool> has_parent;

        //constructie arbore
        for (auto& n : j["nodes"]) {
            string ip = n.value("ip", "");
            string gw = n.value("gateway", "");
            bool parent_found = false;
            for (auto& search : j["nodes"]) {
                if (search["ip"] == gw && gw != ip) {
                    parent_found = true;
                    break;
                }
            }
            if (parent_found) {
                tree_map[gw].push_back(n);
                has_parent[ip] = true;
            }
        }
        for (auto& n : j["nodes"]) {
            if (!has_parent[n["ip"]]) roots.push_back(n);
        }

        //afisarea constructiei
        cout << "\n========== IERARHIA REȚELEI ==========\n";
        for (auto& r : roots) {
            string hostname = r.value("hostname", "Unknown");
            string type = r.value("type", "Generic Device");
            string displayName = (hostname == "Unknown" || hostname == "unknown") ? type : hostname;
            
            cout << "\n● " << r.value("ip", "??") << " [" << displayName << "]" << endl;
            renderTree(r["ip"], tree_map, "");
        }

        //tabela de informatii (mai bine separat ca se umple copacul altfell)
        cout << "\n" << string(60, '-') << "\n";
        cout << left << setw(16) << "IP" 
             << setw(20) << "HOSTNAME" 
             << setw(10) << "CPU" 
             << setw(10) << "RAM" 
             << "OS/STATUS" << endl;
        cout << string(60, '-') << "\n";

        for (auto& n : j["nodes"]) {
            //filtram sa afiseze in tabela de inforamtii doar pe cele care sau conectat prin info(nu ne dau prea multe informatii alea care nu se conecteaza deci ar fi degeabva)
            if (n.contains("cpu_cores") || n.value("type", "") == "central_server") {
                string status_icon = (n.value("status", "") == "online") ? "🟢" : "🔴";
                
                cout << left << setw(16) << n.value("ip", "??")
                     << setw(20) << n.value("hostname", "Unknown").substr(0, 18)
                     << setw(10) << to_string(n.value("cpu_cores", 0)) + " c"
                     << setw(10) << n.value("ram_usage", "N/A")
                     << n.value("os", "Linux") << " (" << status_icon << ")" << endl;
            }
        }
        cout << string(60, '-') << endl;

    } catch (exception& e) { 
        cout << "[!] Eroare afisare: " << e.what() << endl; 
    }
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        cout << "Sintaxa: " << argv[0] << " <IP_SERVER>" << endl;
        return 1;
    }

    string server_ip = argv[1];
    cout << "[CLIENT] Conectat la: " << server_ip << endl;

    while(true) {
        cout << "\n1. Refresh Topologie (Auto 5 min)";
        cout << "\n2. Vezi Istoric (DB)";
        cout << "\n3. Power Ranking";
        cout << "\n4. Iesire";
        cout << "\nOptiune: ";
        int opt; 
        if(!(cin >> opt)) break;

        if(opt == 1) {
            while(true) {
                system("clear"); 
                string data = sendRequest(server_ip, "GET_TOPO");
                displayTopology(data);
                
                time_t acum = time(nullptr);
                cout << "\n[ULTIMUL REFRESH: " << ctime(&acum);
                cout << "[Așteptare 5 minute... Apasă 'm' + Enter pentru a te opri]\n";

                //mecanism de asteptare 5 minute
                fd_set readfds;
                struct timeval timeout;
                FD_ZERO(&readfds);
                FD_SET(0, &readfds);
                timeout.tv_sec = 300;
                timeout.tv_usec = 0;

                if (select(1, &readfds, NULL, NULL, &timeout) > 0) {
                    char c; cin >> c;
                    if (c == 'm' || c == 'M') break; 
                }
            }
        } else if(opt == 2) {
            string response = sendRequest(server_ip, "GET_HISTORY");
            try {
                auto history = json::parse(response);
                cout << "\n======= ISTORIC RECENT =======\n";
                for (auto& entry : history) {
                    cout << "\n[ DATA: " << entry.value("timestamp", "---") << " ]";
                    displayTopology(entry["topology"].dump()); //folosesc din nou fucntia de afisare ca sa afisez frumos istoricul
                    cout << "\n" << string(50, '-') << "\n";
                }
            } catch (...) { cout << "Eroare istoric.\n"; }
        } else if(opt == 3) {
            string data = sendRequest(server_ip, "GET_TOPO");
            displayRanking(data);
        } 
        else if(opt == 4) {
            cout << "[!] Iesire program.\n";
            break;
        }
    }
    return 0;
}