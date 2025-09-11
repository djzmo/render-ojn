using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace O2JamConverter
{
    public static class OjmParser
    {
        private static readonly Encoding KoreanEncoding = Encoding.GetEncoding("EUC-KR");

        private static readonly byte[] RearrangeTable = {
            0x10, 0x0E, 0x02, 0x09, 0x04, 0x00, 0x07, 0x01, 0x06, 0x08, 0x0F, 0x0A, 0x05, 0x0C, 0x03, 0x0D,
            0x0B, 0x07, 0x02, 0x0A, 0x0B, 0x03, 0x05, 0x0D, 0x08, 0x04, 0x00, 0x0C, 0x06, 0x0F, 0x0E, 0x10,
            0x01, 0x09, 0x0C, 0x0D, 0x03, 0x00, 0x06, 0x09, 0x0A, 0x01, 0x07, 0x08, 0x10, 0x02, 0x0B, 0x0E,
            0x04, 0x0F, 0x05, 0x08, 0x03, 0x04, 0x0D, 0x06, 0x05, 0x0B, 0x10, 0x02, 0x0C, 0x07, 0x09, 0x0A,
            0x0F, 0x0E, 0x00, 0x01, 0x0F, 0x02, 0x0C, 0x0D, 0x00, 0x04, 0x01, 0x05, 0x07, 0x03, 0x09, 0x10,
            0x06, 0x0B, 0x0A, 0x08, 0x0E, 0x00, 0x04, 0x0B, 0x10, 0x0F, 0x0D, 0x0C, 0x06, 0x05, 0x07, 0x01,
            0x02, 0x03, 0x08, 0x09, 0x0A, 0x0E, 0x03, 0x10, 0x08, 0x07, 0x06, 0x09, 0x0E, 0x0D, 0x00, 0x0A,
            0x0B, 0x04, 0x05, 0x0C, 0x02, 0x01, 0x0F, 0x04, 0x0E, 0x10, 0x0F, 0x05, 0x08, 0x07, 0x0B, 0x00,
            0x01, 0x06, 0x02, 0x0C, 0x09, 0x03, 0x0A, 0x0D, 0x06, 0x0D, 0x0E, 0x07, 0x10, 0x0A, 0x0B, 0x00,
            0x01, 0x0C, 0x0F, 0x02, 0x03, 0x08, 0x09, 0x04, 0x05, 0x0A, 0x0C, 0x00, 0x08, 0x09, 0x0D, 0x03,
            0x04, 0x05, 0x10, 0x0E, 0x0F, 0x01, 0x02, 0x0B, 0x06, 0x07, 0x05, 0x06, 0x0C, 0x04, 0x0D, 0x0F,
            0x07, 0x0E, 0x08, 0x01, 0x09, 0x02, 0x10, 0x0A, 0x0B, 0x00, 0x03, 0x0B, 0x0F, 0x04, 0x0E, 0x03,
            0x01, 0x00, 0x02, 0x0D, 0x0C, 0x06, 0x07, 0x05, 0x10, 0x09, 0x08, 0x0A, 0x03, 0x02, 0x01, 0x00,
            0x04, 0x0C, 0x0D, 0x0B, 0x10, 0x05, 0x06, 0x0F, 0x0E, 0x07, 0x09, 0x0A, 0x08, 0x09, 0x0A, 0x00,
            0x07, 0x08, 0x06, 0x10, 0x03, 0x04, 0x01, 0x02, 0x05, 0x0B, 0x0E, 0x0F, 0x0D, 0x0C, 0x0A, 0x06,
            0x09, 0x0C, 0x0B, 0x10, 0x07, 0x08, 0x00, 0x0F, 0x03, 0x01, 0x02, 0x05, 0x0D, 0x0E, 0x04, 0x0D,
            0x00, 0x01, 0x0E, 0x02, 0x03, 0x08, 0x0B, 0x07, 0x0C, 0x09, 0x05, 0x0A, 0x0F, 0x04, 0x06, 0x10,
            0x01, 0x0E, 0x02, 0x03, 0x0D, 0x0B, 0x07, 0x00, 0x08, 0x0C, 0x09, 0x06, 0x0F, 0x10, 0x05, 0x0A,
            0x04, 0x00
        };

        private struct WaveFormatHeader
        {
            public short AudioFormat;
            public short NumChannels;
            public int SampleRate;
            public int BitRate;
            public short BlockAlign;
            public short BitsPerSample;
        }

        public static List<Sample> Parse(string ojmFilePath)
        {
            if (!File.Exists(ojmFilePath))
                throw new FileNotFoundException("OJM file not found.", ojmFilePath);

            using (var reader = new BinaryReader(File.OpenRead(ojmFilePath)))
            {
                string signature = KoreanEncoding.GetString(reader.ReadBytes(4));
                if (signature.StartsWith("M30"))
                {
                    return ParseM30(reader);
                }
                else if (signature.StartsWith("OMC") || signature.StartsWith("OJM"))
                {
                    return ParseOMC(reader);
                }
                else
                {
                    throw new InvalidDataException("Unsupported OJM file format.");
                }
            }
        }

        private static List<Sample> ParseM30(BinaryReader reader)
        {
            // Placeholder for M30 parsing. The C++ code didn't detail this as much as OMC.
            // For now, returning an empty list as the primary focus is OMC.
            return new List<Sample>();
        }

        private static List<Sample> ParseOMC(BinaryReader reader)
        {
            var samples = new List<Sample>();
            byte accKeyByte = 0xFF;
            int accCounter = 0;

            ushort nWav = reader.ReadUInt16();
            ushort nOgg = reader.ReadUInt16();
            uint wavOffset = reader.ReadUInt32();
            uint oggOffset = reader.ReadUInt32();
            reader.ReadUInt32(); // Total filesize, unused

            reader.BaseStream.Seek(wavOffset, SeekOrigin.Begin);

            for (int i = 0; i < nWav; i++)
            {
                var sample = new Sample { RefID = (ushort)(i + 1) };
                sample.Name = KoreanEncoding.GetString(reader.ReadBytes(32)).TrimEnd('\0');

                var waveHeader = new WaveFormatHeader
                {
                    AudioFormat = reader.ReadInt16(),
                    NumChannels = reader.ReadInt16(),
                    SampleRate = reader.ReadInt32(),
                    BitRate = reader.ReadInt32(),
                    BlockAlign = reader.ReadInt16(),
                    BitsPerSample = reader.ReadInt16()
                };
                reader.ReadBytes(4); // "data" chunk id
                int chunkSize = reader.ReadInt32();

                if (chunkSize == 0) continue;

                byte[] encryptedData = reader.ReadBytes(chunkSize);
                byte[] decryptedData = DecodeWave(encryptedData, ref accKeyByte, ref accCounter);

                using (var ms = new MemoryStream())
                using (var writer = new BinaryWriter(ms))
                {
                    writer.Write(Encoding.ASCII.GetBytes("RIFF"));
                    writer.Write(36 + decryptedData.Length);
                    writer.Write(Encoding.ASCII.GetBytes("WAVE"));
                    writer.Write(Encoding.ASCII.GetBytes("fmt "));
                    writer.Write(16); // PCM chunk size
                    writer.Write(waveHeader.AudioFormat);
                    writer.Write(waveHeader.NumChannels);
                    writer.Write(waveHeader.SampleRate);
                    writer.Write(waveHeader.BitRate);
                    writer.Write(waveHeader.BlockAlign);
                    writer.Write(waveHeader.BitsPerSample);
                    writer.Write(Encoding.ASCII.GetBytes("data"));
                    writer.Write(decryptedData.Length);
                    writer.Write(decryptedData);
                    sample.AudioData = ms.ToArray();
                }
                samples.Add(sample);
            }

            if (oggOffset > 0 && reader.BaseStream.Position != oggOffset)
            {
                 reader.BaseStream.Seek(oggOffset, SeekOrigin.Begin);
            }

            for (int i = 0; i < nOgg; i++)
            {
                 var sample = new Sample { RefID = (ushort)(i + 1001) };
                 sample.Name = KoreanEncoding.GetString(reader.ReadBytes(32)).TrimEnd('\0');
                 int filesize = reader.ReadInt32();
                 if (filesize > 0 && reader.BaseStream.Position + filesize <= reader.BaseStream.Length)
                 {
                     sample.AudioData = reader.ReadBytes(filesize);
                     samples.Add(sample);
                 }
            }

            return samples;
        }

        private static byte[] DecodeWave(byte[] encryptedData, ref byte accKeyByte, ref int accCounter)
        {
            int length = encryptedData.Length;
            byte[] sourceData = (byte[])encryptedData.Clone();
            byte[] rearrangedData = new byte[length];

            // 1. Rearrange
            int key = ((length % 17) << 4) + (length % 17);
            int blockSize = length / 17;

            for (int block = 0; block < 17; block++)
            {
                int destOffset = blockSize * block;
                int srcOffset = blockSize * RearrangeTable[key];

                if(destOffset + blockSize > length || srcOffset + blockSize > length) continue;

                Buffer.BlockCopy(sourceData, srcOffset, rearrangedData, destOffset, blockSize);
                key++;
            }

            int remainder = length % 17;
            if (remainder > 0)
            {
                int remainderOffset = blockSize * 17;
                Buffer.BlockCopy(sourceData, remainderOffset, rearrangedData, remainderOffset, remainder);
            }

            // 2. ACCXOR
            byte[] decryptedData = new byte[length];
            byte tmp;
            byte currentByte;

            for (int i = 0; i < length; i++)
            {
                tmp = rearrangedData[i];
                currentByte = rearrangedData[i];

                if (((accKeyByte << accCounter) & 0x80) != 0)
                {
                    currentByte = (byte)~currentByte;
                }

                decryptedData[i] = currentByte;
                accCounter++;

                if (accCounter > 7)
                {
                    accCounter = 0;
                    accKeyByte = tmp;
                }
            }

            return decryptedData;
        }
    }
}
