package com.lnan.lanshare

actual fun getPlatform(): Platform = AndroidPlatform()

class AndroidPlatform : Platform {
    override val name: String = "Android ${android.os.Build.VERSION.SDK_INT}"
}
