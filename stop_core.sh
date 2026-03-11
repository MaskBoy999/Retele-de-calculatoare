#!/bin/bash

if [ ! -f pids.txt ]; then
    echo "[!] Nu am găsit pids.txt. Procesele nu par a fi pornite de script."
    exit 1
fi

echo "[*] Oprire procese..."
while IFS= read -r pid; do
    if ps -p $pid > /dev/null; then
        kill -9 $pid
        echo "[-] Procesul $pid a fost oprit."
    else
        echo "[?] Procesul $pid nu mai rula."
    fi
done < pids.txt

rm pids.txt
# Opțional: curățăm și fișierul de sincronizare pentru un start curat data viitoare
# rm shared_data.txt 

echo "[OK] Totul a fost oprit."