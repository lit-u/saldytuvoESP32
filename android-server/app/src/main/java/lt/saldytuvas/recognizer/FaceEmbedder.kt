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
    val embeddingSize: Int

    init {
        val model = loadModelFile(context, "mobilefacenet.tflite")
        interpreter = Interpreter(model)

        val inputShape = interpreter.getInputTensor(0).shape() // [1, H, W, 3]
        inputSize = inputShape[1]

        val outputShape = interpreter.getOutputTensor(0).shape() // [1, N]
        embeddingSize = outputShape[1]
    }

    fun embed(faceBitmap: Bitmap): FloatArray {
        val resized = Bitmap.createScaledBitmap(faceBitmap, inputSize, inputSize, true)
        val input = bitmapToByteBuffer(resized)
        val output = Array(1) { FloatArray(embeddingSize) }
        interpreter.run(input, output)
        return output[0]
    }

    private fun bitmapToByteBuffer(bitmap: Bitmap): ByteBuffer {
        val buffer = ByteBuffer.allocateDirect(4 * inputSize * inputSize * 3)
        buffer.order(ByteOrder.nativeOrder())
        val pixels = IntArray(inputSize * inputSize)
        bitmap.getPixels(pixels, 0, inputSize, 0, 0, inputSize, inputSize)
        for (pixel in pixels) {
            // Normalizuota [-1, 1], standartinis MobileFaceNet preprocessing.
            buffer.putFloat((((pixel shr 16) and 0xFF) - 127.5f) / 128f)
            buffer.putFloat((((pixel shr 8) and 0xFF) - 127.5f) / 128f)
            buffer.putFloat(((pixel and 0xFF) - 127.5f) / 128f)
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
