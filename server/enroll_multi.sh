#!/bin/bash
SERVER="http://192.168.43.51:5000/enroll?name=Seimininkas"
BASE="/d/_OldBoy_D/esp-32/saldytuvas/zmones/seimininkas"

# Isrenka kas 6-a faila (is 57 -> ~9-10), del ivairoves (skirtingi laikai/stiliai),
# likusius palieka kaip "held-out" testo duomenis.
i=0
for f in "$BASE"/*; do
  i=$((i+1))
  if [ $((i % 6)) -eq 0 ]; then
    echo "Registruoju: $(basename "$f")"
    curl -s -X POST --data-binary "@$f" "$SERVER"
    echo ""
  fi
done
