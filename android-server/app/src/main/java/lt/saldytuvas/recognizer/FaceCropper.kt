package lt.saldytuvas.recognizer

import android.graphics.Bitmap
import android.graphics.Rect
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.face.Face
import com.google.mlkit.vision.face.FaceDetection
import com.google.mlkit.vision.face.FaceDetectorOptions

/**
 * ML Kit — TIK veido APTIKIMAS (surasti/apkirpti regiona), NE atpazinimas.
 * Patikrinta tiesiogiai (oficiali Google dokumentacija, 2026-09-01): "the API
 * detects faces, it does not recognize people." Atpazinimui zr. FaceEmbedder.
 */
class FaceCropper {
    private val detector = FaceDetection.getClient(
        FaceDetectorOptions.Builder()
            .setPerformanceMode(FaceDetectorOptions.PERFORMANCE_MODE_ACCURATE)
            .build()
    )

    /** Grazina DIDZIAUSIA (labiausiai tiketina pagrindine) rastą veidą apkirpta, arba null. */
    fun cropLargestFace(bitmap: Bitmap): Bitmap? {
        val image = InputImage.fromBitmap(bitmap, 0)
        val faces: List<Face> = Tasks.await(detector.process(image))
        if (faces.isEmpty()) return null

        val largest = faces.maxByOrNull { it.boundingBox.width() * it.boundingBox.height() }
            ?: return null

        val box = clampToBitmap(largest.boundingBox, bitmap.width, bitmap.height)
        if (box.width() <= 0 || box.height() <= 0) return null

        return Bitmap.createBitmap(bitmap, box.left, box.top, box.width(), box.height())
    }

    private fun clampToBitmap(box: Rect, width: Int, height: Int): Rect {
        return Rect(
            box.left.coerceIn(0, width),
            box.top.coerceIn(0, height),
            box.right.coerceIn(0, width),
            box.bottom.coerceIn(0, height)
        )
    }
}
