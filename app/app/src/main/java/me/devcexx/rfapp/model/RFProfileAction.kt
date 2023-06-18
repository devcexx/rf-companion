package me.devcexx.rfapp.model

import androidx.car.app.model.CarColor
import me.devcexx.rfapp.adapter.RFCompanionAdapter

data class RFProfileAction(val text: String,
                           val rfAction: suspend (RFCompanionAdapter) -> Unit,
                           val actionColor: Long,
                           val carColor: CarColor)