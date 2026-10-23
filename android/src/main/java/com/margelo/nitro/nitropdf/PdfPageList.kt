package com.margelo.nitro.nitropdf

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.graphics.pdf.PdfRenderer
import android.os.ParcelFileDescriptor
import android.util.LruCache
import android.util.Size
import android.view.View
import android.view.ViewGroup
import android.widget.AbsListView
import android.widget.BaseAdapter
import android.widget.ImageView
import android.widget.ListView
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

/** A vertically scrolling list of pages, rendered lazily on one background thread. */
internal class PdfPageList(context: Context) : ListView(context) {
  // PdfRenderer is not thread-safe, so it is only touched on this executor.
  private val executor = Executors.newSingleThreadExecutor()
  private var renderer: PdfRenderer? = null
  private var descriptor: ParcelFileDescriptor? = null

  private val generation = AtomicInteger()
  private var pageSizes: List<Size> = emptyList()
  private val pending = HashSet<Int>()
  private var onFirstPage: (() -> Unit)? = null
  private val bitmaps = object : LruCache<Int, Bitmap>((Runtime.getRuntime().maxMemory() / 8).toInt()) {
    override fun sizeOf(key: Int, value: Bitmap) = value.byteCount
  }
  private val pageAdapter = PageAdapter()
  private var layoutPosted = false

  init {
    val gap = (8 * resources.displayMetrics.density).toInt()
    setBackgroundColor(Color.rgb(229, 229, 229))
    divider = ColorDrawable(Color.TRANSPARENT)
    dividerHeight = gap
    setPadding(gap, gap, gap, gap)
    clipToPadding = false
    adapter = pageAdapter
  }

  fun load(open: () -> ParcelFileDescriptor, onLoad: (Int) -> Unit, onError: (String) -> Unit) {
    val gen = reset()
    executor.execute {
      closeDocument()
      try {
        val fd = open()
        val pdf = PdfRenderer(fd)
        descriptor = fd
        renderer = pdf
        val sizes = (0 until pdf.pageCount).map { index ->
          val page = pdf.openPage(index)
          try { Size(page.width, page.height) } finally { page.close() }
        }
        post {
          if (gen != generation.get()) return@post
          pageSizes = sizes
          if (sizes.isEmpty()) onLoad(0) else onFirstPage = { onLoad(sizes.size) }
          pageAdapter.notifyDataSetChanged()
        }
      } catch (e: Exception) {
        post { if (gen == generation.get()) onError(e.message ?: "Could not open the PDF") }
      }
    }
  }

  fun clear() {
    reset()
    executor.execute { closeDocument() }
  }

  fun release() {
    clear()
    executor.shutdown()
  }

  private fun reset(): Int {
    val gen = generation.incrementAndGet()
    pageSizes = emptyList()
    pending.clear()
    onFirstPage = null
    bitmaps.evictAll()
    pageAdapter.notifyDataSetChanged()
    return gen
  }

  private fun closeDocument() {
    renderer?.close()
    descriptor?.close()
    renderer = null
    descriptor = null
  }

  private fun pageHeight(index: Int, width: Int): Int {
    val size = pageSizes[index]
    return (width.toLong() * size.height / size.width).toInt()
  }

  private fun requestPage(index: Int, width: Int) {
    if (!pending.add(index)) return
    val gen = generation.get()
    val height = pageHeight(index, width)
    executor.execute {
      val pdf = renderer
      val bitmap = if (gen == generation.get() && pdf != null) render(pdf, index, width, height) else null
      post {
        if (gen != generation.get()) return@post
        pending.remove(index)
        if (bitmap == null) return@post
        bitmaps.put(index, bitmap)
        findViewWithTag<ImageView>(index)?.setImageBitmap(bitmap)
        if (index == 0) onFirstPage?.let { callback ->
          onFirstPage = null
          callback()
        }
      }
    }
  }

  private fun render(pdf: PdfRenderer, index: Int, width: Int, height: Int): Bitmap? =
    try {
      val page = pdf.openPage(index)
      try {
        Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888).also { bitmap ->
          bitmap.eraseColor(Color.WHITE)
          page.render(bitmap, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
        }
      } finally {
        page.close()
      }
    } catch (e: Exception) {
      null
    }

  // React Native lays out its own views only; children added later need a manual pass.
  override fun requestLayout() {
    super.requestLayout()
    if (layoutPosted) return
    layoutPosted = true
    post {
      layoutPosted = false
      measure(
        MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
        MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY),
      )
      layout(left, top, right, bottom)
    }
  }

  private inner class PageAdapter : BaseAdapter() {
    override fun getCount() = pageSizes.size

    override fun getItem(position: Int): Any = position

    override fun getItemId(position: Int) = position.toLong()

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
      val image = convertView as? ImageView ?: ImageView(context).apply {
        scaleType = ImageView.ScaleType.FIT_XY
        setBackgroundColor(Color.WHITE)
      }
      val width = this@PdfPageList.width - paddingLeft - paddingRight
      image.layoutParams = AbsListView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, pageHeight(position, width))
      image.tag = position
      val bitmap = bitmaps.get(position)
      image.setImageBitmap(bitmap)
      if (bitmap == null && width > 0) requestPage(position, width)
      return image
    }
  }
}
