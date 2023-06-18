package me.devcexx.rfapp.util

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import androidx.annotation.RequiresPermission
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.time.Duration

class BluetoothScannerManager(private val scanner: BluetoothLeScanner, private val maxUpdateInterval: Duration) {
    data class DiscoveredDevice(val name: String?, val address: String, val rssi: Int, val device: BluetoothDevice)

    private val internalLiveData: MutableLiveData<List<DiscoveredDevice>> = MutableLiveData(listOf())
    val devices: LiveData<List<DiscoveredDevice>> get() = internalLiveData

    private var lastLiveDataUpdate: Long = 0


    private val scanCallback: ScanCallback = object : ScanCallback() {
        private val availableDevices = mutableMapOf<String, DiscoveredDevice>()

        @Suppress("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (callbackType == ScanSettings.CALLBACK_TYPE_ALL_MATCHES || callbackType == ScanSettings.CALLBACK_TYPE_FIRST_MATCH) {
                availableDevices[result.device.address] =
                    DiscoveredDevice(
                        result.device.name,
                        result.device.address,
                        result.rssi,
                        result.device
                    )
            } else {
                availableDevices.remove(result.device.address)
            }
            postLiveData(availableDevices.values.toList().sortedBy { -it.rssi })
        }
    }

    private val elapsedSinceLastUpdate: Long get() = System.currentTimeMillis() - lastLiveDataUpdate
    private val timeForNextUpdate: Long get() = maxUpdateInterval.inWholeMilliseconds - elapsedSinceLastUpdate
    private val shouldUpdate: Boolean get() = timeForNextUpdate < 0
    private var pendingUpdate: Job? = null
    private var currentDiscoveredDevices: List<DiscoveredDevice> = listOf()


    private fun postLiveData(devices: List<DiscoveredDevice>) {
        currentDiscoveredDevices = devices
       if (shouldUpdate) {
           pendingUpdate?.cancel()
           lastLiveDataUpdate = System.currentTimeMillis()
           internalLiveData.postValue(devices)
        } else {
            val waitTime = timeForNextUpdate
            pendingUpdate = CoroutineScope(Dispatchers.Main).launch {
                delay(waitTime)
                internalLiveData.postValue(currentDiscoveredDevices)
                lastLiveDataUpdate = System.currentTimeMillis()
                pendingUpdate = null
            }
        }
    }

    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT])
    fun beginScan() {
        scanner.startScan(scanCallback)
    }

    @Suppress("MissingPermission")
    fun stopScan() {
        scanner.stopScan(scanCallback)
    }
}