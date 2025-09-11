using System.IO;
using System.Text;

namespace O2JamConverter
{
    public static class OjnParser
    {
        // Note: Call Encoding.RegisterProvider(CodePagesEncodingProvider.Instance); at startup.
        private static readonly Encoding KoreanEncoding = Encoding.GetEncoding("EUC-KR");

        public static MusicHeader ParseHeader(string ojnFilePath)
        {
            var header = new MusicHeader();
            using (var reader = new BinaryReader(File.OpenRead(ojnFilePath)))
            {
                header.NewSongID = reader.ReadUInt32();
                header.FileSignature = reader.ReadUInt32();

                if (header.FileSignature != 7236207)
                {
                    throw new InvalidDataException("The file is not a valid OJN file. Signature mismatch.");
                }

                header.NewEncVersion = reader.ReadSingle();
                header.NewGenreCode = reader.ReadUInt32();
                header.Tempo = reader.ReadSingle();
                header.Level[0] = reader.ReadUInt16();
                header.Level[1] = reader.ReadUInt16();
                header.Level[2] = reader.ReadUInt16();
                reader.ReadBytes(2); // Padding

                header.NumEvents[0] = reader.ReadUInt32();
                header.NumEvents[1] = reader.ReadUInt32();
                header.NumEvents[2] = reader.ReadUInt32();

                header.NumNotes[0] = reader.ReadUInt32();
                header.NumNotes[1] = reader.ReadUInt32();
                header.NumNotes[2] = reader.ReadUInt32();

                header.NumMeasures[0] = reader.ReadUInt32();
                header.NumMeasures[1] = reader.ReadUInt32();
                header.NumMeasures[2] = reader.ReadUInt32();

                header.NumNoteSets[0] = reader.ReadUInt32();
                header.NumNoteSets[1] = reader.ReadUInt32();
                header.NumNoteSets[2] = reader.ReadUInt32();

                header.OldEncVersion = reader.ReadUInt16();
                header.OldSongID = reader.ReadUInt16();

                header.OldGenre = KoreanEncoding.GetString(reader.ReadBytes(20)).TrimEnd('\0');
                header.OldCoverArtSize = reader.ReadUInt32();
                header.NoteChartVersion = reader.ReadSingle();

                header.Title = KoreanEncoding.GetString(reader.ReadBytes(64)).TrimEnd('\0');
                header.Artist = KoreanEncoding.GetString(reader.ReadBytes(32)).TrimEnd('\0');
                header.Charter = KoreanEncoding.GetString(reader.ReadBytes(32)).TrimEnd('\0');
                header.OJMFile = KoreanEncoding.GetString(reader.ReadBytes(32)).TrimEnd('\0');

                header.NewCoverArtSize = reader.ReadUInt32();

                header.Duration[0] = reader.ReadUInt32();
                header.Duration[1] = reader.ReadUInt32();
                header.Duration[2] = reader.ReadUInt32();

                header.DataOffset[0] = reader.ReadUInt32();
                header.DataOffset[1] = reader.ReadUInt32();
                header.DataOffset[2] = reader.ReadUInt32();
                header.DataOffset[3] = reader.ReadUInt32();
            }
            return header;
        }

        public static (List<SoundEvent> soundEvents, List<TempoEvent> tempoEvents) ParseEvents(string ojnFilePath, MusicHeader header, int difficulty)
        {
            if (difficulty < 0 || difficulty > 2)
                throw new ArgumentOutOfRangeException(nameof(difficulty), "Difficulty must be between 0 and 2.");

            var soundEvents = new List<SoundEvent>();
            var tempoEvents = new List<TempoEvent>();

            // State for time calculation
            float lastRenderGrid = 0.0f;
            float lastRenderTime = 0.0f;
            float currentRenderTempo = header.Tempo;

            // Local function to replicate C++ CalculateTime
            float CalculateTime(uint measure, uint grid)
            {
                float mspb = 60.0f / currentRenderTempo * 1000.0f;
                float totalGrid = (float)measure * 192.0f + (float)grid;
                float beats = (totalGrid - lastRenderGrid) / 48.0f;

                float result = mspb * beats;
                lastRenderGrid = totalGrid;
                lastRenderTime += result;

                return lastRenderTime;
            }

            using (var reader = new BinaryReader(File.OpenRead(ojnFilePath)))
            {
                long startOffset = header.DataOffset[difficulty];
                // if the next offset is 0, it means it's the last difficulty, so we read to the end of the file
                long endOffset = (difficulty + 1 < header.DataOffset.Length && header.DataOffset[difficulty + 1] > 0)
                               ? header.DataOffset[difficulty + 1]
                               : reader.BaseStream.Length;


                reader.BaseStream.Seek(startOffset, SeekOrigin.Begin);

                while (reader.BaseStream.Position < endOffset)
                {
                    uint measure = reader.ReadUInt32();
                    ushort channel = reader.ReadUInt16();
                    ushort numEvents = reader.ReadUInt16();

                    if (numEvents == 0) continue;

                    for (int i = 0; i < numEvents; i++)
                    {
                        uint grid = (uint)(i * (192.0f / numEvents));

                        if (channel == 1) // Tempo event
                        {
                            float tempoValue = reader.ReadSingle();
                            if (tempoValue > 0)
                            {
                                var tempoEvent = new TempoEvent
                                {
                                    Measure = measure,
                                    Grid = grid,
                                    Value = tempoValue,
                                    Time = CalculateTime(measure, grid)
                                };
                                currentRenderTempo = tempoValue;
                                tempoEvents.Add(tempoEvent);
                            }
                        }
                        else if (channel >= 2 && channel <= 8) // Sound events (notes)
                        {
                            ushort refId = reader.ReadUInt16();
                            if (refId > 0)
                            {
                                sbyte volume = reader.ReadSByte();
                                sbyte pan = reader.ReadSByte();

                                var soundEvent = new SoundEvent
                                {
                                    Measure = measure,
                                    Grid = grid,
                                    RefID = refId,
                                    Volume = volume,
                                    Pan = pan,
                                    Time = CalculateTime(measure, grid)
                                };
                                soundEvents.Add(soundEvent);
                            }
                            else
                            {
                                reader.ReadBytes(2); // Skip volume/pan
                            }
                        }
                        else // Other channels (e.g. time signature, etc.), just skip the data
                        {
                            reader.ReadBytes(4);
                        }
                    }
                }
            }

            // Sort events by time, as the rendering logic will rely on this order.
            soundEvents = soundEvents.OrderBy(e => e.Time).ToList();
            tempoEvents = tempoEvents.OrderBy(e => e.Time).ToList();

            return (soundEvents, tempoEvents);
        }
    }
}
