package lt.saldytuvas.recognizer

import android.content.Intent
import android.graphics.BitmapFactory
import android.net.wifi.WifiManager
import android.os.Bundle
import android.text.format.Formatter
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    private lateinit var statusText: TextView
    private lateinit var logText: TextView
    private lateinit var enrolledText: TextView
    private lateinit var nameInput: EditText
    private var serverRunning = false

    private lateinit var embeddingStore: EmbeddingStore
    private lateinit var faceCropper: FaceCropper
    private lateinit var faceEmbedder: FaceEmbedder

    private val pickImage = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null) return@registerForActivityResult
        val name = nameInput.text.toString().trim()
        if (name.isEmpty()) {
            RecognitionLog.append("Enroll: iveskite varda pirma.")
            return@registerForActivityResult
        }
        thread {
            try {
                val stream = contentResolver.openInputStream(uri)
                val bitmap = BitmapFactory.decodeStream(stream)
                val face = faceCropper.cropLargestFace(bitmap)
                if (face == null) {
                    RecognitionLog.append("Enroll: veidas nerastas nuotraukoje.")
                    return@thread
                }
                val embedding = faceEmbedder.embed(face)
                embeddingStore.add(name, embedding)
                RecognitionLog.append("Uzregistruotas: $name")
                runOnUiThread { updateEnrolledText() }
            } catch (e: Exception) {
                RecognitionLog.append("Enroll KLAIDA: ${e.message}")
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        statusText = findViewById(R.id.statusText)
        logText = findViewById(R.id.logText)
        enrolledText = findViewById(R.id.enrolledText)
        nameInput = findViewById(R.id.nameInput)

        embeddingStore = EmbeddingStore(this)
        faceCropper = FaceCropper()
        faceEmbedder = FaceEmbedder(this)

        updateEnrolledText()
        updateStatusText()

        findViewById<Button>(R.id.toggleServerButton).setOnClickListener {
            toggleServer()
        }
        findViewById<Button>(R.id.enrollButton).setOnClickListener {
            pickImage.launch("image/*")
        }

        RecognitionLog.setListener { line ->
            logText.append("$line\n")
        }
    }

    override fun onDestroy() {
        RecognitionLog.setListener(null)
        super.onDestroy()
    }

    private fun toggleServer() {
        val intent = Intent(this, RecognitionForegroundService::class.java)
        if (serverRunning) {
            stopService(intent)
        } else {
            startForegroundService(intent)
        }
        serverRunning = !serverRunning
        updateStatusText()
    }

    private fun updateStatusText() {
        findViewById<Button>(R.id.toggleServerButton).text =
            if (serverRunning) "Sustabdyti serveri" else "Paleisti serveri"

        val wifiManager = applicationContext.getSystemService(WIFI_SERVICE) as WifiManager
        @Suppress("DEPRECATION")
        val ip = Formatter.formatIpAddress(wifiManager.connectionInfo.ipAddress)
        statusText.text = if (serverRunning) {
            "Serveris veikia: http://$ip:${RecognitionForegroundService.PORT}/recognize"
        } else {
            "Serveris sustabdytas. IP (kai paleisi): $ip"
        }
    }

    private fun updateEnrolledText() {
        val names = embeddingStore.names()
        enrolledText.text = "Registruoti: " + if (names.isEmpty()) "(nera)" else names.joinToString(", ")
    }
}
