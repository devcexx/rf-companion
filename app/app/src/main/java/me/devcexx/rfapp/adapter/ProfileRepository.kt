package me.devcexx.rfapp.adapter

import androidx.car.app.model.CarColor
import me.devcexx.rfapp.model.RFProfile
import me.devcexx.rfapp.model.RFProfileAction

class ProfileRepository {
    companion object {
        private val ALL_PROFILES: List<RFProfile> = listOf(
            RFProfile("Garaje Majadahonda", listOf(
                RFProfileAction("Entrar al garaje", ESP32Adapter.ConnectedESP32Adapter::sendHomeGarageEnterRequest, 0xFF9F00AD, CarColor.RED),
                RFProfileAction("Salir del garaje", ESP32Adapter.ConnectedESP32Adapter::sendHomeGarageExitRequest, 0xFF4FAD00, CarColor.RED),
            )),
            RFProfile("Garaje San Agustín", listOf(
                RFProfileAction("Botón izquierdo", ESP32Adapter.ConnectedESP32Adapter::sendParentsGarageLeftButtonRequest, 0xFFFF7A37, CarColor.YELLOW),
                RFProfileAction("Botón derecho", ESP32Adapter.ConnectedESP32Adapter::sendParentsGarageRightButtonRequest, 0xFF156AB3, CarColor.YELLOW),
            ))
        )
    }

    fun getAllProfiles(): List<RFProfile> {
        return ALL_PROFILES
    }
}