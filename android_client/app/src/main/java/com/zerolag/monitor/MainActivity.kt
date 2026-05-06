package com.zerolag.monitor

import android.animation.ObjectAnimator
import android.animation.PropertyValuesHolder
import android.animation.ValueAnimator
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private val streamWidth  = 1920
    private val streamHeight = 1080
    private val useHevc      = true
    private val udpPort      = 9000

    private var streaming = false
    private lateinit var overlay: FrameLayout
    private lateinit var pulseRing: View
    private lateinit var pulseRing2: View
    private var pulseAnimator:  ObjectAnimator? = null
    private var pulseAnimator2: ObjectAnimator? = null

    companion object {
        init { System.loadLibrary("myrror_android") }
    }

    private external fun nativeStartStream(
        surface: android.view.Surface,
        width:   Int, height: Int,
        useHevc: Boolean, port: Int
    ): Boolean

    private external fun nativeStopStream()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        setContentView(R.layout.activity_main)

        overlay    = findViewById(R.id.overlay)
        pulseRing  = findViewById(R.id.pulseRing)
        pulseRing2 = findViewById(R.id.pulseRing2)

        startPulseAnimation()
        hideSystemBars()

        val surfaceView = findViewById<SurfaceView>(R.id.surfaceView)
        surfaceView.holder.addCallback(this)
        surfaceView.holder.setFixedSize(streamWidth, streamHeight)
    }

    private fun makePulse(target: View, startDelay: Long): ObjectAnimator {
        val scaleX = PropertyValuesHolder.ofFloat(View.SCALE_X, 1f, 2.6f)
        val scaleY = PropertyValuesHolder.ofFloat(View.SCALE_Y, 1f, 2.6f)
        val alpha  = PropertyValuesHolder.ofFloat(View.ALPHA,   0.7f, 0f)
        return ObjectAnimator.ofPropertyValuesHolder(target, scaleX, scaleY, alpha).apply {
            duration         = 1800
            this.startDelay  = startDelay
            repeatCount      = ObjectAnimator.INFINITE
            repeatMode       = ValueAnimator.RESTART
        }
    }

    private fun startPulseAnimation() {
        pulseRing.scaleX  = 1f; pulseRing.scaleY  = 1f; pulseRing.alpha  = 0.7f
        pulseRing2.scaleX = 1f; pulseRing2.scaleY = 1f; pulseRing2.alpha = 0.7f

        pulseAnimator  = makePulse(pulseRing,  0).also   { it.start() }
        pulseAnimator2 = makePulse(pulseRing2, 900).also { it.start() }
    }

    fun onFirstFrame() {
        runOnUiThread {
            pulseAnimator?.cancel()
            pulseAnimator2?.cancel()
            overlay.animate()
                .alpha(0f)
                .setDuration(400)
                .withEndAction { overlay.visibility = View.GONE }
                .start()
        }
    }

    fun onStreamStopped() {
        runOnUiThread {
            // Volver a mostrar el overlay de espera
            overlay.animate().cancel()
            overlay.alpha = 1f
            overlay.visibility = View.VISIBLE
            findViewById<TextView>(R.id.statusMsg).text = "Esperando conexión..."
            startPulseAnimation()
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    private fun hideSystemBars() {
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    override fun onDestroy() {
        super.onDestroy()
        pulseAnimator?.cancel()
        pulseAnimator2?.cancel()
        if (streaming) { nativeStopStream(); streaming = false }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val ok = nativeStartStream(holder.surface, streamWidth, streamHeight, useHevc, udpPort)
        if (ok) {
            streaming = true
            Toast.makeText(this, "Esperando stream en puerto $udpPort...", Toast.LENGTH_SHORT).show()
        } else {
            AlertDialog.Builder(this)
                .setTitle("Error")
                .setMessage("No se pudo iniciar el decoder.\nVerificá que la tablet soporta H.265 por hardware.")
                .setPositiveButton("Cerrar") { _, _ -> finish() }
                .show()
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (streaming) { nativeStopStream(); streaming = false }
    }
}
