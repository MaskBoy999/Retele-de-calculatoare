#!/bin/bash

# Verificăm dacă sunt deja pornite
if [ -f pids.txt ]; then
    echo "[!] pids.txt există deja. Rulează stop_core.sh mai întâi."
    exit 1
fi

echo "[+] Pornire Agent..."
./agent &
AGENT_PID=$!

echo "[+] Pornire Server..."
./server &
SERVER_PID=$!

# Salvăm PID-urile
echo $AGENT_PID > pids.txt
echo $SERVER_PID >> pids.txt

echo "[OK] Agent (PID: $AGENT_PID) și Server (PID: $SERVER_PID) rulează în fundal."