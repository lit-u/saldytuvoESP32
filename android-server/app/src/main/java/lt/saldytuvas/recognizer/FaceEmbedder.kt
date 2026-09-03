package lt.saldytuvas.recognizer

import android.content.Context
import android.graphics.Bitmap
import org.tensorflow.lite.Interpreter
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel

/**
 * Face embedding modelis (TFLite) — paverčia 112x112 apkirptą veido nuotrauka
 * i embedding vektoriu, kuri galima palyginti su kitais embeddings
 * (Euklido atstumas) tapatybei nustatyti.
 *
 * Modelio failas: assets/mobilefacenet_qualcomm.tflite — Qualcomm AI Hub
 * MobileFaceNet (huggingface.co/qualcomm/MobileFaceNet), ArcFace loss ant
 * MS-Celeb-1M, 128-D, 99.48% LFW. Trecias siai sesijai bandytas modelio
 * variantas (pirmas MCarlomagno, antras syaringan357/InsightFace-loss).
 *
 * SVARBUS SKIRTUMAS nuo ankstesniu dvieju: sitas modelis turi DU pavadintus
 * ijejimus (img1/img2 — Siamese poru palyginimo architektura), NCHW
 * (kanalai-pirma) isdestyma [1,3,112,112] (ne NHWC [1,112,112,3]), ir
 * [0,1] reiksmiu diapazona (ne [-1,1]). Kadangi musu architektura reikalauja
 * VIENO standalone embedding (registruoti karta, lyginti veliau), paduodame
 * TA PATI vaizda i abu ijejimus ir imame TIK pirma isvesties eilute.
 */
class FaceEmbedder(context: Context) {
    private val interpreter: Interpreter
    private val inputSize: Int
    private val channelsFirst: Boolean
    val embeddingSize: Int

    init {
        val model = loadModelFile(context, "mobilefacenet_qualcomm.tflite")
        interpreter = Interpreter(model)

        val inputShape = interpreter.getInputTensor(0).shape()
        // NCHW: [1, 3, H, W] — kanalu dim (3) pozicijoje 1. NHWC: [1, H, W, 3].
        channelsFirst = inputShape[1] == 3
        inputSize = if (channelsFirst) inputShape[2] else inputShape[1]

        val outputShape = interpreter.getOutputTensor(0).shape() // [2, 128]
        embeddingSize = outputShape[1]
    }

    fun embed(faceBitmap: Bitmap): FloatArray {
        val resized = Bitmap.createScaledBitmap(faceBitmap, inputSize, inputSize, true)
        val input = bitmapToByteBuffer(resized)

        val inputs = arrayOf<Any>(input, input) // ta pati nuotrauka i abu img1/img2
        val output = HashMap<Int, Any>()
        val embOut = Array(2) { FloatArray(embeddingSize) }
        output[0] = embOut

        interpreter.runForMultipleInputsOutputs(inputs, output)
        return l2Normalize(embOut[0])
    }

    private fun bitmapToByteBuffer(bitmap: Bitmap): ByteBuffer {
        val buffer = ByteBuffer.allocateDirect(4 * inputSize * inputSize * 3)
        buffer.order(ByteOrder.nativeOrder())
        val pixels = IntArray(inputSize * inputSize)
        bitmap.getPixels(pixels, 0, inputSize, 0, 0, inputSize, inputSize)

        // Qualcomm modelis: [0,1] diapazonas (paprastas /255, be offset).
        if (channelsFirst) {
            // NCHW — visos R, tada visos G, tada visos B (plokstuminis, ne susipynes).
            for (c in 0 until 3) {
                val shift = when (c) { 0 -> 16; 1 -> 8; else -> 0 }
                for (pixel in pixels) {
                    buffer.putFloat(((pixel shr shift) and 0xFF) / 255f)
                }
            }
        } else {
            for (pixel in pixels) {
                buffer.putFloat(((pixel shr 16) and 0xFF) / 255f)
                buffer.putFloat(((pixel shr 8) and 0xFF) / 255f)
                buffer.putFloat((pixel and 0xFF) / 255f)
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

    private fun l2Normalize(vector: FloatArray): FloatArray {
        var sumSq = 0f
        for (v in vector) sumSq += v * v
        val norm = kotlin.math.sqrt(sumSq)
        if (norm == 0f) return vector
        return FloatArray(vector.size) { i -> vector[i] / norm }
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
