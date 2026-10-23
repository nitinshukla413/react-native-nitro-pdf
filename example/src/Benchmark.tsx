import React, { useEffect, useRef, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import { generatePDF } from 'react-native-html-to-pdf';
import { NitroPdf, PdfView } from 'react-native-nitro-pdf';
import { invoiceHtml, receiptHtml } from './samples';

declare const performance: { now(): number };

type Library = 'nitro' | 'html-to-pdf';
type Output = { pages: number; data?: ArrayBuffer; path?: string };
type Row = {
  label: string;
  output?: Output;
  generateMs?: number;
  displayMs?: number;
  viewerPages?: number;
};
type Displayed = { pages: number; at: number };

const DOCUMENTS = [
  { label: 'Receipt', html: receiptHtml },
  { label: '150-row invoice', html: invoiceHtml(150) },
  { label: '1000-row invoice', html: invoiceHtml(1000) },
];

// Both libraries print A4 (595 × 842 pt) without margins, and are shown in the same <PdfView>.
const LIBRARIES: Record<
  Library,
  {
    title: string;
    output: string;
    color: string;
    generate(html: string, name: string): Promise<Output>;
  }
> = {
  nitro: {
    title: 'react-native-nitro-pdf',
    output: 'ArrayBuffer in memory',
    color: '#5f259f',
    async generate(html) {
      const pdf = await NitroPdf.generate({
        html,
        pageSize: 'A4',
        margins: { top: 0, right: 0, bottom: 0, left: 0 },
      });
      return { pages: pdf.pageCount, data: pdf.toArrayBuffer() };
    },
  },
  'html-to-pdf': {
    title: 'react-native-html-to-pdf',
    output: 'File on disk',
    color: '#1f6feb',
    async generate(html, name) {
      const result = await generatePDF({
        html,
        fileName: name,
        directory: 'benchmark',
        width: 595,
        height: 842,
      });
      return {
        pages: Number(result.numberOfPages ?? 0),
        path: result.filePath,
      };
    },
  },
};

function formatMs(ms?: number): string {
  if (ms === undefined) return '…';
  return ms < 10 ? `${ms.toFixed(1)} ms` : `${Math.round(ms)} ms`;
}

export function Benchmark() {
  const [ready, setReady] = useState(false);
  const [running, setRunning] = useState(false);
  const [library, setLibrary] = useState<Library>();
  const [rows, setRows] = useState<Row[]>([]);
  const [shown, setShown] = useState<number>();
  const [runId, setRunId] = useState(0);
  const [error, setError] = useState<string>();
  const display = useRef<{
    resolve(value: Displayed): void;
    reject(error: Error): void;
  }>(undefined);

  useEffect(() => {
    // First calls pay one-off start-up costs (e.g. creating a WebView), so warm both up.
    (async () => {
      await LIBRARIES.nitro.generate(receiptHtml, 'warmup');
      await LIBRARIES['html-to-pdf'].generate(receiptHtml, 'warmup');
    })()
      .catch(e => setError(String(e)))
      .finally(() => {
        setReady(true);
        console.log('BENCH_READY');
      });
  }, []);

  const run = async (lib: Library) => {
    const results: Row[] = DOCUMENTS.map(doc => ({ label: doc.label }));
    setLibrary(lib);
    setRunId(id => id + 1);
    setRows(results);
    setError(undefined);
    setRunning(true);
    try {
      for (const [index, doc] of DOCUMENTS.entries()) {
        // Untimed pause: lets the viewer finish with the previous PDF, so each step starts idle.
        await new Promise<void>(resolve => setTimeout(resolve, 600));
        const start = performance.now();
        const output = await LIBRARIES[lib].generate(doc.html, `doc-${index}`);
        const generated = performance.now();
        const displayed = new Promise<Displayed>((resolve, reject) => {
          display.current = { resolve, reject };
        });
        results[index] = {
          ...results[index],
          output,
          generateMs: generated - start,
        };
        setRows([...results]);
        setShown(index);
        const { pages, at } = await displayed;
        results[index] = {
          ...results[index],
          viewerPages: pages,
          displayMs: at - generated,
        };
        setRows([...results]);
        const row = results[index];
        console.log(
          `BENCH_ROW ${lib} ${index} generateMs=${row.generateMs?.toFixed(1)} ` +
            `displayMs=${row.displayMs?.toFixed(1)} pages=${output.pages} viewerPages=${pages}`,
        );
      }
      console.log(`BENCH_DONE ${lib}`);
    } catch (e) {
      setError(String(e));
      console.log(`BENCH_ERROR ${lib} ${String(e)}`);
    } finally {
      setRunning(false);
    }
  };

  const onLoad = (pages: number) => {
    display.current?.resolve({ pages, at: performance.now() });
    display.current = undefined;
  };
  const onError = (message: string) => {
    display.current?.reject(new Error(message));
    display.current = undefined;
  };

  const current = library && LIBRARIES[library];
  const output = shown !== undefined ? rows[shown]?.output : undefined;
  const done = rows.filter(row => row.displayMs !== undefined);
  const total = (key: 'generateMs' | 'displayMs') =>
    done.reduce((sum, row) => sum + (row[key] ?? 0), 0);
  const filesWritten = done.filter(row => row.output?.path).length;

  return (
    <View style={styles.container}>
      <Text style={styles.heading}>Generate → display</Text>
      <Text style={styles.caption}>
        Three documents, one after another, shown in the same {'<PdfView>'}.
      </Text>
      <View style={styles.buttons}>
        {(Object.keys(LIBRARIES) as Library[]).map(lib => (
          <Pressable
            key={lib}
            disabled={!ready || running}
            onPress={() => run(lib)}
            style={[
              styles.button,
              { backgroundColor: LIBRARIES[lib].color },
              (!ready || running) && styles.disabled,
            ]}
          >
            <Text style={styles.buttonText}>{LIBRARIES[lib].title}</Text>
          </Pressable>
        ))}
      </View>

      {current && (
        <>
          <Text style={[styles.title, { color: current.color }]}>
            {current.title}
          </Text>
          <Text style={styles.caption}>Output: {current.output}</Text>
          <View style={styles.table}>
            <View style={styles.row}>
              <Text style={[styles.cell, styles.first, styles.head]}>
                Document
              </Text>
              <Text style={[styles.cell, styles.head]}>Generate</Text>
              <Text style={[styles.cell, styles.head]}>Display</Text>
              <Text style={[styles.cell, styles.head]}>Total</Text>
              <Text style={[styles.cell, styles.head]}>Pages</Text>
            </View>
            {rows.map((row, index) => (
              <Pressable
                key={row.label}
                disabled={running || !row.output}
                onPress={() => setShown(index)}
                style={[styles.row, shown === index && styles.selected]}
              >
                <Text style={[styles.cell, styles.first]}>{row.label}</Text>
                <Text style={styles.cell}>{formatMs(row.generateMs)}</Text>
                <Text style={styles.cell}>{formatMs(row.displayMs)}</Text>
                <Text style={styles.cell}>
                  {row.displayMs === undefined
                    ? '…'
                    : formatMs((row.generateMs ?? 0) + row.displayMs)}
                </Text>
                <Text style={styles.cell}>
                  {row.viewerPages === undefined
                    ? '…'
                    : row.viewerPages === row.output?.pages
                      ? `${row.viewerPages} ✓`
                      : `${row.viewerPages}/${row.output?.pages} ✗`}
                </Text>
              </Pressable>
            ))}
            <View style={[styles.row, styles.totals]}>
              <Text style={[styles.cell, styles.first, styles.head]}>
                Total
              </Text>
              <Text style={[styles.cell, styles.head]}>
                {formatMs(total('generateMs'))}
              </Text>
              <Text style={[styles.cell, styles.head]}>
                {formatMs(total('displayMs'))}
              </Text>
              <Text style={[styles.cell, styles.head]}>
                {formatMs(total('generateMs') + total('displayMs'))}
              </Text>
              <Text style={[styles.cell, styles.head]}>
                {done.reduce((sum, row) => sum + (row.viewerPages ?? 0), 0)}
              </Text>
            </View>
          </View>
          <Text style={styles.caption}>
            Files written to disk: {filesWritten}
            {error ? `  ·  ${error}` : ''}
          </Text>
        </>
      )}

      <View style={styles.viewer}>
        {output && (
          <PdfView
            key={`${runId}-${shown}`}
            style={styles.pdf}
            data={output.data}
            path={output.path}
            onLoad={onLoad}
            onError={onError}
          />
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, padding: 16, backgroundColor: 'white' },
  heading: { fontSize: 22, fontWeight: '700', color: '#111' },
  caption: { fontSize: 13, color: '#555', marginTop: 4 },
  buttons: { flexDirection: 'row', gap: 8, marginVertical: 12 },
  button: { flex: 1, borderRadius: 8, padding: 10, alignItems: 'center' },
  buttonText: { color: 'white', fontWeight: '600', fontSize: 13 },
  disabled: { opacity: 0.4 },
  title: { fontSize: 18, fontWeight: '700' },
  table: {
    marginTop: 8,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: '#ccc',
    borderRadius: 8,
    overflow: 'hidden',
  },
  row: { flexDirection: 'row', paddingVertical: 6, paddingHorizontal: 8 },
  selected: { backgroundColor: '#f2f2f6' },
  totals: { borderTopWidth: StyleSheet.hairlineWidth, borderColor: '#ccc' },
  cell: { flex: 1, fontSize: 13, color: '#222', textAlign: 'right' },
  first: { flex: 1.8, textAlign: 'left' },
  head: { fontWeight: '700' },
  viewer: {
    flex: 1,
    marginTop: 12,
    borderRadius: 8,
    overflow: 'hidden',
    backgroundColor: '#e5e5e5',
  },
  pdf: { flex: 1 },
});
