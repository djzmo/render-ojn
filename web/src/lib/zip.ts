/**
 * Minimal ZIP writer — stored entries only, no compression.
 *
 * Written by hand rather than pulled from a library because the payloads are
 * WAV, MP3, and Ogg: MP3 and Ogg are already compressed and deflate would gain
 * almost nothing, and WAV would gain something but not enough to justify a
 * dependency plus the CPU on files that are already tens of megabytes. Storing
 * means the archive is a copy of the bytes with headers around them, which is
 * fast and allocation-light.
 *
 * Emits ZIP64 fields when an entry or the archive exceeds the 4 GiB limits of
 * the classic format — a full queue of WAV renders reaches that easily.
 */

const LOCAL_SIGNATURE = 0x04034b50
const CENTRAL_SIGNATURE = 0x02014b50
const END_SIGNATURE = 0x06054b50
const ZIP64_END_SIGNATURE = 0x06064b50
const ZIP64_LOCATOR_SIGNATURE = 0x07064b50

/** Values a classic ZIP field cannot hold; ZIP64 takes over above these. */
const MAX_U16 = 0xffff
const MAX_U32 = 0xffffffff

export interface ZipEntry {
  name: string
  /** Backed by a plain ArrayBuffer — Blob rejects SharedArrayBuffer views. */
  bytes: Uint8Array<ArrayBuffer>
}

/** CRC-32 (IEEE), the checksum every ZIP entry carries. */
const CRC_TABLE = (() => {
  const table = new Uint32Array(256)
  for (let n = 0; n < 256; n++) {
    let c = n
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
    table[n] = c >>> 0
  }
  return table
})()

function crc32(bytes: Uint8Array): number {
  let c = 0xffffffff
  for (let i = 0; i < bytes.length; i++) {
    c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8)
  }
  return (c ^ 0xffffffff) >>> 0
}

/**
 * Converts a Date to the DOS date/time pair ZIP stores.
 *
 * DOS time has two-second resolution and cannot represent anything before
 * 1980, so earlier dates clamp to the epoch rather than wrapping into a
 * nonsensical timestamp.
 */
function dosDateTime(date: Date): { time: number; date: number } {
  const year = Math.max(date.getFullYear(), 1980)
  return {
    time:
      (date.getHours() << 11) |
      (date.getMinutes() << 5) |
      (date.getSeconds() >> 1),
    date: ((year - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate(),
  }
}

/**
 * Makes a name safe for an archive.
 *
 * Path separators would create directories (or, with `..`, escape them), and
 * ZIP paths are forward-slash only. Titles come from file headers and are not
 * trustworthy as paths.
 */
function safeName(name: string): string {
  const flat = name.replace(/[\\/]+/g, "_").replace(/^\.+/, "")
  return flat.trim() || "render"
}

/**
 * Ensures every entry has a distinct name.
 *
 * Two charts can legitimately render to the same filename — the same song at
 * the same difficulty from different folders — and a zip with duplicate names
 * extracts to a single overwritten file, silently losing one.
 */
function uniqueNames(entries: ZipEntry[]): string[] {
  const seen = new Map<string, number>()
  return entries.map((entry) => {
    const base = safeName(entry.name)
    const key = base.toLowerCase()
    const count = seen.get(key) ?? 0
    seen.set(key, count + 1)
    if (count === 0) return base
    const dot = base.lastIndexOf(".")
    return dot <= 0
      ? `${base} (${count})`
      : `${base.slice(0, dot)} (${count})${base.slice(dot)}`
  })
}

/**
 * Builds the archive.
 *
 * `stamp` is injected rather than read from the clock so the output is
 * reproducible in tests.
 */
export function createZip(entries: ZipEntry[], stamp = new Date()): Blob {
  const { time, date } = dosDateTime(stamp)
  const names = uniqueNames(entries)
  const encoder = new TextEncoder()

  const parts: BlobPart[] = []
  const central: Uint8Array<ArrayBuffer>[] = []
  let offset = 0

  entries.forEach((entry, index) => {
    const name = encoder.encode(names[index])
    const crc = crc32(entry.bytes)
    const size = entry.bytes.length
    // A single entry past 4 GiB needs ZIP64 in its own header; so does an entry
    // whose local header starts past 4 GiB, even if the entry itself is small.
    const needsZip64 = size > MAX_U32 || offset > MAX_U32

    const local = new DataView(new ArrayBuffer(30))
    local.setUint32(0, LOCAL_SIGNATURE, true)
    // 4.5 when a ZIP64 extra field is present, 2.0 otherwise.
    local.setUint16(4, needsZip64 ? 45 : 20, true)
    local.setUint16(6, 0x0800, true) // UTF-8 names
    local.setUint16(8, 0, true) // stored
    local.setUint16(10, time, true)
    local.setUint16(12, date, true)
    local.setUint32(14, crc, true)
    local.setUint32(18, needsZip64 ? MAX_U32 : size, true)
    local.setUint32(22, needsZip64 ? MAX_U32 : size, true)
    local.setUint16(26, name.length, true)
    local.setUint16(28, needsZip64 ? 20 : 0, true)

    parts.push(local.buffer, name)
    let localExtra: Uint8Array<ArrayBuffer> | null = null
    if (needsZip64) {
      const extra = new DataView(new ArrayBuffer(20))
      extra.setUint16(0, 0x0001, true)
      extra.setUint16(2, 16, true)
      extra.setBigUint64(4, BigInt(size), true)
      extra.setBigUint64(12, BigInt(size), true)
      localExtra = new Uint8Array(extra.buffer)
      parts.push(localExtra)
    }
    parts.push(entry.bytes)

    const extraFields: number[] = []
    if (needsZip64) {
      // Only the fields actually overflowed appear, in a fixed order.
      const values: bigint[] = []
      if (size > MAX_U32) values.push(BigInt(size), BigInt(size))
      if (offset > MAX_U32) values.push(BigInt(offset))
      const view = new DataView(new ArrayBuffer(4 + values.length * 8))
      view.setUint16(0, 0x0001, true)
      view.setUint16(2, values.length * 8, true)
      values.forEach((value, i) => view.setBigUint64(4 + i * 8, value, true))
      extraFields.push(...new Uint8Array(view.buffer))
    }

    const header = new DataView(new ArrayBuffer(46))
    header.setUint32(0, CENTRAL_SIGNATURE, true)
    header.setUint16(4, needsZip64 ? 45 : 20, true)
    header.setUint16(6, needsZip64 ? 45 : 20, true)
    header.setUint16(8, 0x0800, true)
    header.setUint16(10, 0, true)
    header.setUint16(12, time, true)
    header.setUint16(14, date, true)
    header.setUint32(16, crc, true)
    header.setUint32(20, size > MAX_U32 ? MAX_U32 : size, true)
    header.setUint32(24, size > MAX_U32 ? MAX_U32 : size, true)
    header.setUint16(28, name.length, true)
    header.setUint16(30, extraFields.length, true)
    header.setUint16(32, 0, true)
    header.setUint16(34, 0, true)
    header.setUint16(36, 0, true)
    header.setUint32(38, 0, true)
    header.setUint32(42, offset > MAX_U32 ? MAX_U32 : offset, true)

    const record = new Uint8Array(46 + name.length + extraFields.length)
    record.set(new Uint8Array(header.buffer), 0)
    record.set(name, 46)
    record.set(new Uint8Array(extraFields), 46 + name.length)
    central.push(record)

    offset += 30 + name.length + (localExtra?.length ?? 0) + size
  })

  const centralOffset = offset
  const centralSize = central.reduce((sum, record) => sum + record.length, 0)
  parts.push(...central)

  // The classic end record cannot express counts past 65535 or offsets past
  // 4 GiB; when either overflows, ZIP64 records precede it and the classic
  // fields hold sentinels.
  const needsZip64End =
    entries.length > MAX_U16 || centralOffset > MAX_U32 || centralSize > MAX_U32

  if (needsZip64End) {
    const end64 = new DataView(new ArrayBuffer(56))
    end64.setUint32(0, ZIP64_END_SIGNATURE, true)
    end64.setBigUint64(4, 44n, true) // size of this record minus 12
    end64.setUint16(12, 45, true)
    end64.setUint16(14, 45, true)
    end64.setUint32(16, 0, true)
    end64.setUint32(20, 0, true)
    end64.setBigUint64(24, BigInt(entries.length), true)
    end64.setBigUint64(32, BigInt(entries.length), true)
    end64.setBigUint64(40, BigInt(centralSize), true)
    end64.setBigUint64(48, BigInt(centralOffset), true)
    parts.push(end64.buffer)

    const locator = new DataView(new ArrayBuffer(20))
    locator.setUint32(0, ZIP64_LOCATOR_SIGNATURE, true)
    locator.setUint32(4, 0, true)
    locator.setBigUint64(8, BigInt(centralOffset + centralSize), true)
    locator.setUint32(16, 1, true)
    parts.push(locator.buffer)
  }

  const end = new DataView(new ArrayBuffer(22))
  end.setUint32(0, END_SIGNATURE, true)
  end.setUint16(4, 0, true)
  end.setUint16(6, 0, true)
  end.setUint16(8, Math.min(entries.length, MAX_U16), true)
  end.setUint16(10, Math.min(entries.length, MAX_U16), true)
  end.setUint32(12, Math.min(centralSize, MAX_U32), true)
  end.setUint32(16, Math.min(centralOffset, MAX_U32), true)
  end.setUint16(20, 0, true)
  parts.push(end.buffer)

  return new Blob(parts, { type: "application/zip" })
}
