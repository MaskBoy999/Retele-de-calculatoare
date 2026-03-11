#!/bin/bash

#hostname -I | awk '{print $1}'
#Adresa IP a mașinii unde rulează Serverul
SERVER_IP="192.168.1.213"
#asta la date mobile
#SERVER_IP="10.207.181.35"
CLIENT="./client"

echo "[RUN] Target server IP: $SERVER_IP"
echo "[RUN] Starting client GUI..."

# Pornire client cu argumentul IP
$CLIENT $SERVER_IP

echo "[RUN] Client session closed."
