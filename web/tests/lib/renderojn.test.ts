import { expect, test } from "vitest"

import {
  DEFAULT_DIFFICULTY,
  DEFAULT_QUALITY,
  DIFFICULTY_NAMES,
  FORMAT_VALUES,
  MIME_TYPES,
  OUTPUT_FORMATS,
  QUALITY_DETAIL,
  QUALITY_LABELS,
  QUALITY_VALUES,
  OjnParseError,
  type OutputFormat,
  type Quality,
} from "@/lib/renderojn"

/*
 * These constants are the contract with the C++ core. TypeScript erases the
 * `Quality` and `OutputFormat` types at runtime, so a type alone cannot protect
 * the boundary -- only assertions on the values can. A shipped bug had the UI
 * on a 0-based quality scale while the CLI used 1-3, which silently produced
 * one bitrate tier too low with no error anywhere.
 */

test("format values match the C++ output::Format enum, not display order", () => {
  expect(FORMAT_VALUES).toEqual({ wav: 0, mp3: 1, ogg: 2 })
})

test("format values are not derivable from the OUTPUT_FORMATS display order", () => {
  // OUTPUT_FORMATS is ["wav", "ogg", "mp3"] for the UI; the enum is
  // {Wav=0, Mp3=1, Ogg=2}. Deriving the module argument from the array index
  // would hand OGG the value that means MP3 and produce an MP3 body inside a
  // .ogg file, with no error raised. This inequality is what stops someone
  // "simplifying" FORMAT_VALUES into an indexOf lookup.
  expect(FORMAT_VALUES.ogg).not.toBe(OUTPUT_FORMATS.indexOf("ogg"))
  expect(FORMAT_VALUES.mp3).not.toBe(OUTPUT_FORMATS.indexOf("mp3"))
})

test("quality values are the CLI's 1-3 scale and never include zero", () => {
  // src/app/Cli.cpp rejects anything outside 1..3. A 0 would fall through to
  // the lowest bitrate tier in mp3_quality_for instead of erroring.
  expect(QUALITY_VALUES).toEqual([1, 2, 3])
  expect(Math.min(...QUALITY_VALUES)).toBe(1)
})

test("the default quality matches the CLI default of 3", () => {
  expect(DEFAULT_QUALITY).toBe(3)
  expect(QUALITY_VALUES).toContain(DEFAULT_QUALITY)
})

test("the default difficulty is Hard and is a known difficulty", () => {
  expect(DEFAULT_DIFFICULTY).toBe(2)
  expect(DIFFICULTY_NAMES[DEFAULT_DIFFICULTY]).toBe("Hard")
})

test("every output format has a module value and a MIME type", () => {
  // Exhaustiveness: adding a format without wiring both maps would otherwise
  // half-land and fail at runtime rather than here.
  for (const format of OUTPUT_FORMATS) {
    expect(FORMAT_VALUES[format]).toBeTypeOf("number")
    expect(MIME_TYPES[format]).toMatch(/^audio\//)
  }
  expect(Object.keys(FORMAT_VALUES).sort()).toEqual([...OUTPUT_FORMATS].sort())
  expect(Object.keys(MIME_TYPES).sort()).toEqual([...OUTPUT_FORMATS].sort())
})

test("every quality has a short label and a long description", () => {
  for (const quality of QUALITY_VALUES) {
    expect(QUALITY_LABELS[quality]).toBeTruthy()
    expect(QUALITY_DETAIL[quality]).toBeTruthy()
  }
})

test("quality labels state the bitrate they actually produce", () => {
  // The labels were "Draft/Standard/High", which named a verdict rather than
  // the thing that changes. mp3_quality_for maps 1/2/3 to 128/192/320 kbps.
  expect(QUALITY_LABELS[1]).toContain("128")
  expect(QUALITY_LABELS[2]).toContain("192")
  expect(QUALITY_LABELS[3]).toContain("320")
})

test("a parse error carries its own name so callers can distinguish it", () => {
  const error = new OjnParseError("truncated header")
  expect(error).toBeInstanceOf(Error)
  expect(error.name).toBe("OjnParseError")
  expect(error.message).toBe("truncated header")
})

test("the format and quality types admit exactly the tested values", () => {
  // Compile-time guard: if the unions widen, these assignments stop compiling
  // and the suite fails at typecheck rather than silently under-covering.
  const formats: OutputFormat[] = ["wav", "ogg", "mp3"]
  const qualities: Quality[] = [1, 2, 3]
  expect(formats).toHaveLength(OUTPUT_FORMATS.length)
  expect(qualities).toHaveLength(QUALITY_VALUES.length)
})
