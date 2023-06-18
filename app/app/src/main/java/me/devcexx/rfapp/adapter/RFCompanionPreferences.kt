package me.devcexx.rfapp.adapter

import android.content.SharedPreferences

class RFCompanionPreferences(private val sharedPreferences: SharedPreferences, private val onPreferenceChanged: (() -> Unit)?) {
    class EvictableLazy<T>(private val f: () -> T) {
        sealed class Value<out T> {
            object Uninitialized: Value<Nothing>()
            data class Initialized<T>(val value: T): Value<T>()
        }
        private var internalValue: Value<T> = Value.Uninitialized

        val value: T get() = when(val v = internalValue) {
            is Value.Uninitialized -> {
                val v = Value.Initialized(f())
                internalValue = v

                v.value
            }

            is Value.Initialized -> v.value
        }

        fun evict() {
            internalValue = Value.Uninitialized
        }
    }

    companion object {
        private const val PREF_NAME_DEVICE_ADDR = "device_address"
    }

    private val internalPrefBluetoothDeviceAddress = EvictableLazy {
        sharedPreferences.getString(PREF_NAME_DEVICE_ADDR, null)
    }

    val bluetoothDeviceAddress: String? get() = internalPrefBluetoothDeviceAddress.value
    fun updateBluetoothDeviceAddress(address: String) {
        sharedPreferences.edit()
            .putString(PREF_NAME_DEVICE_ADDR, address)
            .apply()
        internalPrefBluetoothDeviceAddress.evict()
        onPreferenceChanged?.invoke()
    }
}