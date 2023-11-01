package me.devcexx.rfapp.adapter

import androidx.car.app.model.CarColor
import me.devcexx.rfapp.model.RFProfile
import me.devcexx.rfapp.model.RFProfileAction

class ProfileRepository {
    companion object {
        private val ALL_PROFILES: List<RFProfile> = listOf(
            RFProfile(
                "Garaje Vicálvaro", listOf(
                    RFProfileAction(
                        "Entrar al garaje",
                        { it.runSendRfCommand(RFCompanionAdapter.SendRfStoredCode.HOME_2_GARAGE_ENTER) },
                        0xff00ad93,
                        CarColor.RED
                    ),
                    RFProfileAction(
                        "Salir del garaje",
                        { it.runSendRfCommand(RFCompanionAdapter.SendRfStoredCode.HOME_2_GARAGE_EXIT) },
                        0xffad0062,
                        CarColor.RED
                    ),
                )
            ),
            RFProfile(
                "Garaje San Agustín", listOf(
                    RFProfileAction(
                        "Abrir garaje",
                        { it.runSendRfCommand(RFCompanionAdapter.SendRfStoredCode.PARENTS_GARAGE_B) },
                        0xFF156AB3,
                        CarColor.YELLOW
                    ),
                )
            ),
            RFProfile(
                "Garaje Majadahonda", listOf(
                    RFProfileAction(
                        "Entrar al garaje",
                        { it.runSendRfCommand(RFCompanionAdapter.SendRfStoredCode.HOME_1_GARAGE_ENTER) },
                        0xFF9F00AD,
                        CarColor.RED
                    ),
                    RFProfileAction(
                        "Salir del garaje",
                        { it.runSendRfCommand(RFCompanionAdapter.SendRfStoredCode.HOME_1_GARAGE_EXIT) },
                        0xFF4FAD00,
                        CarColor.RED
                    ),
                )
            ),
            RFProfile(
                "Otros", listOf(
                    RFProfileAction(
                        "Cargador tesla",
                        { it.runSendRfCommand(RFCompanionAdapter.SendRfStoredCode.TESLA_CHARGER_OPENER) },
                        0xFFFF7A37,
                        CarColor.YELLOW
                    )
                )
            )
        )
    }

    fun getAllProfiles(): List<RFProfile> {
        return ALL_PROFILES
    }
}