package lt.saldytuvas.recognizer

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

/**
 * Foreground service — laiko RecognitionServer gyva net kai app fone/ekranas
 * isjungtas. Be sito Android po kurio laiko "uzmustu" procesa (background
 * task killing), ir ESP32 nebegautu atsakymo.
 */
class RecognitionForegroundService : Service() {

    private var server: RecognitionServer? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(NOTIFICATION_ID, buildNotification())

        if (server == null) {
            val store = EmbeddingStore(this)
            val cropper = FaceCropper()
            val embedder = FaceEmbedder(this)
            server = RecognitionServer(
                port = PORT,
                faceCropper = cropper,
                faceEmbedder = embedder,
                embeddingStore = store,
                distanceThreshold = DISTANCE_THRESHOLD
            ) { msg -> RecognitionLog.append(msg) }
            server?.start()
            RecognitionLog.append("Serveris paleistas, portas $PORT")
        }

        return START_STICKY
    }

    override fun onDestroy() {
        server?.stop()
        server = null
        RecognitionLog.append("Serveris sustabdytas.")
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, "Atpazinimo serveris", NotificationManager.IMPORTANCE_LOW
            )
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Saldytuvo atpazinimo serveris")
            .setContentText("Veikia, klausosi porto $PORT")
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setOngoing(true)
            .build()
    }

    companion object {
        const val PORT = 5000
        // PATIKRINTA REALIU HARDWARE 2026-09-01: to paties zmogaus, skirtingu
        // nuotrauku atstumas siame modelio konvertavime buvo 1.104 — pradinis
        // 1.0 buvo per grieztas, atmete tikra atitikima. 1.3 duoda saugu
        // atsarga, bet dar nepatikrinta prieš SKIRTINGUS zmones (false-positive
        // rizika nezinoma) — koreguoti, kai bus daugiau realiu duomenu.
        const val DISTANCE_THRESHOLD = 1.3f
        private const val CHANNEL_ID = "recognition_server"
        private const val NOTIFICATION_ID = 1
    }
}
