package com.margelo.nitro.nitropdf

import android.os.Build
import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import android.view.View
import com.facebook.react.uimanager.ThemedReactContext
import com.margelo.nitro.core.ArrayBuffer
import java.io.File
import java.io.FileDescriptor
import java.io.FileOutputStream
import java.nio.ByteBuffer

/** Renders a PDF with the platform PdfRenderer. `data` is read straight from native memory. */
class HybridPdfView(private val context: ThemedReactContext) : HybridPdfViewSpec() {
  private val pages = PdfPageList(context)
  private var loadedSource: Any? = null

  override val view: View
    get() = pages

  override var data: ArrayBuffer? = null
  override var path: String? = null
  override var onLoad: ((pageCount: Double) -> Unit)? = null
  override var onError: ((message: String) -> Unit)? = null

  override fun afterUpdate() {
    val buffer = data
    if (buffer != null && !buffer.isOwner) {
      // JS-owned buffers can only be read on the JS thread; the JS wrapper copies them first.
      onError?.invoke("PdfView needs a native ArrayBuffer, e.g. pdf.toArrayBuffer()")
      return
    }
    val file = path
    val source: Any? = buffer ?: file
    if (source == loadedSource) return
    loadedSource = source
    if (source == null) {
      pages.clear()
      return
    }
    pages.load(
      open = { if (buffer != null) openMemory(buffer) else openFile(file!!) },
      onLoad = { pageCount -> onLoad?.invoke(pageCount.toDouble()) },
      onError = { message -> onError?.invoke(message) },
    )
  }

  override fun onDropView() {
    pages.release()
  }

  private fun openFile(path: String): ParcelFileDescriptor =
    ParcelFileDescriptor.open(File(path.removePrefix("file://")), ParcelFileDescriptor.MODE_READ_ONLY)

  private fun openMemory(buffer: ArrayBuffer): ParcelFileDescriptor {
    val bytes = buffer.getBuffer(false).duplicate()
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      // An anonymous in-memory file: PdfRenderer needs a seekable descriptor, not a disk file.
      val fd = Os.memfd_create("nitro-pdf", 0)
      try {
        writeFully(fd, bytes)
        Os.lseek(fd, 0, OsConstants.SEEK_SET)
        return ParcelFileDescriptor.dup(fd)
      } finally {
        Os.close(fd)
      }
    }
    // Before Android 11 there is no memfd API, so use a temp file that is unlinked once open.
    val file = File.createTempFile("nitro-pdf", ".pdf", context.cacheDir)
    try {
      FileOutputStream(file).use { writeFully(it.fd, bytes) }
      return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
    } finally {
      file.delete()
    }
  }

  private fun writeFully(fd: FileDescriptor, bytes: ByteBuffer) {
    while (bytes.hasRemaining()) Os.write(fd, bytes)
  }
}
