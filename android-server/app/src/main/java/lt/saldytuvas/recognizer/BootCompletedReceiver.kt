package lt.saldytuvas.recognizer

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import androidx.core.content.ContextCompat

/** Starts the existing server after boot, once credential-protected data is available. */
class BootCompletedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return

        // Repeated starts use the same Service; its server == null guard prevents duplicates.
        ContextCompat.startForegroundService(
            context,
            Intent(context, RecognitionForegroundService::class.java)
        )
    }
}
