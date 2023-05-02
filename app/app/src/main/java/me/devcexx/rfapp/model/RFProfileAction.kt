package me.devcexx.rfapp.model

import androidx.car.app.model.CarColor
import me.devcexx.rfapp.adapter.ESP32Adapter

data class RFProfileAction(val text: String,
                           val rfAction: suspend (ESP32Adapter.ConnectedESP32Adapter) -> ESP32Adapter.CommandExecutionResult,
                           val actionColor: Long,
                           val carColor: CarColor)