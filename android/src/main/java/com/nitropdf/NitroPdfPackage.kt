package com.nitropdf;

import com.facebook.react.bridge.NativeModule;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.module.model.ReactModuleInfoProvider;
import com.facebook.react.BaseReactPackage;
import com.facebook.react.uimanager.ViewManager
import com.margelo.nitro.nitropdf.NitroPdfOnLoad;
import com.margelo.nitro.nitropdf.views.HybridPdfViewManager


public class NitroPdfPackage : BaseReactPackage() {
  override fun getModule(name: String, reactContext: ReactApplicationContext): NativeModule? = null

  override fun getReactModuleInfoProvider(): ReactModuleInfoProvider = ReactModuleInfoProvider { emptyMap() }

  override fun createViewManagers(reactContext: ReactApplicationContext): List<ViewManager<*, *>> =
    listOf(HybridPdfViewManager())

  companion object {
    init {
      NitroPdfOnLoad.initializeNative();
    }
  }
}
