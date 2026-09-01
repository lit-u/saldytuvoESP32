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
            return jsonResponse(JSONObject().put("name", "unknown"))
        }

        val embedding = faceEmbedder.embed(face)
        val match = embeddingStore.findClosest(embedding)

        if (match == null || match.second > distanceThreshold) {
            onLog("Veidas rastas, bet neatpazintas (atstumas=${match?.second}).")
            return jsonResponse(JSONObject().put("name", "unknown"))
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
}
