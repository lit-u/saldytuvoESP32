package lt.saldytuvas.recognizer

import android.content.Context
import android.graphics.Bitmap
import org.tensorflow.lite.Interpreter
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel

/**
 * MobileFaceNet (TFLite) — paverčia 112x112 apkirptą veido nuotrauka i
 * embedding vektoriu (skaiciu masyva), kuri galima palyginti su kitais
 * embeddings (Euklido/kosinuso atstumas) tapatybei nustatyti.
 *
 * Modelio failas: assets/mobilefacenet.tflite (zr. README del saltinio).
 */
class FaceEmbedder(context: Context) {
    private val interpreter: Interpreter
    private val inputSize: Int
    private val batchSize: Int
    val embeddingSize: Int

    init {
        val model = loadModelFile(context, "mobilefacenet.tflite")
        interpreter = Interpreter(model)

        // PATIKRINTA 2026-09-02: kai kurie MobileFaceNet/InsightFace konvertavimai
        // (pvz. syaringan357 variantas) tikisi batch=2 (skirtas TIESIOGINIAM poru
        // palyginimui), ne batch=1 kaip musu originalus MCarlomagno saltinis.
        // Skaitome dinamiskai, kad veiktu abu atvejai — dubliuojame ta pati vaizda
        // i visas batch "vietas" ir imame TIK pirma isvesties eilute.
        val inputShape = interpreter.getInputTensor(0).shape() // [batch, H, W, 3]
        batchSize = inputShape[0]
        inputSize = inputShape[1]

        val outputShape = interpreter.getOutputTensor(0).shape() // [batch, N]
        embeddingSize = outputShape[1]
    }

    fun embed(faceBitmap: Bitmap): FloatArray {
        val resized = Bitmap.createScaledBitmap(faceBitmap, inputSize, inputSize, true)
        val input = bitmapToByteBuffer(resized)
        val output = Array(batchSize) { FloatArray(embeddingSize) }
        interpreter.run(input, output)
        return l2Normalize(output[0])
    }

    /**
     * PATIKRINTA 2026-09-02: originalus mobilefacenet.tflite (MCarlomagno)
     * jau grazina L2=1.0 normalizuota vektoriu (patikrinta tiesiogiai per adb).
     * BET syaringan357 InsightFace-loss variantas — NE (rasti atstumai 29-54,
     * vietoj tiketo 0-2 diapazono unit vektoriams). Reference arcface python
     * paketas (ArcFace.py) TIES sitas daro l2_norm() PO inferencijos rankiniu
     * budu — kartojame ta pati zingsni cia UNIVERSALIAI (nekenkia jau
     * normalizuotam modeliui, butina nenormalizuotam).
     */
    private fun l2Normalize(vector: FloatArray): FloatArray {
        var sumSq = 0f
        for (v in vector) sumSq += v * v
        val norm = kotlin.math.sqrt(sumSq)
        if (norm == 0f) return vector
        return FloatArray(vector.size) { i -> vector[i] / norm }
    }

    private fun bitmapToByteBuffer(bitmap: Bitmap): ByteBuffer {
        val buffer = ByteBuffer.allocateDirect(4 * batchSize * inputSize * inputSize * 3)
        buffer.order(ByteOrder.nativeOrder())
        val pixels = IntArray(inputSize * inputSize)
        bitmap.getPixels(pixels, 0, inputSize, 0, 0, inputSize, inputSize)
        repeat(batchSize) {
            for (pixel in pixels) {
                // Normalizuota [-1, 1], standartinis MobileFaceNet preprocessing.
                buffer.putFloat((((pixel shr 16) and 0xFF) - 127.5f) / 128f)
                buffer.putFloat((((pixel shr 8) and 0xFF) - 127.5f) / 128f)
                buffer.putFloat(((pixel and 0xFF) - 127.5f) / 128f)
            }
        }
        buffer.rewind()
        return buffer
    }

    private fun loadModelFile(context: Context, assetName: String): ByteBuffer {
        val afd = context.assets.openFd(assetName)
        FileInputStream(afd.fileDescriptor).use { input ->
            val channel = input.channel
            return channel.map(
                FileChannel.MapMode.READ_ONLY,
                afd.startOffset,
                afd.declaredLength
            )
        }
    }

    companion object {
        /** Euklido atstumas — mazesnis = panasesni veidai. */
        fun distance(a: FloatArray, b: FloatArray): Float {
            var sum = 0f
            for (i in a.indices) {
                val d = a[i] - b[i]
                sum += d * d
            }
            return kotlin.math.sqrt(sum)
        }
    }
}
