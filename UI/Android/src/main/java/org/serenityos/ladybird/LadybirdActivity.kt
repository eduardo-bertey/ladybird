/**
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

package org.serenityos.ladybird

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.view.KeyEvent
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.TextView
import org.serenityos.ladybird.databinding.ActivityMainBinding
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.util.zip.ZipFile
import kotlin.io.path.Path
import kotlin.io.path.inputStream
import kotlin.io.path.isDirectory
import kotlin.io.path.outputStream

class LadybirdActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var resourceDir: String
    private lateinit var view: WebView
    private lateinit var urlEditText: EditText
    private var timerService = TimerExecutorService()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        resourceDir = TransferAssets.transferAssets(this)
        // Los assets se re-extraen si cambia el layout/contenido (version) o si
        // falta el testigo. Asi un update (pm install -r conserva datos viejos)
        // se autocura solo sin desinstalar.
        val prefs = getSharedPreferences("ladybird_assets", MODE_PRIVATE)
        val ASSETS_VERSION = 2 // 1=res/, 2=plano como desktop
        val testFile = File("$resourceDir/icons/48x48/app-browser.png")
        if (prefs.getInt("assets_version", 0) != ASSETS_VERSION || !testFile.exists())
        {
            ZipFile("$resourceDir/ladybird-assets.zip").use { zip ->
                zip.entries().asSequence().forEach { entry ->
                    val fileName = entry.name
                    val file = File("$resourceDir/$fileName")
                    if (!entry.isDirectory)
                    {
                        val parentFolder = File(file.parent!!)
                        if (!parentFolder.exists())
                            parentFolder.mkdirs()
                        zip.getInputStream(entry).use { input ->
                            file.outputStream().use { output ->
                                input.copyTo(output)
                            }
                        }
                    }
                }
            }

            // curl has some issues with the Android's way of storing certificates.
            // We need to do this in order to make curl happy.
            val certMain = File("$resourceDir/cacert.pem")
            certMain.outputStream().use { output ->
                Files.walk(Path("/system/etc/security/cacerts")).forEach { certPath ->
                    if (!certPath.isDirectory()) {
                        certPath.inputStream().use { input ->
                            input.copyTo(output)
                        }
                    }
                }
            }
            prefs.edit().putInt("assets_version", ASSETS_VERSION).apply()
        }
        val userDir = applicationContext.getExternalFilesDir(null)!!.absolutePath;
        initNativeCode(resourceDir, "Ladybird", timerService, userDir)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        setSupportActionBar(binding.toolbar)
        urlEditText = binding.urlEditText
        view = binding.webView
        view.onLoadStart = { url: String, _ ->
            urlEditText.setText(url, TextView.BufferType.EDITABLE)
        }
        urlEditText.setOnEditorActionListener { textView: TextView, actionId: Int, _: KeyEvent? ->
            when (actionId) {
                EditorInfo.IME_ACTION_GO, EditorInfo.IME_ACTION_SEARCH -> view.loadURL(textView.text.toString())
            }
            false
        }
        view.initialize(resourceDir)
        view.loadURL(intent.dataString ?: "https://ladybird.dev")
    }

    override fun onStart() {
        super.onStart()
    }

    override fun onDestroy() {
        view.dispose()
        disposeNativeCode()
        super.onDestroy()
    }

    private fun scheduleEventLoop() {
        mainExecutor.execute {
            execMainEventLoop()
        }
    }

    private external fun initNativeCode(
        resourceDir: String, tag: String, timerService: TimerExecutorService, userDir: String
    )

    private external fun disposeNativeCode()
    private external fun execMainEventLoop()

    companion object {
        // Used to load the 'ladybird' library on application startup.
        init {
            System.loadLibrary("Ladybird")
        }
    }
}
