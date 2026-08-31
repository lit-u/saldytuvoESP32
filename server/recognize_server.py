"""
Veido atpazinimo serveris (saldytuvo terminalo eksperimentui).

ESP32 POST'ina JPEG kadra (raw baitai, Content-Type: image/jpeg) i /recognize,
serveris ji palygina su known_faces/<Vardas>/*.jpg nuotraukomis per DeepFace
(VGG-Face backend — pasirinktas del geresnio tikslumo atskiriant panasius
veidus, pvz. seseris/brolius, zr. README).

Registravimas: tiesiog idek 2-3 nuotraukas i known_faces/<Vardas>/ (Vardas
TURI sutapti su family_profiles.cpp displayName, pvz. "Seimininkas").
Pirma karta paleidus, DeepFace sukurs kesavima faila (representations_*.pkl)
sitame aplanke — istrink ji, jei pridejai/pasalinai nuotrauku.

Paleidimas: python recognize_server.py  (klausosi 0.0.0.0:5000)
"""
import io
import logging

import cv2
import numpy as np
from flask import Flask, request, jsonify
from deepface import DeepFace

KNOWN_FACES_DIR = "known_faces"
MODEL_NAME = "VGG-Face"
# "opencv" (DeepFace numatytasis) reikalauja Haar cascade XML is cv2 paketo
# duomenu — sioje aplinkoje (opencv-python 5.0.0) tu failu nera ikelta
# (tik __init__.py cv2/data/). "mtcnn" jau idiegtas kaip DeepFace
# priklausomybe, tad naudojam ji vietoj to.
DETECTOR_BACKEND = "mtcnn"

app = Flask(__name__)
logging.basicConfig(level=logging.INFO)
log = logging.getLogger("recognize_server")


@app.route("/recognize", methods=["POST"])
def recognize():
    jpeg_bytes = request.get_data()
    if not jpeg_bytes:
        return jsonify({"name": "unknown", "error": "tuscias request body"}), 400

    frame = cv2.imdecode(np.frombuffer(jpeg_bytes, np.uint8), cv2.IMREAD_COLOR)
    if frame is None:
        return jsonify({"name": "unknown", "error": "nepavyko dekoduoti JPEG"}), 400

    try:
        results = DeepFace.find(
            img_path=frame,
            db_path=KNOWN_FACES_DIR,
            model_name=MODEL_NAME,
            detector_backend=DETECTOR_BACKEND,
            enforce_detection=False,
            silent=True,
        )
    except ValueError as exc:
        # Dazniausiai: known_faces/ tuscias (dar niekas neregistruotas).
        log.warning("DeepFace.find() klaida (greiciausiai tuscias known_faces/): %s", exc)
        return jsonify({"name": "unknown"})

    if not results or results[0].empty:
        return jsonify({"name": "unknown"})

    best = results[0].iloc[0]
    identity_path = best["identity"]
    # identity_path pvz. "known_faces/Seimininkas/foto1.jpg" -> "Seimininkas"
    name = identity_path.replace("\\", "/").split("/")[-2]

    distance_col = next((c for c in best.index if "distance" in c), None)
    distance = float(best[distance_col]) if distance_col else None

    log.info("Atpazinta: %s (distance=%s)", name, distance)
    return jsonify({"name": name, "distance": distance})


@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok"})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
