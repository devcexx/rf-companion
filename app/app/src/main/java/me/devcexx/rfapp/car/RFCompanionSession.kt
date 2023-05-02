package me.devcexx.rfapp.car

import android.content.Intent
import androidx.car.app.Screen
import androidx.car.app.Session

class RFCompanionSession : Session() {
    override fun onCreateScreen(intent: Intent): Screen {
        return RFCompanionScreen(carContext)
    }
}