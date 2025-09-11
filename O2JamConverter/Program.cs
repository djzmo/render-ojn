using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using CommandLine;

namespace O2JamConverter
{
    class Program
    {
        public class Options
        {
            [Option('i', "input", Required = true, HelpText = "Input OJN file or directory containing OJN files.")]
            public string InputPath { get; set; } = default!;

            [Option('o', "output", Required = false, HelpText = "Output directory to save MP3 files. Defaults to a directory named 'output' in the input path.")]
            public string? OutputPath { get; set; }
        }

        static void Main(string[] args)
        {
            // Register the provider for handling Korean encodings
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

            Parser.Default.ParseArguments<Options>(args)
                   .WithParsed<Options>(o =>
                   {
                       RunConversion(o);
                   });
        }

        static void RunConversion(Options opts)
        {
            string inputPath = Path.GetFullPath(opts.InputPath);
            string outputRootPath;

            List<string> ojnFiles = new List<string>();

            if (File.Exists(inputPath))
            {
                if (Path.GetExtension(inputPath).ToLower() != ".ojn")
                {
                    Console.WriteLine("Error: Input file must be an .ojn file.");
                    return;
                }
                ojnFiles.Add(inputPath);
                string? inputDir = Path.GetDirectoryName(inputPath);
                outputRootPath = opts.OutputPath ?? Path.Combine(inputDir ?? "", "output");
            }
            else if (Directory.Exists(inputPath))
            {
                ojnFiles.AddRange(Directory.GetFiles(inputPath, "*.ojn", SearchOption.AllDirectories));
                if (!ojnFiles.Any())
                {
                    Console.WriteLine($"No .ojn files found in '{inputPath}'.");
                    return;
                }
                outputRootPath = opts.OutputPath ?? Path.Combine(inputPath, "output");
                 Console.WriteLine($"Found {ojnFiles.Count} .ojn files. Starting batch conversion...");
            }
            else
            {
                Console.WriteLine($"Error: Input path '{inputPath}' is not a valid file or directory.");
                return;
            }

            foreach (var ojnFile in ojnFiles)
            {
                string outputDir = outputRootPath;
                // If processing a directory, maintain sub-directory structure in the output
                if (Directory.Exists(inputPath))
                {
                    string relativeDir = Path.GetDirectoryName(Path.GetRelativePath(inputPath, ojnFile)) ?? "";
                    outputDir = Path.Combine(outputRootPath, relativeDir);
                }
                ProcessFile(ojnFile, outputDir);
            }
             Console.WriteLine("\nConversion finished.");
        }

        static void ProcessFile(string ojnPath, string outputDir)
        {
            Console.WriteLine($"\nProcessing '{Path.GetFileName(ojnPath)}'...");
            try
            {
                // 1. Parse Header
                Console.WriteLine("  Parsing OJN header...");
                var header = OjnParser.ParseHeader(ojnPath);

                // 2. Parse OJM Samples
                string ojnDir = Path.GetDirectoryName(ojnPath) ?? "";
                string ojmPath = Path.Combine(ojnDir, header.OJMFile);
                if (!File.Exists(ojmPath))
                {
                    Console.WriteLine($"  Error: OJM file not found at '{ojmPath}'");
                    return;
                }
                Console.WriteLine("  Parsing OJM samples...");
                var samples = OjmParser.Parse(ojmPath);

                // 3. Parse Events (using Hard difficulty by default, as per original C++ app)
                Console.WriteLine("  Parsing note events...");
                var (soundEvents, _) = OjnParser.ParseEvents(ojnPath, header, 2); // 0=E, 1=N, 2=H

                if (!soundEvents.Any())
                {
                    Console.WriteLine("  No sound events found in the selected difficulty.");
                    return;
                }

                // 4. Render to WAV
                Directory.CreateDirectory(outputDir);
                string tempWavPath = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".wav");
                string finalMp3Path = Path.Combine(outputDir, Path.GetFileNameWithoutExtension(ojnPath) + ".mp3");

                Console.WriteLine("  Rendering to temporary WAV file...");
                MusicRenderer.RenderToFile(tempWavPath, soundEvents, samples);

                // 5. Encode to MP3
                Console.WriteLine("  Encoding to MP3...");
                bool success = Encoder.EncodeToMp3(tempWavPath, finalMp3Path);

                // 6. Clean up temp file
                File.Delete(tempWavPath);

                if (success)
                {
                    // 7. Tag MP3
                    Console.WriteLine("  Tagging MP3 file...");
                    Encoder.TagMp3(finalMp3Path, header);
                    Console.WriteLine($"  Successfully converted to '{finalMp3Path}'");
                }
                else
                {
                     Console.WriteLine("  Failed to convert to MP3. Ensure 'lame' is installed and in your system PATH.");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  An error occurred: {ex.Message}");
                // For debugging: Console.WriteLine(ex.StackTrace);
            }
        }
    }
}
