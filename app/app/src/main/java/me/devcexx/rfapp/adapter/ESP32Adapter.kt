package me.devcexx.rfapp.adapter

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.withContext
import java.io.EOFException
import java.io.IOException
import java.time.Instant
import java.util.UUID
import java.util.concurrent.TimeoutException
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.time.Duration
import kotlin.time.DurationUnit
import kotlin.time.toDuration

class ESP32Adapter(bluetoothManager: BluetoothManager) {
    private val bluetoothDevice = bluetoothManager.adapter.getRemoteDevice(DEVICE_MAC_ADDRESS)
    private var socket: BluetoothSocket? = null
    private var lastCheckAliveTime: Instant = Instant.EPOCH
    private val connectionMutex: Mutex = Mutex(false)

    companion object {
        private val SPP_SERVICE_ID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
        private const val DEVICE_MAC_ADDRESS: String = "94:E6:86:3D:73:8A"
        private const val LOG_TAG = "ESP32Adapter"
        private val CMD_RESPONSE_TIMEOUT = 10.toDuration(DurationUnit.SECONDS)
        private const val MIN_SECONDS_BETWEEN_ALIVE_PROBES = 10
    }

    enum class CommandExecutionResult {
        OK,
        BUSY,
        INVALID_COMMAND
    }

    class ConnectedESP32Adapter(
        private val adapter: ESP32Adapter,
        private val socket: BluetoothSocket
    ) {
        private suspend fun sendCommand(command: String): CommandExecutionResult {
            adapter.writeCommand(socket, command)
            return when (val msg = adapter.readMessage(socket, CMD_RESPONSE_TIMEOUT)) {
                "OK" -> CommandExecutionResult.OK
                "ERR_BUSY" -> CommandExecutionResult.BUSY
                "ERR_INV_CMD" -> CommandExecutionResult.INVALID_COMMAND
                else -> throw RuntimeException("Unknown error received from device: $msg")
            }
        }

        suspend fun sendHomeGarageEnterRequest(): CommandExecutionResult =
            sendCommand("home-garage-enter")

        suspend fun sendHomeGarageExitRequest(): CommandExecutionResult =
            sendCommand("home-garage-exit")

        suspend fun sendParentsGarageLeftButtonRequest(): CommandExecutionResult =
            sendCommand("parents-garage-left")

        suspend fun sendParentsGarageRightButtonRequest(): CommandExecutionResult =
            sendCommand("parents-garage-right")
    }

    private suspend fun writeCommand(sock: BluetoothSocket, cmd: String) {
        withContext(Dispatchers.IO) {
            try {
                sock.outputStream.write("$cmd\r".encodeToByteArray())
                sock.outputStream.flush()
            } catch (e: IOException) {
                disconnectSocket(sock)
                throw e
            }
        }
    }

    private fun syncReadMessage(socket: BluetoothSocket): String {
        val b = StringBuilder()
        val inputStream = socket.inputStream
        var keepGoing = true
        var ch: Int
        while (keepGoing) {
            ch = inputStream.read()
            Log.i(LOG_TAG, "${ch.toChar()} ($ch)")
            if (ch == -1) throw EOFException()
            if (ch.toChar() == '\n') {
                keepGoing = false
            } else {
                b.append(ch.toChar())
            }
        }
        return b.toString()
    }

    private suspend fun readMessage(sock: BluetoothSocket, timeout: Duration): String {
        return withContext(Dispatchers.IO) {
            val disconnectionForced = AtomicBoolean(false)

            val timeoutWatchdog = async {
                delay(timeout)
                Log.w(LOG_TAG, "Read message timed-out after $timeout")
                disconnectionForced.set(true)
                disconnectSocket(sock)
            }
            val message = try {
                syncReadMessage(sock)
            } catch (e: IOException) {
                if (disconnectionForced.get()) {
                    throw TimeoutException("Didn't get a response after $timeout")
                } else {
                    throw e
                }
            } finally {
                timeoutWatchdog.cancelAndJoin()
            }
            message
        }
    }

    private suspend fun checkAlive(sock: BluetoothSocket) {
        val now = Instant.now()
        if (now.epochSecond - lastCheckAliveTime.epochSecond > MIN_SECONDS_BETWEEN_ALIVE_PROBES) {
            lastCheckAliveTime = now
            writeCommand(sock, "nop");
        }
    }

    @SuppressLint("MissingPermission")
    private suspend fun connectSocket(): BluetoothSocket = withContext(Dispatchers.IO) {
            val socket = bluetoothDevice.createRfcommSocketToServiceRecord(SPP_SERVICE_ID)
            socket.connect()
            socket
        }

    private fun disconnectSocket(socket: BluetoothSocket) {
        socket.outputStream.close()
        socket.inputStream.close()
        socket.close()
        lastCheckAliveTime = Instant.EPOCH
    }

    fun disconnect() {
        socket?.let(this::disconnectSocket)
        socket = null
    }

    private suspend fun ensureConnected(): BluetoothSocket {
        return try {
            connectionMutex.lock()
            this.socket?.let { sock ->
                if (sock.isConnected) {
                    try {
                        checkAlive(sock)
                        sock
                    } catch (e: IOException) {
                        disconnectSocket(sock)
                        connectSocket()
                    }
                } else {
                    connectSocket()
                }
            } ?: connectSocket()
        } finally {
            connectionMutex.unlock()
        }
    }

    suspend fun connectAndRun(f: suspend (ConnectedESP32Adapter) -> Unit) {
        val socket = ensureConnected()
        this.socket = socket
        f(ConnectedESP32Adapter(this, socket))
    }
}