package me.devcexx.rfapp.activity

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Divider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import me.devcexx.rfapp.util.BluetoothDeviceBonder
import me.devcexx.rfapp.util.BluetoothScannerManager
import me.devcexx.rfapp.util.PermissionRequester
import me.devcexx.rfapp.R
import me.devcexx.rfapp.RFCompanionApplication.Companion.rfApplication
import me.devcexx.rfapp.ui.theme.GarageDoorTheme
import kotlin.time.Duration.Companion.seconds

class BluetoothOptionsActivity: ComponentActivity() {
    private val permissionRequester = PermissionRequester(this)
    private lateinit var scanner: BluetoothScannerManager
    private val deviceBonder = BluetoothDeviceBonder(this)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        scanner = BluetoothScannerManager(
            (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter.bluetoothLeScanner,
            5.seconds
        )

        setContent {
            var refreshingDevices by remember {
                mutableStateOf(false)
            }

            var connecting by remember {
                mutableStateOf(false)
            }

            val setRefreshingDevices: (Boolean) -> Unit = {
                refreshingDevices = it
            }

            val setConnecting: (Boolean) -> Unit = {
                connecting = it
            }

            GarageDoorTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    BluetoothDeviceList(
                        setRefreshingDevices = setRefreshingDevices,
                        setConnecting = setConnecting
                    )
                    Spinner(spinnerVisible = refreshingDevices || connecting)
                }
            }
        }
    }

    @Composable
    private fun BluetoothDeviceList(
        setRefreshingDevices: (Boolean) -> Unit,
        setConnecting: (Boolean) -> Unit
    ) {
        val devices: List<BluetoothScannerManager.DiscoveredDevice>? by scanner.devices.observeAsState()
        val devs = devices ?: listOf()
        setRefreshingDevices(devs.isEmpty())

        LazyColumn(
            state = rememberLazyListState(),
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight()
        ) {
            items(devs.size) { index ->
                BluetoothDeviceItem(device = devs[index], setConnecting)
            }
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    private fun BluetoothDeviceItem(
        device: BluetoothScannerManager.DiscoveredDevice,
        setConnecting: (Boolean) -> Unit
    ) {
        Column(modifier = Modifier.clickable {
            setConnecting(true)
            onDeviceSelected(device.device, setConnecting)
        }) {
            ListItem(
                leadingContent = {
                    Icon(
                        painter = painterResource(id = R.drawable.baseline_bluetooth_24),
                        contentDescription = null,
                        modifier = Modifier.size(40.dp)
                    )
                },
                headlineText = { Text(text = device.name ?: device.address) },
            )
            Divider()
        }
    }

    @Composable
    private fun Spinner(spinnerVisible: Boolean) {
        AnimatedVisibility(visible = spinnerVisible, enter = fadeIn(), exit = fadeOut()) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color(0, 0, 0, 127))
                    .pointerInput(null) { }) {

                Box(
                    modifier = Modifier
                        .width(Dp(64.0f))
                        .height(Dp(64.0f))
                ) {
                    CircularProgressIndicator(modifier = Modifier.fillMaxSize())
                }
            }
        }
    }

    @Suppress("MissingPermission")
    fun beginScan() {
        CoroutineScope(Dispatchers.Main).launch {
            if (permissionRequester.requestPermission(
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT
                )
            ) {
                scanner.beginScan()
            }
        }
    }

    override fun onPause() {
        super.onPause()
        scanner.stopScan()
    }

    override fun onResume() {
        super.onResume()
        beginScan()
    }

    override fun onDestroy() {
        super.onDestroy()
        scanner.stopScan()
    }

    @Suppress("MissingPermission")
    private fun onDeviceSelected(
        bluetoothDevice: BluetoothDevice,
        setConnecting: (Boolean) -> Unit
    ) {
        CoroutineScope(Dispatchers.Main).launch {
            if (deviceBonder.bondDevice(bluetoothDevice)) {
                rfApplication.rfCompanionPreferences.updateBluetoothDeviceAddress(bluetoothDevice.address)
                finish()
            } else {
                Toast.makeText(
                    this@BluetoothOptionsActivity,
                    R.string.activity_bluetooth_settings_bond_error, Toast.LENGTH_SHORT
                ).show()
            }
            setConnecting(false)
        }
    }
}