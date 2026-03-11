#!/bin/bash

#hostname -I | awk '{print $1}'
# Adresa IP a mașinii unde rulează Agentul
AGENT_IP="192.168.1.213"
#asta la date mobile
#AGENT_IP="10.207.181.35"
INFO="./info"

echo "[RUN] Target agent IP: $AGENT_IP"
echo "[RUN] Starting background monitor..."

# Pornire info cu argumentul IP
$INFO $AGENT_IP

echo "[RUN] Monitor stopped."
