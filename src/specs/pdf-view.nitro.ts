import type {
  HybridView,
  HybridViewMethods,
  HybridViewProps,
} from 'react-native-nitro-modules'

export interface PdfViewProps extends HybridViewProps {
  /** PDF bytes, e.g. `pdf.toArrayBuffer()`. Rendered straight from memory. */
  data?: ArrayBuffer
  /** Absolute path or file:// URL of a PDF. Used when `data` is not set. */
  path?: string
  /** Called once the document is open and its first page is ready. */
  onLoad?: (pageCount: number) => void
  onError?: (message: string) => void
}

export interface PdfViewMethods extends HybridViewMethods {}

export type PdfView = HybridView<PdfViewProps, PdfViewMethods>
