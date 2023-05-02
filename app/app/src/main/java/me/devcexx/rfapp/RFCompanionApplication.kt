package me.devcexx.rfapp

import android.app.Activity
import android.app.Application
import android.bluetooth.BluetoothManager
import android.util.Log
import android.widget.Toast
import androidx.car.app.Screen
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import me.devcexx.rfapp.adapter.ESP32Adapter
import me.devcexx.rfapp.adapter.ProfileRepository

class RFCompanionApplication: Application() {
    companion object {
        val Activity.rfApplication get() = this.application as RFCompanionApplication
        val Screen.rfApplication get() = this.carContext.applicationContext as RFCompanionApplication
        private const val TAG = "RFCompanionApplication"
    }

    lateinit var esp32Adapter: ESP32Adapter
    val profileRepository = ProfileRepository()

    override fun onCreate() {
        super.onCreate()
        esp32Adapter = ESP32Adapter(getSystemService(BLUETOOTH_SERVICE) as BluetoothManager)

        CoroutineScope(Dispatchers.Main).launch {
            try {
                esp32Adapter.connectAndRun {
                    Toast.makeText(applicationContext, getString(R.string.app_connection_success), Toast.LENGTH_SHORT).show()
                }
            } catch (_: SecurityException) {
            } catch (e: Exception) {
                Log.e(TAG, "Error connecting to the device", e)
                Toast.makeText(applicationContext, getString(R.string.app_connection_failed), Toast.LENGTH_SHORT).show()
            }
        }
    }

    override fun onTerminate() {
        super.onTerminate()
        esp32Adapter.disconnect()
    }
}