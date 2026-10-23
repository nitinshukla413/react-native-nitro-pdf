import React, { useMemo } from 'react'
import type { ViewProps } from 'react-native'
import {
  NitroModules,
  callback,
  getHostComponent,
} from 'react-native-nitro-modules'
import PdfViewConfig from '../nitrogen/generated/shared/json/PdfViewConfig.json'
import type {
  PdfViewMethods,
  PdfViewProps as NativePdfViewProps,
} from './specs/pdf-view.nitro'

const NativePdfView = getHostComponent<NativePdfViewProps, PdfViewMethods>(
  'PdfView',
  () => PdfViewConfig
)

export interface PdfViewProps extends ViewProps {
  /** PDF bytes, e.g. `pdf.toArrayBuffer()`. Rendered straight from memory. */
  data?: ArrayBuffer
  /** Absolute path or file:// URL of a PDF. Used when `data` is not set. */
  path?: string
  /** Called once the document is open and its first page is ready. */
  onLoad?: (pageCount: number) => void
  onError?: (message: string) => void
}

/** Native views read props on the UI thread, so JS-owned buffers are copied into native memory once. */
function toNativeBuffer(buffer: ArrayBuffer): ArrayBuffer {
  if (NitroModules.hasNativeState(buffer)) return buffer
  const copy = NitroModules.createNativeArrayBuffer(buffer.byteLength)
  new Uint8Array(copy).set(new Uint8Array(buffer))
  return copy
}

/** Displays a PDF with the platform engine (PDFKit on iOS, PdfRenderer on Android). */
export function PdfView({
  data,
  path,
  onLoad,
  onError,
  ...props
}: PdfViewProps) {
  const nativeData = useMemo(() => data && toNativeBuffer(data), [data])
  return (
    <NativePdfView
      {...props}
      data={nativeData}
      path={path}
      onLoad={callback(onLoad)}
      onError={callback(onError)}
    />
  )
}
