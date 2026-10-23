import React, { useEffect, useState } from 'react';
import { Pressable, StyleSheet, Switch, Text, View } from 'react-native';
import { NitroPdf, PdfView, type PageSize } from 'react-native-nitro-pdf';
import { invoiceHtml, receiptHtml } from './samples';

declare const performance: { now(): number };

const DOCUMENTS: Record<string, string> = {
  Receipt: receiptHtml,
  'Invoice · 20 rows': invoiceHtml(20),
  'Invoice · 200 rows': invoiceHtml(200),
};
const PAGE_SIZES: PageSize[] = ['A4', 'Letter', 'Legal'];
// PDF points: 72 pt = 1 inch.
const MARGINS: Record<string, number> = { None: 0, '0.5 in': 36, '1 in': 72 };

type Result = {
  data: ArrayBuffer;
  pages: number;
  kb: number;
  header: string;
  ms: number;
};

function Choice<T extends string>({
  label,
  options,
  value,
  onChange,
}: {
  label: string;
  options: readonly T[];
  value: T;
  onChange: (value: T) => void;
}) {
  return (
    <View style={styles.option}>
      <Text style={styles.label}>{label}</Text>
      <View style={styles.chips}>
        {options.map(option => (
          <Pressable
            key={option}
            onPress={() => onChange(option)}
            style={[styles.chip, option === value && styles.chipActive]}
          >
            <Text
              style={[
                styles.chipText,
                option === value && styles.chipTextActive,
              ]}
            >
              {option}
            </Text>
          </Pressable>
        ))}
      </View>
    </View>
  );
}

export function Examples() {
  const [sample, setSample] = useState('Invoice · 20 rows');
  const [pageSize, setPageSize] = useState<PageSize>('A4');
  const [landscape, setLandscape] = useState(false);
  const [margin, setMargin] = useState('0.5 in');
  const [result, setResult] = useState<Result>();
  const [error, setError] = useState<string>();

  useEffect(() => {
    let cancelled = false;
    (async () => {
      const start = performance.now();
      const m = MARGINS[margin];
      // Layout and PDF writing run on a background thread; the JS thread stays free.
      const pdf = await NitroPdf.generate({
        html: DOCUMENTS[sample],
        pageSize,
        landscape,
        margins: { top: m, right: m, bottom: m, left: m },
      });
      const ms = performance.now() - start;

      // A zero-copy view of the PDF bytes: upload, hash or display them without touching disk.
      // Use `await pdf.saveToFile(path)` when you do need a file.
      const data = pdf.toArrayBuffer();
      const header = String.fromCharCode(...new Uint8Array(data, 0, 8));

      if (!cancelled) {
        setError(undefined);
        setResult({
          data,
          pages: pdf.pageCount,
          kb: Math.round(pdf.byteLength / 1024),
          header,
          ms,
        });
      }
    })().catch(e => {
      if (!cancelled) setError(String(e));
    });
    return () => {
      cancelled = true;
    };
  }, [sample, pageSize, landscape, margin]);

  return (
    <View style={styles.container}>
      <Choice
        label="Document"
        options={Object.keys(DOCUMENTS)}
        value={sample}
        onChange={setSample}
      />
      <Choice
        label="Page size"
        options={PAGE_SIZES}
        value={pageSize}
        onChange={setPageSize}
      />
      <Choice
        label="Margins"
        options={Object.keys(MARGINS)}
        value={margin}
        onChange={setMargin}
      />
      <View style={[styles.option, styles.row]}>
        <Text style={styles.label}>Landscape</Text>
        <Switch value={landscape} onValueChange={setLandscape} />
      </View>

      <Text style={styles.result}>
        {error ??
          (result
            ? `${result.pages} ${result.pages === 1 ? 'page' : 'pages'} · ${result.kb} KB · ` +
              `generated in ${result.ms.toFixed(1)} ms · ${result.header.trim()}`
            : 'Generating…')}
      </Text>

      <View style={styles.preview}>
        {result && (
          <PdfView
            style={styles.pdf}
            data={result.data}
            onError={message => setError(message)}
          />
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, padding: 16, backgroundColor: 'white' },
  option: { marginBottom: 10 },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  label: { fontSize: 13, fontWeight: '600', color: '#555', marginBottom: 6 },
  chips: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  chip: {
    paddingVertical: 6,
    paddingHorizontal: 12,
    borderRadius: 16,
    borderWidth: 1,
    borderColor: '#ccc',
  },
  chipActive: { backgroundColor: '#5f259f', borderColor: '#5f259f' },
  chipText: { fontSize: 14, color: '#333' },
  chipTextActive: { color: 'white' },
  result: { fontSize: 13, color: '#222', marginVertical: 8 },
  preview: {
    flex: 1,
    borderRadius: 8,
    overflow: 'hidden',
    backgroundColor: '#e5e5e5',
  },
  pdf: { flex: 1 },
});
