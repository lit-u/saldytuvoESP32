package lt.saldytuvas.recognizer

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import fi.iki.elonen.NanoHTTPD
import org.json.JSONObject
import java.io.ByteArrayOutputStream

/**
 * Embedded HTTP serveris telefone — ESP32 POST'ina JPEG kadra i /recognize,
 * grazina JSON {"name": ..., "distance": ...} arba {"name": "unknown"}.
 * Ta pati API forma, kaip ir laptopo Flask/DeepFace serveryje (server/
 * saldytuvas repo saknyje) — ESP32 kodo keisti nereikia, tik SECRET_SERVER_URL.
 *
 * /enroll?name=<Vardas> — POST JPEG, uzregistruoja veida su tuo vardu.
 * /health — GET, paprastas gyvybes patikrinimas.
 */
class RecognitionServer(
    port: Int,
    private val faceCropper: FaceCropper,
    private val faceEmbedder: FaceEmbedder,
    private val embeddingStore: EmbeddingStore,
    private val distanceThreshold: Float,
    private val onLog: (String) -> Unit
) : NanoHTTPD(port) {

    override fun serve(session: IHTTPSession): Response {
        return try {
            when {
                session.method == Method.GET && session.uri == "/health" ->
                    jsonResponse(JSONObject().put("status", "ok"))

                session.method == Method.GET && session.uri == "/test" ->
                    newFixedLengthResponse(Response.Status.OK, "text/html", TEST_PAGE_HTML)

                session.method == Method.POST && session.uri == "/recognize" ->
                    handleRecognize(session)

                session.method == Method.POST && session.uri == "/enroll" ->
                    handleEnroll(session)

                else -> newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "not found")
            }
        } catch (e: Exception) {
            onLog("KLAIDA: ${e.message}")
            jsonResponse(JSONObject().put("name", "unknown").put("error", e.message ?: "unknown"))
        }
    }

    private fun handleRecognize(session: IHTTPSession): Response {
        val bitmap = readJpegBody(session) ?: return jsonResponse(
            JSONObject().put("name", "unknown").put("error", "negalima dekoduoti JPEG")
        )

        val face = faceCropper.cropLargestFace(bitmap)
        if (face == null) {
            onLog("Veidas nerastas kadre.")
            return jsonResponse(
                JSONObject().put("name", "unknown").put("reason", "no_face_detected")
            )
        }

        val embedding = faceEmbedder.embed(face)
        val match = embeddingStore.findClosest(embedding)

        if (match == null || match.second > distanceThreshold) {
            onLog("Veidas rastas, bet neatpazintas (atstumas=${match?.second}).")
            val resp = JSONObject().put("name", "unknown").put("reason", "too_different")
            if (match != null) {
                resp.put("closest_name", match.first).put("closest_distance", match.second.toDouble())
            }
            return jsonResponse(resp)
        }

        onLog("Atpazinta: ${match.first} (atstumas=${match.second})")
        return jsonResponse(
            JSONObject().put("name", match.first).put("distance", match.second.toDouble())
        )
    }

    private fun handleEnroll(session: IHTTPSession): Response {
        val name = session.parms["name"]
        if (name.isNullOrBlank()) {
            return jsonResponse(JSONObject().put("ok", false).put("error", "trukstu 'name' parametro"))
        }

        val bitmap = readJpegBody(session) ?: return jsonResponse(
            JSONObject().put("ok", false).put("error", "negalima dekoduoti JPEG")
        )

        val face = faceCropper.cropLargestFace(bitmap)
            ?: return jsonResponse(JSONObject().put("ok", false).put("error", "veidas nerastas"))

        val embedding = faceEmbedder.embed(face)
        embeddingStore.add(name, embedding)
        onLog("Uzregistruotas: $name")
        return jsonResponse(JSONObject().put("ok", true).put("name", name))
    }

    private fun readJpegBody(session: IHTTPSession): Bitmap? {
        // NanoHTTPD reikalauja parseBody() ismesti POST body i tempfile ("postData"
        // raktu), arba naudoti session.inputStream tiesiogiai priklausomai nuo
        // Content-Length. Cia skaitome tiesiai is inputStream pagal Content-Length.
        val contentLength = session.headers["content-length"]?.toIntOrNull() ?: return null
        val buffer = ByteArrayOutputStream()
        val input = session.inputStream
        val chunk = ByteArray(8192)
        var remaining = contentLength
        while (remaining > 0) {
            val read = input.read(chunk, 0, minOf(chunk.size, remaining))
            if (read == -1) break
            buffer.write(chunk, 0, read)
            remaining -= read
        }
        val bytes = buffer.toByteArray()
        return BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
    }

    private fun jsonResponse(json: JSONObject): Response {
        return newFixedLengthResponse(Response.Status.OK, "application/json", json.toString())
    }

    companion object {
        // /test — paprastas rankinis testavimo puslapis (nuotraukos pasirinkimas
        // -> POST /recognize -> JSON atsakymas). Tas pats "origin" kaip serveris,
        // tad nera CORS problemu. Atidaryti bet kuriame irenginyje to paties
        // tinklo narsykleje: http://<telefono-ip>:5000/test
        private const val TEST_PAGE_HTML = """<!DOCTYPE html>
<html lang="lt"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Atpazinimo testas</title>
<style>
  body { font-family: sans-serif; max-width: 500px; margin: 12px auto; padding: 0 12px; }
  #result { margin-top: 16px; padding: 12px; border-radius: 8px; font-size: 18px; white-space: pre-wrap; }
  .ok { background: #d4f7d4; } .no { background: #f7d4d4; }
  img { max-width: 100%; margin-top: 12px; border-radius: 8px; }
  input, button { font-size: 16px; padding: 8px; }
  #history { margin-top: 20px; font-size: 13px; }
  #history div { padding: 4px 0; border-bottom: 1px solid #eee; }
</style></head><body>
<h2>Veido atpazinimo testas</h2>
<input type="file" id="fileInput" accept="image/*">
<div id="result"></div>
<img id="preview">
<h3>Istorija</h3>
<div id="history"></div>
<script>
document.getElementById("fileInput").addEventListener("change", async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  document.getElementById("preview").src = URL.createObjectURL(file);
  const resultDiv = document.getElementById("result");
  resultDiv.textContent = "Testuojama..."; resultDiv.className = "";
  const start = performance.now();
  try {
    const resp = await fetch("/recognize", { method: "POST", headers: {"Content-Type": "image/jpeg"}, body: file });
    const elapsed = ((performance.now() - start) / 1000).toFixed(1);
    const json = await resp.json();
    const ok = json.name && json.name !== "unknown";
    resultDiv.textContent = JSON.stringify(json, null, 2) + "\n\n(" + elapsed + "s)";
    resultDiv.className = ok ? "ok" : "no";
    const entry = document.createElement("div");
    entry.textContent = file.name + ": " + JSON.stringify(json) + " (" + elapsed + "s)";
    document.getElementById("history").prepend(entry);
  } catch (err) {
    resultDiv.textContent = "KLAIDA: " + err; resultDiv.className = "no";
  }
});
</script></body></html>"""
    }
}
