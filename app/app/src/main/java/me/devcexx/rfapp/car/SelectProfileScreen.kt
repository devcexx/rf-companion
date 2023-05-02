package me.devcexx.rfapp.car

import androidx.car.app.CarContext
import androidx.car.app.Screen
import androidx.car.app.model.Action
import androidx.car.app.model.ActionStrip
import androidx.car.app.model.ItemList
import androidx.car.app.model.ListTemplate
import androidx.car.app.model.Row
import androidx.car.app.model.Template
import me.devcexx.rfapp.R
import me.devcexx.rfapp.RFCompanionApplication.Companion.rfApplication
import me.devcexx.rfapp.model.RFProfile

class SelectProfileScreen(carContext: CarContext): Screen(carContext) {
    private fun onItemClick(profile: RFProfile) {
        setResult(profile)
        screenManager.pop()
    }

    override fun onGetTemplate(): Template {
        val profiles = rfApplication.profileRepository.getAllProfiles()
        val itemListBuilder = ItemList.Builder()
        profiles.forEach {
            itemListBuilder.addItem(Row.Builder()
                .setTitle(it.name)
                .setOnClickListener { onItemClick(it) }
                .build())
        }

        return ListTemplate.Builder()
            .setTitle(carContext.getString(R.string.car_select_profile_title))
            .setSingleList(itemListBuilder.build())
            .setActionStrip(ActionStrip.Builder().addAction(Action.BACK).build())
            .build()
    }
}