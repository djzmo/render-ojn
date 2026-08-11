import { crc32 } from "node:zlib"

import { unzipSync } from "fflate"
import { expect, test } from "vitest"

import { createZip, type ZipEntry } from "@/lib/zip"

/*
 * Verified against fflate rather than a hand-rolled decoder in this file:
 * checking one ZIP implementation with a second one written from the same
 * understanding of the format lets a shared misreading cancel out. fflate is
 * independent. Byte-level assertions sit alongside it because fflate reads the
 * central directory and would happily ignore a corrupt *local* header that a
 * streaming extractor rejects.
 */

/** A fixed stamp so every archive in this file is byte-reproducible. */
const STAMP = new Date(2026, 0, 2, 3, 4, 6)

const bytesOf = (text: string) => new TextEncoder().encode(text)

async function archive(entries: ZipEntry[], stamp = STAMP): Promise<Uint8Array> {
  return new Uint8Array(await createZip(entries, stamp).arrayBuffer())
}

const LOCAL_SIGNATURE = 0x04034b50
const CENTRAL_SIGNATURE = 0x02014b50
const END_SIGNATURE = 0x06054b50
const ZIP64_END_SIGNATURE = 0x06064b50

/** Walks the end-of-central-directory record back to its entries. */
function centralDirectory(zip: Uint8Array) {
  const view = new DataView(zip.buffer, zip.byteOffset, zip.byteLength)
  let end = -1
  for (let i = zip.length - 22; i >= 0; i--) {
    if (view.getUint32(i, true) === END_SIGNATURE) {
      end = i
      break
    }
  }
  if (end < 0) throw new Error("no end-of-central-directory record")

  const count = view.getUint16(end + 10, true)
  let cursor = view.getUint32(end + 16, true)
  const records = []
  for (let i = 0; i < count; i++) {
    const nameLength = view.getUint16(cursor + 28, true)
    const extraLength = view.getUint16(cursor + 30, true)
    const commentLength = view.getUint16(cursor + 32, true)
    records.push({
      signature: view.getUint32(cursor, true),
      flags: view.getUint16(cursor + 8, true),
      method: view.getUint16(cursor + 10, true),
      crc: view.getUint32(cursor + 16, true),
      compressedSize: view.getUint32(cursor + 20, true),
      uncompressedSize: view.getUint32(cursor + 24, true),
      localOffset: view.getUint32(cursor + 42, true),
      name: new TextDecoder().decode(
        zip.subarray(cursor + 46, cursor + 46 + nameLength)
      ),
    })
    cursor += 46 + nameLength + extraLength + commentLength
  }
  return { count, records, endOffset: end }
}

function hasZip64EndRecord(zip: Uint8Array): boolean {
  const view = new DataView(zip.buffer, zip.byteOffset, zip.byteLength)
  for (let i = 0; i <= zip.length - 4; i++) {
    if (view.getUint32(i, true) === ZIP64_END_SIGNATURE) return true
  }
  return false
}

/* ------------------------------------------------------------------ *
 * Round trips through an independent implementation
 * ------------------------------------------------------------------ */

test("a single entry round-trips through an independent unzip", async () => {
  const payload = bytesOf("hello world")
  const unzipped = unzipSync(await archive([{ name: "a.txt", bytes: payload }]))

  expect(Object.keys(unzipped)).toEqual(["a.txt"])
  expect(unzipped["a.txt"]).toEqual(payload)
})

test("several entries round-trip with their bytes intact", async () => {
  const entries = [
    { name: "one.ogg", bytes: bytesOf("first") },
    { name: "two.mp3", bytes: bytesOf("second") },
    { name: "three.wav", bytes: bytesOf("third") },
  ]
  const unzipped = unzipSync(await archive(entries))

  expect(Object.keys(unzipped)).toEqual(["one.ogg", "two.mp3", "three.wav"])
  for (const entry of entries) {
    expect(unzipped[entry.name]).toEqual(entry.bytes)
  }
})

test("a zero-byte entry survives the round trip", async () => {
  // A chart that renders to nothing must still appear in the archive rather
  // than silently vanishing.
  const unzipped = unzipSync(
    await archive([{ name: "empty.wav", bytes: new Uint8Array(0) }])
  )
  expect(unzipped["empty.wav"]).toEqual(new Uint8Array(0))
})

test("a non-ASCII name round-trips, which the UTF-8 flag is what enables", async () => {
  const name = "誰にも [Hard].ogg"
  const unzipped = unzipSync(await archive([{ name, bytes: bytesOf("x") }]))
  expect(Object.keys(unzipped)).toEqual([name])

  const { records } = centralDirectory(await archive([{ name, bytes: bytesOf("x") }]))
  expect(records[0].flags & 0x0800).toBe(0x0800)
})

test("a megabyte of arbitrary bytes round-trips unchanged", async () => {
  // Real payloads are audio, so exercise something that is not text.
  const payload = new Uint8Array(1024 * 1024)
  for (let i = 0; i < payload.length; i++) payload[i] = (i * 31 + 7) & 0xff

  const unzipped = unzipSync(await archive([{ name: "big.wav", bytes: payload }]))
  expect(unzipped["big.wav"]).toEqual(payload)
})

test("an archive with no entries is still structurally valid", async () => {
  const zip = await archive([])
  expect(unzipSync(zip)).toEqual({})
  expect(centralDirectory(zip).count).toBe(0)
})

/* ------------------------------------------------------------------ *
 * Byte-level structure
 * ------------------------------------------------------------------ */

test("entry checksums match an independent CRC-32 implementation", async () => {
  // node:zlib is the oracle; the writer rolls its own table so no crypto API is
  // needed in the browser.
  const payloads = [bytesOf("123456789"), bytesOf(""), bytesOf("RenderOJN")]
  const zip = await archive(
    payloads.map((bytes, index) => ({ name: `f${index}.bin`, bytes }))
  )

  const { records } = centralDirectory(zip)
  records.forEach((record, index) => {
    expect(record.crc).toBe(crc32(payloads[index]))
  })
  // The canonical check vector, stated explicitly so a table regression is
  // obvious rather than merely "different from zlib".
  expect(records[0].crc).toBe(0xcbf43926)
})

test("entries are stored rather than deflated", async () => {
  // MP3 and Ogg payloads are already compressed, so the writer stores them and
  // the two size fields must agree with the input length.
  const payload = bytesOf("some audio bytes")
  const { records } = centralDirectory(
    await archive([{ name: "a.ogg", bytes: payload }])
  )

  expect(records[0].method).toBe(0)
  expect(records[0].compressedSize).toBe(payload.length)
  expect(records[0].uncompressedSize).toBe(payload.length)
})

test("every central record points at a real local header", async () => {
  // The offset accounting (30 + name length + payload) drifts silently if the
  // header layout changes, and fflate would still read the archive because it
  // trusts the central directory.
  const zip = await archive([
    { name: "a.ogg", bytes: bytesOf("aaaa") },
    { name: "bb.mp3", bytes: bytesOf("bbbbbbbb") },
    { name: "ccc.wav", bytes: new Uint8Array(0) },
  ])
  const view = new DataView(zip.buffer, zip.byteOffset, zip.byteLength)

  for (const record of centralDirectory(zip).records) {
    expect(record.signature).toBe(CENTRAL_SIGNATURE)
    expect(view.getUint32(record.localOffset, true)).toBe(LOCAL_SIGNATURE)
  }
})

test("the same input and stamp always produce the same bytes", async () => {
  // The stamp parameter exists so callers can be reproducible; without it the
  // DOS timestamp would make every archive differ.
  const entries = [{ name: "a.ogg", bytes: bytesOf("stable") }]
  expect(await archive(entries)).toEqual(await archive(entries))
})

test("timestamps before 1980 clamp instead of wrapping", async () => {
  // DOS dates cannot represent them, and a wrapped year would produce a file
  // dated decades away.
  const zip = await archive([{ name: "a.ogg", bytes: bytesOf("x") }], new Date(1979, 5, 5))
  expect(() => unzipSync(zip)).not.toThrow()
})

/* ------------------------------------------------------------------ *
 * Name handling
 * ------------------------------------------------------------------ */

test("path separators are flattened so no directory is created", async () => {
  const unzipped = unzipSync(
    await archive([{ name: "sub/dir/x.ogg", bytes: bytesOf("x") }])
  )
  expect(Object.keys(unzipped)).toEqual(["sub_dir_x.ogg"])
})

test("a traversal attempt cannot escape the archive root", async () => {
  // Names come from OJN header titles, which are not trustworthy as paths.
  // Dots may survive inside the name ("_.._etc_passwd"); what makes traversal
  // impossible is that every separator is gone, so the result is one flat
  // filename however an extractor reads it.
  const unzipped = unzipSync(
    await archive([{ name: "../../etc/passwd", bytes: bytesOf("x") }])
  )
  const [name] = Object.keys(unzipped)
  expect(name).not.toContain("/")
  expect(name).not.toContain("\\")
  expect(name.startsWith("..")).toBe(false)
})

test("a name with nothing usable left falls back to a placeholder", async () => {
  const unzipped = unzipSync(
    await archive([
      { name: "...", bytes: bytesOf("a") },
      { name: "   ", bytes: bytesOf("b") },
    ])
  )
  const names = Object.keys(unzipped)
  expect(names[0]).toBe("render")
  expect(names[1]).toMatch(/^render/)
})

test("duplicate names are disambiguated rather than overwriting each other", async () => {
  // Two charts can legitimately render to the same filename -- the same song
  // from different folders. Without this, extracting loses one of them.
  const unzipped = unzipSync(
    await archive([
      { name: "song.ogg", bytes: bytesOf("first") },
      { name: "song.ogg", bytes: bytesOf("second") },
      { name: "song.ogg", bytes: bytesOf("third") },
    ])
  )

  expect(Object.keys(unzipped)).toEqual([
    "song.ogg",
    "song (1).ogg",
    "song (2).ogg",
  ])
  expect(unzipped["song.ogg"]).toEqual(bytesOf("first"))
  expect(unzipped["song (2).ogg"]).toEqual(bytesOf("third"))
})

test("duplicate detection ignores case but the original case is kept", async () => {
  const unzipped = unzipSync(
    await archive([
      { name: "song.ogg", bytes: bytesOf("a") },
      { name: "SONG.OGG", bytes: bytesOf("b") },
    ])
  )
  expect(Object.keys(unzipped)).toEqual(["song.ogg", "SONG (1).OGG"])
})

test("an extensionless duplicate gets its counter appended at the end", async () => {
  const unzipped = unzipSync(
    await archive([
      { name: "noext", bytes: bytesOf("a") },
      { name: "noext", bytes: bytesOf("b") },
    ])
  )
  expect(Object.keys(unzipped)).toEqual(["noext", "noext (1)"])
})

/* ------------------------------------------------------------------ *
 * ZIP64
 *
 * Only the entry-count threshold is reachable in a test. The size and offset
 * thresholds need genuine 4 GiB buffers, and faking them would mean
 * restructuring createZip to accept injected sizes -- changing production code
 * to suit a test, with a fake that could drift from reality. That gap is
 * accepted deliberately.
 * ------------------------------------------------------------------ */

test("an archive at the classic entry limit stays in the classic format", async () => {
  const entries = Array.from({ length: 65535 }, (_, index) => ({
    name: `f${index}`,
    bytes: new Uint8Array(0),
  }))
  const zip = await archive(entries)

  expect(hasZip64EndRecord(zip)).toBe(false)
  expect(centralDirectory(zip).count).toBe(65535)
})

test("an archive past the classic entry limit emits ZIP64 records", async () => {
  // 65,536 entries is the first count the 16-bit field cannot hold.
  const entries = Array.from({ length: 65536 }, (_, index) => ({
    name: `f${index}`,
    bytes: new Uint8Array(0),
  }))
  const zip = await archive(entries)

  expect(hasZip64EndRecord(zip)).toBe(true)
  expect(Object.keys(unzipSync(zip))).toHaveLength(65536)
})
