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

        // PATIKRINTA 2026-09-02: originalus mobilefacenet.tflite saltinis
        // (MCarlomagno/FaceRecognitionAuth) prie ML Kit stacikampio prideda
        // paraste (jie: fiksuoti 10px, tinka ju maziems kameros kadrams;
        // cia proporcinga % dydzio, nes musu nuotraukos ivairaus dydzio) —
        // be sitos parastes musu atstumai buvo sistemingai per dideli
        // (0.97-1.4 vietoj tiketo ~0.5 slenkscio diapazono).
        val marginX = (largest.boundingBox.width() * 0.15f).toInt()
        val marginY = (largest.boundingBox.height() * 0.15f).toInt()
        val expanded = Rect(
            largest.boundingBox.left - marginX,
            largest.boundingBox.top - marginY,
            largest.boundingBox.right + marginX,
            largest.boundingBox.bottom + marginY
        )

        val box = clampToBitmap(expanded, bitmap.width, bitmap.height)
        if (box.width() <= 0 || box.height() <= 0) return null

        // PATIKRINTA: originalus saltinis naudoja copyResizeCropSquare (kerpa
        // IKI kvadrato, NEIKRAIPO proporciju), o musu ankstesnis kodas tiesiog
        // "istempdavo" staciakampi iki 112x112 (FaceEmbedder.embed() resize),
        // iskraipydamas veido proporcijas. Cia padarome kvadrata PRIES resize.
        val square = squareCrop(box, bitmap.width, bitmap.height)
        return Bitmap.createBitmap(bitmap, square.left, square.top, square.width(), square.height())
    }

    /** Sutraukia staciakampi i kvadrata (imant trumpesne krastine), centruotai. */
    private fun squareCrop(box: Rect, bitmapWidth: Int, bitmapHeight: Int): Rect {
        val side = minOf(box.width(), box.height())
        val cx = box.centerX()
        val cy = box.centerY()
        val half = side / 2
        return clampToBitmap(Rect(cx - half, cy - half, cx + half, cy + half), bitmapWidth, bitmapHeight)
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
