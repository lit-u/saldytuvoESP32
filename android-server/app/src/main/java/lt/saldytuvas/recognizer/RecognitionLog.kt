package lt.saldytuvas.recognizer

import android.os.Handler
import android.os.Looper
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Paprasta bendra log eile — RecognitionServer (fone veikiantis) rasoma,
 * MainActivity (UI gijoje) skaitoma per listener'i. Nera pilnos
 * architekturos (LiveData/Flow) — tyciai paprasta, testavimo programele.
 */
object RecognitionLog {
    private val mainHandler = Handler(Looper.getMainLooper())
    private var listener: ((String) -> Unit)? = null
    private val timeFormat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    fun setListener(l: ((String) -> Unit)?) {
        listener = l
    }

    fun append(message: String) {
        val line = "${timeFormat.format(Date())} $message"
        mainHandler.post { listener?.invoke(line) }
    }
}
