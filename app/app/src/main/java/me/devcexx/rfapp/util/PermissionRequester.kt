package me.devcexx.rfapp.util

import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import kotlin.coroutines.Continuation
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

class PermissionRequester(activity: ComponentActivity) {
    private var requestPermissionContinuation: Continuation<Boolean>? = null
    private val activityResultLauncher = activity.registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        requestPermissionContinuation?.resume(granted.values.fold(true) { l, r -> l && r })
    }


    suspend fun requestPermission(vararg permissions: String) = suspendCoroutine { cont ->
        val perms: Array<String> = permissions.map { it }.toTypedArray()
        requestPermissionContinuation = cont
        activityResultLauncher.launch(perms)
    }
}