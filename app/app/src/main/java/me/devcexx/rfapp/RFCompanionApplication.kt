package me.devcexx.rfapp

import android.app.Activity
import android.app.Application
import android.bluetooth.BluetoothManager
import android.os.StrictMode
import android.os.StrictMode.VmPolicy
import android.util.Log
import android.widget.Toast
import androidx.car.app.Screen
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import me.devcexx.rfapp.adapter.ProfileRepository
import me.devcexx.rfapp.adapter.RFCompanionAdapter
import me.devcexx.rfapp.adapter.RFCompanionPreferences


class RFCompanionApplication: Application() {
    companion object {
        val Activity.rfApplication get() = this.application as RFCompanionApplication
        val Screen.rfApplication get() = this.carContext.applicationContext as RFCompanionApplication
        private const val TAG = "RFCompanionApplication"
        private const val PREF_NAME = "AppPreferences"
    }

    lateinit var rfCompanionAdapter: RFCompanionAdapter
    lateinit var rfCompanionPreferences: RFCompanionPreferences
    val profileRepository = ProfileRepository()

    override fun onCreate() {
        super.onCreate()
        StrictMode.setVmPolicy(
            VmPolicy.Builder()
                .detectLeakedClosableObjects()
                .penaltyLog()
                .build()
        )

        rfCompanionPreferences = RFCompanionPreferences(getSharedPreferences(PREF_NAME, 0)) {
            rfCompanionAdapter.disconnect()
        }

        rfCompanionAdapter = RFCompanionAdapter(this,
            getSystemService(BLUETOOTH_SERVICE) as BluetoothManager,
            rfCompanionPreferences
        )

        CoroutineScope(Dispatchers.Main).launch {
            if (rfCompanionPreferences.bluetoothDeviceAddress != null) {
                try {
                    rfCompanionAdapter.ensureConnected()
                    Toast.makeText(applicationContext, getString(R.string.app_connection_success), Toast.LENGTH_SHORT).show()
                } catch (_: SecurityException) {

                } catch (e: Exception) {
                    Log.e(TAG, "Error connecting to the device", e)
                    Toast.makeText(applicationContext, getString(R.string.app_connection_failed), Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    override fun onTerminate() {
        super.onTerminate()
        rfCompanionAdapter.disconnect()
    }
}