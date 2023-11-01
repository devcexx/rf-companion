package me.devcexx.rfapp.adapter

import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.os.Handler
import android.util.Log
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeout
import me.devcexx.rfapp.RFCompanionApplication
import java.util.UUID
import kotlin.coroutines.Continuation
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine
import kotlin.reflect.KClass
import kotlin.time.Duration
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds

@Suppress("MissingPermission")
class RFCompanionAdapter(
    private val application: RFCompanionApplication,
    private val bluetoothManager: BluetoothManager,
    private val preferences: RFCompanionPreferences
) {
    data class ChrWriteListener(val chrId: UUID, val result: CancellableContinuation<Int>)
    data class NotificationListener(val chrId: UUID, val recv: CancellableContinuation<List<Byte>>)

    sealed class State {
        object Disconnected : State()
        data class Connecting(val gatt: BluetoothGatt) : State()

        // This state means connected + services discovered!
        data class Connected(
            val gatt: BluetoothGatt,
            val disconnectionIdleJob: CancellableContinuation<Unit>?,
            val chrWriteListener: ChrWriteListener?,
            val notifListener: NotificationListener?
        ) : State()

        data class Disconnecting(val gatt: BluetoothGatt) : State()
    }

    private data class StateTransitionWait(
        val targetStates: List<KClass<out State>>,
        val cont: Continuation<State>
    )

    companion object {
        private val SVC_RF_COMPANION = UUID.fromString("4ffaac15-2fc8-15a9-7347-5b4ed3a56ca8")
        private val CHR_ANTENNA_STATE = UUID.fromString("9f5650ee-5756-5b95-5a48-e9764d33f3a0")
        private val CHR_SEND_RF = UUID.fromString("421a3acb-9a83-f8bd-1c4f-7469e1a15954")
        private val TAG = RFCompanionAdapter::class.simpleName

        private val CONNECTION_TIMEOUT = 10.seconds
        private val DEVICE_INACTIVITY_MAX_TIME = 2.minutes
        private val WRITE_TIMEOUT = 2.seconds
        private val SENDRF_RESPONSE_TIMEOUT = 10.seconds
    }

    enum class SendRfCommandResult(val code: Byte) {
        PROCESSING(1),
        ANTENNA_BUSY(2),
        COMPLETED(3),
        UNKNOWN_SIGNAL(4);

        companion object {
            fun fromCode(code: Byte) = SendRfCommandResult.values().find { it.code == code }
                ?: throw IllegalArgumentException("Invalid SendRF response code $code")
        }
    }

    enum class SendRfStoredCode(val code: Byte) {
        HOME_1_GARAGE_EXIT(1),
        HOME_1_GARAGE_ENTER(2),
        PARENTS_GARAGE_A(3),
        PARENTS_GARAGE_B(4),
        TESLA_CHARGER_OPENER(5),
        HOME_2_GARAGE_EXIT(6),
        HOME_2_GARAGE_ENTER(7),
    }

    var state: State = State.Disconnected
    private val handler = Handler(application.mainLooper)
    private val transitionChangeWaitList = mutableListOf<StateTransitionWait>()
    private val gattCallback = GattCallback()

    inner class GattCallback : BluetoothGattCallback() {
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            handler.post {
                val bytes = value.toList()
                if (characteristic.uuid == CHR_ANTENNA_STATE) {
                    Log.i(TAG, "Antenna state change. Busy? ${bytes.first() == 1.toByte()}")
                    return@post
                }

                val state = when (val st = state) {
                    is State.Connected -> st
                    else -> {
                        Log.e(TAG, "Received a notification while not connected??")
                        return@post
                    }
                }

                Log.i(TAG, "Current state: $state")

                val listener = state.notifListener
                if (listener == null || listener.chrId != characteristic.uuid) {
                    Log.w(
                        TAG,
                        "Received an unexpected notification right now from chr id ${characteristic.uuid}"
                    )
                    return@post
                }

                updateCurrentState(state.copy(notifListener = null))
                listener.recv.resume(bytes)
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            Log.i(TAG, "Write operation terminated with status code $status")
            handler.post {
                val state = when (val st = state) {
                    is State.Connected -> st
                    else -> {
                        Log.e(TAG, "Characteristic write finished while not connected??")
                        return@post
                    }
                }
                Log.i(TAG, "Chr write state: $state")

                val listener = state.chrWriteListener
                if (listener == null || listener.chrId != characteristic.uuid) {
                    Log.w(
                        TAG,
                        "Received a characteristic write termination condition from chr id ${characteristic.uuid}"
                    )
                    return@post
                }

                updateCurrentState(state.copy(chrWriteListener = null))
                listener.result.resume(status)
            }
        }

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            handler.post {
                Log.i(TAG, "Connection state changed: $newState")

                when (newState) {
                    BluetoothProfile.STATE_CONNECTED -> {
                        Log.i("RfApp", "Discovering services")
                        gatt.discoverServices()
                    }

                    BluetoothProfile.STATE_DISCONNECTING -> {
                        transitionState(State.Disconnecting(gatt))
                    }

                    BluetoothProfile.STATE_DISCONNECTED -> {
                        transitionState(State.Disconnected)
                        gatt.close()
                    }
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            // Operations of writing characteristic on a just connected device seems not to be transmitting properly? adding just a delay for trying to fix that.
            Thread.sleep(200)
            handler.post {
                Log.i(TAG, "Services discovered with status $status")

                when (val state = state) {
                    is State.Connecting -> {
                        val service = gatt.getService(SVC_RF_COMPANION)
                        if (service == null) {
                            Log.e(
                                TAG,
                                "Couldn't find required service $SVC_RF_COMPANION on remote device"
                            )
                            gatt.disconnect()
                            return@post
                        }

                        val sendRfChr = service.getCharacteristic(CHR_SEND_RF)
                        if (sendRfChr == null) {
                            Log.e(TAG, "Couldn't find required characteristic $CHR_SEND_RF")
                            gatt.disconnect()
                            return@post
                        }

                        val antennaChr = service.getCharacteristic(CHR_ANTENNA_STATE)
                        if (antennaChr == null) {
                            Log.e(TAG, "Couldn't find required characteristic $CHR_ANTENNA_STATE")
                            gatt.disconnect()
                            return@post
                        }

                        gatt.setCharacteristicNotification(sendRfChr, true)
                        gatt.setCharacteristicNotification(antennaChr, true)
                        Log.i(
                            TAG,
                            "Enabled notifications for characteristic $CHR_SEND_RF and $CHR_ANTENNA_STATE"
                        )

                        transitionState(State.Connected(gatt, null, null, null))
                    }

                    else -> Log.e(TAG, "Services discovered on unexpected state: $state")
                }
            }
        }
    }

    @OptIn(DelicateCoroutinesApi::class)
    private fun resetDeviceInactivity() {
        GlobalScope.launch(context = Dispatchers.Main) {
            val state = when (val st = state) {
                is State.Connected -> st
                else -> {
                    Log.w(TAG, "Cannot set device inactivity timer as adapter is not connected")
                    return@launch
                }
            }

            state.disconnectionIdleJob?.cancel()
            suspendCancellableCoroutine {
                updateCurrentState(state.copy(disconnectionIdleJob = it))
                launch(context = Dispatchers.Main) {
                    delay(DEVICE_INACTIVITY_MAX_TIME)
                    disconnectAndAwait()
                    Log.i(TAG, "Device disconnected due to inactivity")
                }
            }
        }
    }

    private fun cleanupState(prevState: State, newState: State) {
        val reason = when (newState) {
            State.Disconnected -> BluetoothException("Remote device disconnected")
            else -> null
        }

        Log.i(TAG, "Cleaning up state: Prev state is $prevState")
        when (prevState) {
            is State.Connected -> {
                Log.i(TAG, "Cancelling pending listeners...")
                prevState.disconnectionIdleJob?.cancel()
                prevState.chrWriteListener?.result?.cancel(reason)
                prevState.notifListener?.recv?.cancel(reason)
            }

            else -> {}
        }
    }

    private fun updateCurrentState(updatedState: State) {
        Log.i(TAG, "Updating current state to $updatedState")
        val state = this.state
        if (state::class.isInstance(updatedState)) {
            this.state = updatedState
        } else {
            throw IllegalStateException("Current state is $state but requested state is different $updatedState and cannot be updated")
        }
    }

    private fun transitionState(newState: State) {
        val prevState = this.state
        Log.i(TAG, "Transition state from $prevState to $newState")
        this.state = newState
        cleanupState(prevState, newState)

        val it = transitionChangeWaitList.iterator()
        while (it.hasNext()) {
            val next: StateTransitionWait = it.next()
            if (next.targetStates.any { it.isInstance(newState) }) {
                it.remove()
                next.cont.resume(newState)
            }
        }
    }

    private suspend fun <A : State> waitForTransition(
        type: KClass<A>,
        maxTimeout: Duration? = null
    ): A {
        return waitForTransition(listOf(type), maxTimeout) as A
    }

    private suspend fun waitForTransition(
        types: List<KClass<out State>>,
        maxTimeout: Duration? = null
    ): State {
        val state = this.state
        return if (types.any { it.isInstance(state) }) {
            state
        } else {
            if (maxTimeout == null) {
                suspendCoroutine {
                    transitionChangeWaitList.add(StateTransitionWait(types, it))
                }
            } else {
                withTimeout(maxTimeout) {
                    suspendCancellableCoroutine {
                        transitionChangeWaitList.add(StateTransitionWait(types, it))
                    }
                }
            }
        }
    }

    private suspend fun writeAndAwait(
        state: State.Connected,
        chr: BluetoothGattCharacteristic,
        value: ByteArray
    ) {
        try {
            withTimeout(WRITE_TIMEOUT) {
                val result = suspendCancellableCoroutine { cont ->
                    updateCurrentState(
                        state.copy(
                            chrWriteListener = ChrWriteListener(
                                chr.uuid,
                                cont
                            )
                        )
                    )
                    val err = state.gatt.writeCharacteristic(
                        chr,
                        value,
                        BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                    )
                    if (err != BluetoothStatusCodes.SUCCESS) {
                        updateCurrentState(state.copy(chrWriteListener = null))
                        state.gatt.disconnect()
                        throw BluetoothException("Error writing characteristic ${chr.uuid}. Bluetooth status code $err")
                    }
                }

                if (result != BluetoothGatt.GATT_SUCCESS) {
                    disconnectAndAwait()
                    throw BluetoothException("Unable to write characteristic ${chr.uuid}. Received response code $state")
                }
            }
        } catch (ex: TimeoutCancellationException) {
            Log.e(TAG, "Write operation timed-out")
            disconnectAndAwait()
            throw BluetoothException("Bluetooth operation timed-out", ex)
        }
    }

    private suspend fun waitForNotification(
        state: State.Connected,
        chr: BluetoothGattCharacteristic,
        timeout: Duration? = null
    ): List<Byte> {
        suspend fun run(): List<Byte> {
            return suspendCancellableCoroutine { cont ->
                updateCurrentState(state.copy(notifListener = NotificationListener(chr.uuid, cont)))
            }
        }

        return if (timeout == null) {
            run()
        } else {
            withTimeout(timeout) {
                run()
            }
        }
    }

    suspend fun checkConnected(): State.Connected {
        val state = state
        if (state is State.Connected) {
            return state
        } else {
            throw BluetoothException("Device is not connected")
        }
    }

    suspend fun ensureConnected(): State.Connected {
        suspend fun waitForConnection(bluetoothDevice: BluetoothDevice): State.Connected {
            try {
                return when (val newState = waitForTransition(
                    listOf(State.Connected::class, State.Disconnected::class),
                    CONNECTION_TIMEOUT
                )) {
                    is State.Connected -> {
                        Log.i(TAG, "Device ${bluetoothDevice.address} connected")
                        resetDeviceInactivity()
                        newState
                    }

                    else -> {
                        throw BluetoothException("Could not connect to the device")
                    }
                }
            } catch (ex: TimeoutCancellationException) {
                Log.i(TAG, "Connection to device ${bluetoothDevice.address} timed-out")
                disconnectAndAwait()
                throw BluetoothException("Connection timed out", ex)
            }
        }

        val state = this.state
        if (state is State.Connected) {
            Log.i(TAG, "Device is already connected, returning")
            return state
        } else if (state is State.Connecting) {
            Log.i(TAG, "Device is already connecting. Wait for connection")
            return waitForConnection(state.gatt.device)
        }

        val bluetoothDevice =
            bluetoothManager.adapter.getRemoteDevice(preferences.bluetoothDeviceAddress)
        Log.i(TAG, "Connecting to ${bluetoothDevice.address}...")

        val gatt = bluetoothDevice.connectGatt(
            application,
            false,
            gattCallback,
            BluetoothDevice.TRANSPORT_LE,
            BluetoothDevice.PHY_LE_1M
        )

        transitionState(State.Connecting(gatt))
        return waitForConnection(bluetoothDevice)
    }

    suspend fun runSendRfCommand(knownCode: SendRfStoredCode) {
        val state = ensureConnected()
        Log.i(TAG, "Invoking SendRF command...")
        val chr = state.gatt.getService(SVC_RF_COMPANION).getCharacteristic(CHR_SEND_RF)

        coroutineScope {
            val notificationJob = async {
                val notification = SendRfCommandResult.fromCode(
                    waitForNotification(
                        checkConnected(),
                        chr,
                        SENDRF_RESPONSE_TIMEOUT
                    ).first()
                )
                if (notification == SendRfCommandResult.PROCESSING) {
                    val terminationNotification = SendRfCommandResult.fromCode(
                        waitForNotification(
                            checkConnected(),
                            chr,
                            SENDRF_RESPONSE_TIMEOUT
                        ).first()
                    )
                    if (terminationNotification == SendRfCommandResult.COMPLETED) {
                        Log.i(TAG, "SendRF command completed successfully")
                    } else {
                        throw BluetoothException("Unexpected result code returned by device. Expected COMPLETED but got ${terminationNotification.name}")
                    }
                } else {
                    throw BluetoothException("Received SendRF command result: ${notification.name}")
                }
            }

            val sendJob = async {
                writeAndAwait(checkConnected(), chr, byteArrayOf(knownCode.code))
            }

            sendJob.await()
            notificationJob.await()
        }
    }

    suspend fun disconnectAndAwait() {
        suspend fun disconnectAndWait(gatt: BluetoothGatt) {
            gatt.disconnect()
            waitForTransition(State.Disconnected::class)
        }

        when (val state = this.state) {
            is State.Connected -> disconnectAndWait(state.gatt)
            is State.Connecting -> {
                // Disconnected state is not fired after a call to this function,
                // So we can disconnect and we suppose device is disconnected after that
                state.gatt.disconnect()
                transitionState(State.Disconnected)
            }

            is State.Disconnecting -> waitForTransition(State.Disconnected::class)
            State.Disconnected -> return
        }

        Log.i(TAG, "Device disconnected successfully")
    }

    fun disconnect() {
        when (val st = state) {
            is State.Connected -> {
                st.gatt.disconnect()
                st.gatt.close()
            }
            is State.Connecting -> st.gatt.disconnect()
            else -> {}
        }
    }
}