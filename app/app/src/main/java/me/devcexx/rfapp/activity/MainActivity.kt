package me.devcexx.rfapp.activity

import android.Manifest
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.Menu
import android.view.MenuItem
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
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
import me.devcexx.rfapp.util.PermissionRequester
import me.devcexx.rfapp.R
import me.devcexx.rfapp.RFCompanionApplication.Companion.rfApplication
import me.devcexx.rfapp.adapter.BluetoothException
import me.devcexx.rfapp.adapter.RFCompanionAdapter
import me.devcexx.rfapp.model.RFProfileAction
import me.devcexx.rfapp.ui.theme.GarageDoorTheme

class MainActivity : ComponentActivity() {
    companion object {
        private const val TAG: String = "MainActivity"
    }


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

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main_activity_menu, menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == R.id.main_activity_menu_item_settings) {
            openBluetoothOptionsActivity()
            return true
        }
        return false
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

    private val permissionRequester = PermissionRequester(this)

    suspend fun runDeferredBluetoothInteractiveRequest(
        updateSpinnerVisibility: (Boolean) -> Unit,
        commandFn: suspend (RFCompanionAdapter) -> Unit
    ) {
        updateSpinnerVisibility(true)

        try {
            Log.i(TAG, "Begin requesting permissions...")
            if (!permissionRequester.requestPermission(Manifest.permission.BLUETOOTH_CONNECT)) {
                Log.w(TAG, "Permission denied! Aborted")
                Toast.makeText(this, R.string.rf_permission_denied, Toast.LENGTH_SHORT).show()
                return
            }
            Log.i(TAG, "Begin connecting...")
            commandFn(rfApplication.rfCompanionAdapter)
        } catch (ex: BluetoothException) {
            Log.e(TAG, "Bluetooth error", ex)
            Toast.makeText(this, getString(R.string.rf_operation_error, ex.message), Toast.LENGTH_SHORT).show()
        } finally {
            updateSpinnerVisibility(false)
        }
    }

    fun openBluetoothOptionsActivity() {
        startActivity(Intent(this, BluetoothOptionsActivity::class.java))
    }

    fun runProfileAction(
        updateSpinnerVisibility: (Boolean) -> Unit,
        commandFn: suspend (RFCompanionAdapter) -> Unit
    ) {
        if (rfApplication.rfCompanionPreferences.bluetoothDeviceAddress == null) {
            openBluetoothOptionsActivity()
            return
        }

        CoroutineScope(Dispatchers.Main).launch {
            runDeferredBluetoothInteractiveRequest(updateSpinnerVisibility, commandFn)
        }
    }
}