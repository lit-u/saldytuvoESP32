package lt.saldytuvas.recognizer

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Registruotu veidu embeddings saugykla — paprastas JSON failas telefono
 * vidineje atmintyje (nera SD/cloud priklausomybes). Kiekvienas vardas gali
 * tureti KELIS embeddings (registruota is keliu nuotrauku) — atpazystant
 * lyginama su VISAIS, imamas artimiausias.
 */
class EmbeddingStore(context: Context) {
    private val file = File(context.filesDir, "embeddings.json")
    private val data = mutableMapOf<String, MutableList<FloatArray>>()

    init {
        load()
    }

    fun add(name: String, embedding: FloatArray) {
        data.getOrPut(name) { mutableListOf() }.add(embedding)
        save()
    }

    fun clearAll() {
        data.clear()
        save()
    }

    fun names(): Set<String> = data.keys

    /** Grazina (vardas, atstumas) artimiausiam registruotam veidui, arba null jei nieko neregistruota. */
    fun findClosest(embedding: FloatArray): Pair<String, Float>? {
        var bestName: String? = null
        var bestDist = Float.MAX_VALUE
        for ((name, embeddings) in data) {
            for (e in embeddings) {
                val d = FaceEmbedder.distance(embedding, e)
                if (d < bestDist) {
                    bestDist = d
                    bestName = name
                }
            }
        }
        return bestName?.let { it to bestDist }
    }

    private fun load() {
        if (!file.exists()) return
        val json = JSONObject(file.readText())
        for (name in json.keys()) {
            val arr = json.getJSONArray(name)
            val list = mutableListOf<FloatArray>()
            for (i in 0 until arr.length()) {
                val embArr = arr.getJSONArray(i)
                val emb = FloatArray(embArr.length()) { j -> embArr.getDouble(j).toFloat() }
                list.add(emb)
            }
            data[name] = list
        }
    }

    private fun save() {
        val json = JSONObject()
        for ((name, embeddings) in data) {
            val arr = JSONArray()
            for (emb in embeddings) {
                val embArr = JSONArray()
                for (v in emb) embArr.put(v.toDouble())
                arr.put(embArr)
            }
            json.put(name, arr)
        }
        file.writeText(json.toString())
    }
}
