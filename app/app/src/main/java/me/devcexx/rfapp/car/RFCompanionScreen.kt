package me.devcexx.rfapp.car

import android.util.Log
import androidx.car.app.CarContext
import androidx.car.app.CarToast
import androidx.car.app.Screen
import androidx.car.app.model.Action
import androidx.car.app.model.ActionStrip
import androidx.car.app.model.CarColor
import androidx.car.app.model.CarIcon
import androidx.car.app.model.GridItem
import androidx.car.app.model.GridTemplate
import androidx.car.app.model.ItemList
import androidx.car.app.model.Template
import androidx.core.graphics.drawable.IconCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import me.devcexx.rfapp.R
import me.devcexx.rfapp.RFCompanionApplication.Companion.rfApplication
import me.devcexx.rfapp.adapter.ESP32Adapter
import me.devcexx.rfapp.model.RFProfile
import me.devcexx.rfapp.model.RFProfileAction

class RFCompanionScreen(carContext: CarContext) : Screen(carContext) {
    companion object {
        private const val TAG = "RFCompanionScreen"
    }

    init {
        updateProfile(rfApplication.profileRepository.getAllProfiles().first())
    }

    data class ProfileActionElement(val action: RFProfileAction, var loading: Boolean)

    private val actionIconBase = IconCompat.createWithResource(carContext, R.drawable.flash_circle)
    private val changeProfileIcon = IconCompat.createWithResource(carContext, R.drawable.swap_horizontal)

    private lateinit var selectedProfile: RFProfile
    private lateinit var actionElements: List<ProfileActionElement>

    private fun updateProfile(profile: RFProfile) {
        this.selectedProfile = profile
        actionElements = profile.actions.map { ProfileActionElement(it, false) }
        invalidate()
    }

    override fun onGetTemplate(): Template {
        val itemList = ItemList.Builder()
        val anyElementLoading = actionElements.any { it.loading }

        actionElements.forEach { element ->
            val itemBuilder = GridItem.Builder()
                .setTitle(element.action.text)
                .setLoading(element.loading)


            if (!element.loading) {
                itemBuilder.setImage(CarIcon.Builder(actionIconBase)
                    .setTint(CarColor.createCustom(
                        element.action.actionColor.toInt(),
                        element.action.actionColor.toInt())
                    ).build())

                if (!anyElementLoading) {
                    itemBuilder.setOnClickListener { this.runProfileAction(element) }
                }
            }

            itemList.addItem(itemBuilder.build())
        }

        return GridTemplate.Builder()
            .setTitle("RF Companion - ${selectedProfile.name}")
            .setLoading(false)
            .setSingleList(itemList.build())
            .setActionStrip(ActionStrip.Builder()
                .addAction(Action.Builder()
                    .setIcon(CarIcon.Builder(changeProfileIcon).build())
                    .setOnClickListener(this::onChangeProfileClick)
                    .build())
                .build())
            .build()
    }


    private fun onChangeProfileClick() {
        screenManager.pushForResult(SelectProfileScreen(carContext)) {
            if (it as? RFProfile != null) {
               updateProfile(it)
            }
        }
    }

    private suspend fun runDeferredBluetoothInteractiveRequest(action: ProfileActionElement) {
        action.loading = true
        invalidate()

        try {
            Log.i(TAG, "Begin requesting permissions...")
            Log.i(TAG, "Begin connecting...")
            rfApplication.esp32Adapter.connectAndRun {
                Log.i(TAG, "Connection success. Sending command...")
                when(action.action.rfAction(it)) {
                    ESP32Adapter.CommandExecutionResult.OK -> CarToast.makeText(carContext, R.string.rf_operation_success, CarToast.LENGTH_SHORT).show()
                    ESP32Adapter.CommandExecutionResult.BUSY -> CarToast.makeText(carContext, R.string.rf_operation_busy, CarToast.LENGTH_SHORT).show()
                    ESP32Adapter.CommandExecutionResult.INVALID_COMMAND -> CarToast.makeText(carContext, R.string.rf_operation_invalid_command, CarToast.LENGTH_SHORT).show()
                }
            }
        } catch (ex: Exception) {
            Log.e(TAG, "Error communicating with the device", ex)
            CarToast.makeText(carContext, carContext.getString(R.string.rf_operation_error, ex.message), CarToast.LENGTH_SHORT).show()
        } finally {
            action.loading = false
            invalidate()
        }
    }

    private fun runProfileAction(action: ProfileActionElement) {
        CoroutineScope(Dispatchers.Main).launch {
            runDeferredBluetoothInteractiveRequest(action)
        }
    }
}