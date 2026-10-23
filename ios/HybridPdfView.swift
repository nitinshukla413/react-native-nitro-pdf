import Foundation
import NitroModules
import PDFKit
import UIKit

/// Renders a PDF with PDFKit. `data` is wrapped without copying, so the bytes never touch disk.
class HybridPdfView: HybridPdfViewSpec {
  private let pdfView: PDFView = {
    let view = PDFView()
    view.autoScales = true
    view.displayMode = .singlePageContinuous
    view.displayDirection = .vertical
    return view
  }()
  private var loadedSource: String?
  private var loadToken = 0

  var view: UIView { pdfView }

  var data: ArrayBuffer?
  var path: String?
  var onLoad: ((_ pageCount: Double) -> Void)?
  var onError: ((_ message: String) -> Void)?

  func afterUpdate() {
    if let data, !data.isOwner {
      // JS-owned buffers can only be read on the JS thread; the JS wrapper copies them first.
      onError?("PdfView needs a native ArrayBuffer, e.g. pdf.toArrayBuffer()")
      return
    }
    let source = data.map { "memory:\(UInt(bitPattern: $0.data)):\($0.size)" } ?? path
    guard source != loadedSource else { return }
    loadedSource = source
    load()
  }

  func onDropView() {
    loadToken += 1
    pdfView.document = nil
  }

  private func load() {
    loadToken += 1
    let token = loadToken
    pdfView.document = nil
    let bytes = data?.toData(copyIfNeeded: false)
    let url = data == nil ? path.map(Self.fileURL) : nil
    guard bytes != nil || url != nil else { return }

    DispatchQueue.global(qos: .userInitiated).async { [weak self] in
      let document = bytes.flatMap(PDFDocument.init(data:)) ?? url.flatMap(PDFDocument.init(url:))
      DispatchQueue.main.async {
        guard let self, token == self.loadToken else { return }
        guard let document else {
          self.onError?("Could not open the PDF")
          return
        }
        self.pdfView.document = document
        self.pdfView.layoutDocumentView()
        self.onLoad?(Double(document.pageCount))
      }
    }
  }

  private static func fileURL(_ path: String) -> URL {
    if path.hasPrefix("file://"), let url = URL(string: path) {
      return url
    }
    return URL(fileURLWithPath: path)
  }
}
