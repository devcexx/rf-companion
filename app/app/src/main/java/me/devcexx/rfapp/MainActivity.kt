package me.devcexx.rfapp

import android.Manifest
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.Crossfade
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.AbsoluteRoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import me.devcexx.rfapp.RFCompanionApplication.Companion.rfApplication
import me.devcexx.rfapp.adapter.ESP32Adapter
import me.devcexx.rfapp.model.RFProfileAction
import me.devcexx.rfapp.ui.theme.GarageDoorTheme
import kotlin.coroutines.Continuation
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

class MainActivity : ComponentActivity() {
    companion object {
        private const val TAG: String = "MainActivity"
    }

    private var requestPermissionContinuation: Continuation<Boolean>? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            var spinnerVisible by remember {
                mutableStateOf(false)
            }

            val updateSpinnerVisible: (Boolean) -> Unit = { visible ->
                spinnerVisible = visible
            }

            GarageDoorTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    Box(modifier = Modifier
                        .fillMaxSize()) {
                        Content(updateSpinnerVisibility = updateSpinnerVisible)
                        Spinner(spinnerVisible = spinnerVisible)
                    }
                }
            }
        }
    }

    @Composable
    fun Spinner(spinnerVisible: Boolean) {
        AnimatedVisibility(visible = spinnerVisible, enter = fadeIn(), exit = fadeOut()) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color(0, 0, 0, 127))) {

                Box(modifier = Modifier
                    .width(Dp(70.0f))
                    .height(Dp(70.0f))) {
                    CircularProgressIndicator(modifier = Modifier.fillMaxSize())
                }
            }
        }
    }

    @Composable
    fun ProfileActionButton(updateSpinnerVisibility: (Boolean) -> Unit, profileAction: RFProfileAction) {
        Button(
            onClick = { runProfileAction(updateSpinnerVisibility, profileAction.rfAction) },
            modifier = Modifier.fillMaxWidth(0.75f),
            elevation = ButtonDefaults.elevatedButtonElevation(),
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 25.dp),
            shape = AbsoluteRoundedCornerShape(8.dp),
        colors = ButtonDefaults.buttonColors(containerColor = Color(profileAction.actionColor))) {
            Text(text = profileAction.text)
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun Content(updateSpinnerVisibility: (Boolean) -> Unit) {
        val profiles = rfApplication.profileRepository.getAllProfiles()
        var selectedProfile by remember {
            mutableStateOf(profiles.first())
        }

        Box(modifier = Modifier.fillMaxSize()) {
            Crossfade(targetState = selectedProfile) { selectedProfile ->
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterVertically), modifier = Modifier
                .fillMaxSize()
                .padding(24.dp)) {
                   selectedProfile.actions.forEach {  profileAction ->
                       ProfileActionButton(updateSpinnerVisibility = updateSpinnerVisibility, profileAction = profileAction)
                   }
               }
            }

            var expanded by remember { mutableStateOf(false) }
            ExposedDropdownMenuBox(
                expanded = expanded,
                onExpandedChange = {
                    expanded = !expanded
                },
                modifier = Modifier.align(Alignment.BottomStart
                )
            ) {
                OutlinedTextField(
                    readOnly = true,
                    value = selectedProfile.name,
                    onValueChange = { },
                    label = { Text(getString(R.string.activity_main_profile)) },
                    trailingIcon = {
                        ExposedDropdownMenuDefaults.TrailingIcon(
                            expanded = expanded
                        )
                    },
                    colors = ExposedDropdownMenuDefaults.textFieldColors(),
                    modifier = Modifier
                        .menuAnchor()
                        .fillMaxWidth()
                )
                ExposedDropdownMenu(
                    expanded = expanded,
                    onDismissRequest = {
                        expanded = false
                    }
                ) {
                    profiles.forEach { profile ->
                        DropdownMenuItem(
                            onClick = {
                                selectedProfile = profile
                                expanded = false
                            },
                            text = {  Text(text = profile.name) }
                        )
                    }
                }
            }
        }


    }

    private val activityResultLauncher = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        requestPermissionContinuation?.resume(granted)
    }

    fun onHomeGarageEnterButton(updateSpinnerVisibility: (Boolean) -> Unit) {
        runProfileAction(updateSpinnerVisibility, ESP32Adapter.ConnectedESP32Adapter::sendHomeGarageEnterRequest)
    }

    fun onHomeGarageExitButton(updateSpinnerVisibility: (Boolean) -> Unit) {
        runProfileAction(updateSpinnerVisibility, ESP32Adapter.ConnectedESP32Adapter::sendHomeGarageExitRequest)
    }

    suspend fun requestPermissionDeferred(permission: String) = suspendCoroutine { cont ->
        requestPermissionContinuation = cont
        activityResultLauncher.launch(permission)
    }

    suspend fun runDeferredBluetoothInteractiveRequest(
        updateSpinnerVisibility: (Boolean) -> Unit,
        commandFn: suspend (ESP32Adapter.ConnectedESP32Adapter) -> ESP32Adapter.CommandExecutionResult
    ) {
        updateSpinnerVisibility(true)

        try {
            Log.i(TAG, "Begin requesting permissions...")
            if (!requestPermissionDeferred(Manifest.permission.BLUETOOTH_CONNECT)) {
                Log.w(TAG, "Permission denied! Aborted")
                Toast.makeText(this, R.string.rf_permission_denied, Toast.LENGTH_SHORT).show()
                return
            }
            Log.i(TAG, "Begin connecting...")
            rfApplication.esp32Adapter.connectAndRun {
                Log.i(TAG, "Connection success. Sending command...")
                when(commandFn(it)) {
                    ESP32Adapter.CommandExecutionResult.OK -> Toast.makeText(this, R.string.rf_operation_success, Toast.LENGTH_SHORT).show()
                    ESP32Adapter.CommandExecutionResult.BUSY -> Toast.makeText(this, R.string.rf_operation_busy, Toast.LENGTH_SHORT).show()
                    ESP32Adapter.CommandExecutionResult.INVALID_COMMAND -> Toast.makeText(this, R.string.rf_operation_invalid_command, Toast.LENGTH_SHORT).show()
                }
            }
        } catch (ex: Exception) {
            Log.e(TAG, "Error communicating with the device", ex)
            Toast.makeText(this, getString(R.string.rf_operation_error, ex.message), Toast.LENGTH_SHORT).show()
        } finally {
            updateSpinnerVisibility(false)
        }
    }

    fun runProfileAction(
        updateSpinnerVisibility: (Boolean) -> Unit,
        commandFn: suspend (ESP32Adapter.ConnectedESP32Adapter) -> ESP32Adapter.CommandExecutionResult
    ) {
        CoroutineScope(Dispatchers.Main).launch {
            runDeferredBluetoothInteractiveRequest(updateSpinnerVisibility, commandFn)
        }
    }
}