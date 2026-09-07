package lt.saldytuvas.recognizer

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import fi.iki.elonen.NanoHTTPD
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Embedded HTTP serveris telefone — ESP32 POST'ina JPEG kadra i /recognize,
 * grazina JSON {"name": ..., "distance": ...} arba {"name": "unknown"}.
 * Ta pati API forma, kaip ir laptopo Flask/DeepFace serveryje (server/
 * saldytuvas repo saknyje) — ESP32 kodo keisti nereikia, tik SECRET_SERVER_URL.
 *
 * /enroll?name=<Vardas> — POST JPEG, uzregistruoja veida su tuo vardu.
 * /health — GET, paprastas gyvybes patikrinimas.
 *
 * 2026-09-06 (vartotojo pastaba: "noriu papildomai pridėti serverio - P10
 * saugyklą") — /store?person=N: balso zinuciu saugojimas telefono atmintyje
 * (40GB) vietoj ESP32 LittleFS (~3.4MB) — tas pats principas kaip veidu
 * atpazinimas, tik audio bytu, ne JPEG. ESP32 puse (audio_output.cpp
 * Audio_UploadToPhone/Audio_DownloadFromPhone) LittleFS naudoja TIK kaip
 * laikina buferi irasant/grojant, telefonas — nuolatine saugykla.
 */
class RecognitionServer(
    port: Int,
    private val faceCropper: FaceCropper,
    private val faceEmbedder: FaceEmbedder,
    private val embeddingStore: EmbeddingStore,
    private val distanceThreshold: Float,
    private val storageDir: File,
    private val onLog: (String) -> Unit
) : NanoHTTPD(port) {

    override fun serve(session: IHTTPSession): Response {
        return try {
            val response = when {
                session.method == Method.GET && session.uri == "/health" ->
                    jsonResponse(JSONObject().put("status", "ok"))

                session.method == Method.GET && session.uri == "/test" ->
                    newFixedLengthResponse(Response.Status.OK, "text/html", TEST_PAGE_HTML)

                session.method == Method.POST && session.uri == "/recognize" ->
                    handleRecognize(session)

                session.method == Method.POST && session.uri == "/enroll" ->
                    handleEnroll(session)

                session.method == Method.POST && session.uri == "/store" ->
                    handleStoreUpload(session)

                session.method == Method.GET && session.uri == "/store" ->
                    handleStoreDownload(session)

                session.method == Method.POST && session.uri == "/photo" ->
                    handlePhotoUpload(session)

                session.method == Method.GET && session.uri == "/photo" ->
                    handlePhotoDownload()

                // 2026-09-07 (vartotojo pastaba: "šeimos nuotraukų rėmelis") —
                // galerijos nuotraukos (admin puslapio JS ikelia/tvarko
                // TIESIOGIAI, be ESP32 tarpininkavimo — zr. secrets.h
                // SECRET_SERVER_GALLERY_BASE_URL komentara). OPTIONS —
                // CORS preflight (narsykle siunciau automatiskai, nes
                // "image/jpeg" Content-Type nera "simple" pagal CORS specifikacija).
                session.method == Method.OPTIONS && session.uri.startsWith("/gallery/") ->
                    newFixedLengthResponse(Response.Status.OK, "text/plain", "")

                session.method == Method.POST && session.uri == "/gallery/upload" ->
                    handleGalleryUpload(session)

                session.method == Method.GET && session.uri == "/gallery/list" ->
                    handleGalleryList()

                session.method == Method.GET && session.uri == "/gallery/photo" ->
                    handleGalleryPhoto(session)

                session.method == Method.POST && session.uri == "/gallery/delete" ->
                    handleGalleryDelete(session)

                else -> newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "not found")
            }
            if (session.uri.startsWith("/gallery/")) addCorsHeaders(response) else response
        } catch (e: Exception) {
            onLog("KLAIDA: ${e.message}")
            jsonResponse(JSONObject().put("name", "unknown").put("error", e.message ?: "unknown"))
        }
    }

    // Admin puslapio JS kreipiasi TIESIOGIAI (skirtingas "origin" nuo ESP32
    // adminkes) — be siu antrasciu narsykle blokuotu atsakyma CORS politikos.
    private fun addCorsHeaders(response: Response): Response {
        response.addHeader("Access-Control-Allow-Origin", "*")
        response.addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        response.addHeader("Access-Control-Allow-Headers", "Content-Type")
        return response
    }

    // 2026-09-06: balso zinutes failo issaugojimas — RAW binarinis POST body
    // (ta pati konvencija kaip ESP32 admin panele /admin/audio), issaugoma
    // "audio_<person>.wav" storageDir kataloge (app privati atmintis
    // /data/data/lt.saldytuvas.recognizer/files/, NEREIKIA jokio atskiro
    // WRITE_EXTERNAL_STORAGE leidimo).
    private fun handleStoreUpload(session: IHTTPSession): Response {
        val person = session.parms["person"]?.toIntOrNull()
        if (person == null || person <= 0) {
            return jsonResponse(JSONObject().put("ok", false).put("error", "trukstu 'person' parametro"))
        }
        val contentLength = session.headers["content-length"]?.toIntOrNull()
        if (contentLength == null) {
            return jsonResponse(JSONObject().put("ok", false).put("error", "trukstu Content-Length"))
        }
        val file = File(storageDir, "audio_$person.wav")
        val input = session.inputStream
        val chunk = ByteArray(8192)
        var remaining = contentLength
        file.outputStream().use { out ->
            while (remaining > 0) {
                val read = input.read(chunk, 0, minOf(chunk.size, remaining))
                if (read == -1) break
                out.write(chunk, 0, read)
                remaining -= read
            }
        }
        onLog("Balso zinute issaugota: ${file.name} (${file.length()} baitu)")
        return jsonResponse(JSONObject().put("ok", true).put("bytes", file.length()))
    }

    // 2026-09-06: balso zinutes failo atsiuntimas — RAW binarinis atsakymas
    // (application/octet-stream), 404 jei nera irasytos zinutes tam zmogui.
    private fun handleStoreDownload(session: IHTTPSession): Response {
        val person = session.parms["person"]?.toIntOrNull()
        if (person == null || person <= 0) {
            return newFixedLengthResponse(Response.Status.BAD_REQUEST, "text/plain", "trukstu 'person' parametro")
        }
        val file = File(storageDir, "audio_$person.wav")
        if (!file.exists()) {
            return newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "nera irasytos zinutes")
        }
        return newFixedLengthResponse(
            Response.Status.OK, "application/octet-stream", file.inputStream(), file.length()
        )
    }

    // 2026-09-06 (vartotojo pastaba: "būtinai padarome ir fotografavimo per
    // esp funkciją ir rodymo iš P10 - 3.5 ekrane") — ESP32 fotografuoja,
    // POST'ina JPEG cia (ta pati raw-body logika kaip /store), telefonas
    // laiko VIENA "naujausia" nuotrauka (paprasciausias MVP — ne galerija).
    private fun handlePhotoUpload(session: IHTTPSession): Response {
        val contentLength = session.headers["content-length"]?.toIntOrNull()
        if (contentLength == null) {
            return jsonResponse(JSONObject().put("ok", false).put("error", "trukstu Content-Length"))
        }
        val file = File(storageDir.parentFile, "photo_latest.jpg")
        val input = session.inputStream
        val chunk = ByteArray(8192)
        var remaining = contentLength
        file.outputStream().use { out ->
            while (remaining > 0) {
                val read = input.read(chunk, 0, minOf(chunk.size, remaining))
                if (read == -1) break
                out.write(chunk, 0, read)
                remaining -= read
            }
        }
        onLog("Nuotrauka issaugota: ${file.length()} baitu")
        return jsonResponse(JSONObject().put("ok", true).put("bytes", file.length()))
    }

    private fun handlePhotoDownload(): Response {
        val file = File(storageDir.parentFile, "photo_latest.jpg")
        if (!file.exists()) {
            return newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "nera nuotraukos")
        }
        return newFixedLengthResponse(Response.Status.OK, "image/jpeg", file.inputStream(), file.length())
    }

    // 2026-09-07 — "šeimos nuotraukų rėmelis" (žr. saldytuvas README naują
    // skyrių). Skirtingai nuo /photo (VIENA "naujausia" nuotrauka), cia —
    // TIKRA galerija: keli failai, kiekvienas su vardu+aprašymu, niekada
    // savaime neperrasomi. Admin puslapio JS PATS suspaudzia (canvas
    // toBlob('image/jpeg')) PRIES siusdamas — visada baseline JPEG, TAD
    // sis serveris tiesiog issaugo baitus, be jokio papildomo apdorojimo.
    // 2026-09-07 (vartotojo pastaba: "Data nebūtina, palik aprašymui
    // laukelį") — data NEBERENKAMA is vartotojo (naudojamas failo
    // lastModified() rodymui), o vietoj jos LAISVAS teksto aprasymas,
    // saugomas SALIA esancio ".txt" failo (ta pati baze, tas pats vardas).
    private fun galleryDir(): File {
        val dir = File(storageDir.parentFile, "gallery")
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    private fun handleGalleryUpload(session: IHTTPSession): Response {
        val rawName = session.parms["name"]?.trim()
        if (rawName.isNullOrBlank()) {
            return jsonResponse(JSONObject().put("ok", false).put("error", "trukstu 'name' parametro"))
        }
        val contentLength = session.headers["content-length"]?.toIntOrNull()
        if (contentLength == null) {
            return jsonResponse(JSONObject().put("ok", false).put("error", "trukstu Content-Length"))
        }
        // Failo varde saugomas vardas — paprasciausias "duomenu baze" be
        // atskiro JSON/SQLite indekso (nuoseklu su likusio projekto "kuo
        // paprasciau" filosofija, zr. README). Aprasymas — atskirame ".txt".
        val safeName = rawName.replace(Regex("[^\\w ÄÖÜäöüÀ-ÿ-]"), "_").take(40)
        val baseName = "${safeName}_${System.currentTimeMillis()}"
        val file = File(galleryDir(), "$baseName.jpg")
        val input = session.inputStream
        val chunk = ByteArray(8192)
        var remaining = contentLength
        file.outputStream().use { out ->
            while (remaining > 0) {
                val read = input.read(chunk, 0, minOf(chunk.size, remaining))
                if (read == -1) break
                out.write(chunk, 0, read)
                remaining -= read
            }
        }
        val description = session.parms["description"]?.trim()
        if (!description.isNullOrEmpty()) {
            File(galleryDir(), "$baseName.txt").writeText(description, Charsets.UTF_8)
        }
        onLog("Galerijos nuotrauka issaugota: $baseName.jpg (${file.length()} baitu)")
        return jsonResponse(JSONObject().put("ok", true).put("file", "$baseName.jpg"))
    }

    private fun handleGalleryList(): Response {
        val arr = JSONArray()
        val dateFmt = SimpleDateFormat("yyyy-MM-dd", Locale.US)
        galleryDir().listFiles { f -> f.isFile && f.name.endsWith(".jpg") }
            ?.sortedByDescending { it.lastModified() }
            ?.forEach { f ->
                val base = f.name.removeSuffix(".jpg")
                val parts = base.split("_")
                // Paskutinis "_"-atskirtas gabalas = millis (unikalumui), likusi dalis = vardas.
                val name = parts.dropLast(1).joinToString("_")
                val descFile = File(galleryDir(), "$base.txt")
                val description = if (descFile.exists()) descFile.readText(Charsets.UTF_8) else ""
                arr.put(
                    JSONObject()
                        .put("file", f.name)
                        .put("name", name)
                        .put("date", dateFmt.format(Date(f.lastModified())))
                        .put("description", description)
                        .put("bytes", f.length())
                )
            }
        return jsonResponse(JSONObject().put("items", arr))
    }

    private fun handleGalleryPhoto(session: IHTTPSession): Response {
        val fname = session.parms["file"]
        val file = resolveGalleryFile(fname)
            ?: return newFixedLengthResponse(Response.Status.NOT_FOUND, "text/plain", "nera nuotraukos")
        return newFixedLengthResponse(Response.Status.OK, "image/jpeg", file.inputStream(), file.length())
    }

    private fun handleGalleryDelete(session: IHTTPSession): Response {
        val fname = session.parms["file"]
        val file = resolveGalleryFile(fname)
            ?: return jsonResponse(JSONObject().put("ok", false).put("error", "nera tokio failo"))
        val deleted = file.delete()
        File(galleryDir(), file.name.removeSuffix(".jpg") + ".txt").delete()
        onLog("Galerijos nuotrauka pasalinta: $fname")
        return jsonResponse(JSONObject().put("ok", deleted))
    }

    // Apsauga nuo path traversal ("file=../../whatever") — tikrinama, kad
    // rezultatas TIKRAI liktu galleryDir() viduje.
    private fun resolveGalleryFile(fname: String?): File? {
        if (fname.isNullOrBlank()) return null
        val dir = galleryDir()
        val file = File(dir, fname)
        if (!file.exists()) return null
        if (!file.canonicalPath.startsWith(dir.canonicalPath + File.separator)) return null
        return file
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
