package me.devcexx.rfapp.util

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.activity.ComponentActivity
import androidx.annotation.RequiresPermission
import kotlin.coroutines.Continuation
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

class BluetoothDeviceBonder(private val activity: ComponentActivity) {
    private var pendingCoroutine: Continuation<Boolean>? = null
    private var pendingDeviceBond: BluetoothDevice? = null

    private var bluetoothReceiver: BroadcastReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (BluetoothDevice.ACTION_BOND_STATE_CHANGED == intent.action) {
                val device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                    ?: throw IllegalStateException("Did not received a bluetooth device")

                if (device != pendingDeviceBond) {
                    return
                }

                val bondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)
                if (bondState == -1) {
                    throw IllegalStateException("Received event did not include current bond state")
                }

                if (bondState != BluetoothDevice.BOND_BONDING) {
                    activity.unregisterReceiver(this)
                    pendingCoroutine?.resume(bondState == BluetoothDevice.BOND_BONDED)
                }
            }
        }

    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    suspend fun bondDevice(device: BluetoothDevice): Boolean {
        if (device.bondState == BluetoothDevice.BOND_BONDED) {
            return true
        }

        return suspendCoroutine {
            pendingCoroutine = it
            pendingDeviceBond = device
            activity.registerReceiver(bluetoothReceiver, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED))
            device.createBond()
        }
    }
}