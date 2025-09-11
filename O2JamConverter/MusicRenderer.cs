using NAudio.Wave;
using NAudio.Wave.SampleProviders;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace O2JamConverter
{
    public static class MusicRenderer
    {
        public static void RenderToFile(string outputPath, List<SoundEvent> soundEvents, List<Sample> samples)
        {
            if (!soundEvents.Any())
            {
                Console.WriteLine("No sound events to render.");
                return;
            }

            // Define a standard output format. NAudio's mixer will resample inputs to match.
            var outputFormat = new WaveFormat(44100, 16, 2);
            var mixer = new MixingSampleProvider(outputFormat);

            // Create a dictionary of sample data for quick lookup.
            var sampleData = samples.Where(s => s.AudioData != null)
                                    .ToDictionary(s => s.RefID, s => s.AudioData);

            foreach (var soundEvent in soundEvents)
            {
                if (sampleData.TryGetValue(soundEvent.RefID, out var audioBytes))
                {
                    // For each event, create a new reader from the sample's byte array.
                    var reader = new WaveFileReader(new MemoryStream(audioBytes!));

                    // Convert to a sample provider. The mixer will handle resampling if needed.
                    ISampleProvider sampleProvider = reader.ToSampleProvider();

                    // Apply volume and pan if the data is available in the event.
                    // Note: The original C++ code didn't seem to use volume/pan, but we support it here.
                    if (soundEvent.Volume != 0 || soundEvent.Pan != 0)
                    {
                        var panningProvider = new PanningSampleProvider(sampleProvider);
                        // Pan is -1 (left) to 1 (right). O2Jam is -100 to 100? Let's assume a simple mapping.
                        panningProvider.Pan = Math.Max(-1.0f, Math.Min(1.0f, soundEvent.Pan / 100.0f));
                        sampleProvider = panningProvider;

                        // Volume is not directly available in ISampleProvider, but we can wrap it.
                        // We'll skip volume for now to keep it simple, as it wasn't used in the C++ source.
                    }

                    // Schedule the sample to be played at the correct time.
                    var offsetProvider = new OffsetSampleProvider(sampleProvider)
                    {
                        DelayBy = TimeSpan.FromMilliseconds(soundEvent.Time)
                    };

                    mixer.AddMixerInput(offsetProvider);
                }
            }

            // Render the mixed audio to a 16-bit WAV file.
            WaveFileWriter.CreateWaveFile16(outputPath, mixer);
        }
    }
}
