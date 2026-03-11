#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;
using namespace std;

#define SHARED_FILE "shared_data.txt"
#define AGENT_PORT 9001

mutex cache_mutex;

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

//functie care asteapta sa se elibere fisieru;
void wait_for_file_ready() {
    while (true) {
        ifstream f(SHARED_FILE);
        if (!f.is_open()) return; //daca nu exista oprim
        
        string first_line;
        getline(f, first_line);
        f.close();

        if (first_line == "1") return; //fisierul e gata de modificare
        
        //daca cumva e 0 e ocupat un fel de semafor dar blocheaza tot fisierul nu doar biti modificati pentru securitatea informatiilor
        this_thread::sleep_for(chrono::milliseconds(50));
    }
}

void update_network_state(string client_ip, json new_data) {
    lock_guard<mutex> lock(cache_mutex);
    
    //astpta randul la fisier
    wait_for_file_ready();

    //citim starea actuala pentru a nu sterge ce a scris serverul
    json current_state = json::object();
    ifstream f_in(SHARED_FILE);
    if (f_in.is_open()) {
        string flag;
        getline(f_in, flag);
        try {
            f_in >> current_state;
        } catch(...) { current_state = json::object(); }
        f_in.close();
    }

    //actualizam datele pentru ipul curent
    current_state[client_ip] = new_data;
    current_state[client_ip]["ip"] = client_ip;
    current_state[client_ip]["last_seen"] = time(nullptr);
    current_state[client_ip]["status"] = "online";

    //fcuntie pentru a pune datele sigur in fisierul shared
    safeWriteShared(current_state);

    cout << "[AGENT] Actualizat date pentru: " << client_ip << endl;
}

void handle_client(int sock, string ip) {
    char buffer[8192];
    int n = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        try {
            json data = json::parse(buffer);
            update_network_state(ip, data);
        } catch(...) {}
    }
    close(sock);
}

int main() {
    //ne asiguram ca exista fisierul
    {
        ifstream check(SHARED_FILE);
        if(!check) {
            ofstream init(SHARED_FILE);
            init << "1" << endl << "{}" << endl;
        }
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(AGENT_PORT);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    listen(server_fd, 50);

    cout << "[AGENT] Ascult portul " << AGENT_PORT << ". Gata pentru info.cpp..." << endl;

    while (true) {
        sockaddr_in c_addr;
        socklen_t len = sizeof(c_addr);
        int c_sock = accept(server_fd, (sockaddr*)&c_addr, &len);
        if (c_sock >= 0) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(c_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
            thread(handle_client, c_sock, string(ip_str)).detach();
        }
    }
    return 0;
}