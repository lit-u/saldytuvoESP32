#!/bin/bash
# Automatinis testas: pereina per visas nuotraukas zmones/ aplankuose,
# siuncia i /recognize, ir suskaiciuoja tiksluma pagal aplanko pavadinima
# (seimininkas = teisinga atpazinti kaip "Seimininkas", sunus/kiti = teisinga
# gauti "unknown").
SERVER="http://192.168.43.51:5000/recognize"
BASE="/d/_OldBoy_D/esp-32/saldytuvas/zmones"

true_pos=0
false_neg=0
true_neg=0
false_pos=0

echo "=== seimininkas ==="
for f in "$BASE/seimininkas"/*; do
  result=$(curl -s -m 15 -X POST -H "Content-Type: image/jpeg" --data-binary "@$f" "$SERVER")
  name=$(echo "$result" | grep -o '"name":"[^"]*"' | cut -d'"' -f4)
  dist=$(echo "$result" | grep -o '"distance":[0-9.]*' | cut -d: -f2)
  if [ "$name" == "Seimininkas" ]; then
    true_pos=$((true_pos+1))
    echo "OK   $dist   $(basename "$f")"
  else
    false_neg=$((false_neg+1))
    echo "MISS $result   $(basename "$f")"
  fi
done

echo ""
echo "=== sunus ==="
for f in "$BASE/sunus"/*; do
  result=$(curl -s -m 15 -X POST -H "Content-Type: image/jpeg" --data-binary "@$f" "$SERVER")
  name=$(echo "$result" | grep -o '"name":"[^"]*"' | cut -d'"' -f4)
  if [ "$name" == "Seimininkas" ]; then
    false_pos=$((false_pos+1))
    echo "FALSE-POS  $result   $(basename "$f")"
  else
    true_neg=$((true_neg+1))
    echo "OK-reject  $result   $(basename "$f")"
  fi
done

echo ""
echo "=== kiti ==="
for f in "$BASE/kiti"/*; do
  result=$(curl -s -m 15 -X POST -H "Content-Type: image/jpeg" --data-binary "@$f" "$SERVER")
  name=$(echo "$result" | grep -o '"name":"[^"]*"' | cut -d'"' -f4)
  if [ "$name" == "Seimininkas" ]; then
    false_pos=$((false_pos+1))
    echo "FALSE-POS  $result   $(basename "$f")"
  else
    true_neg=$((true_neg+1))
    echo "OK-reject  $result   $(basename "$f")"
  fi
done

echo ""
echo "======================================"
echo "Seimininkas teisingai atpazinta: $true_pos / $((true_pos+false_neg))"
echo "Sunus+kiti teisingai atmesti:    $true_neg / $((true_neg+false_pos))"
echo "Klaidingu priemimu:              $false_pos"
