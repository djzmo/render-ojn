namespace O2JamConverter
{
    // Corresponds to the MusicHeader struct in the C++ project.
    // Note: The char arrays (strings) will be handled during binary reading.
    public class MusicHeader
    {
        public uint NewSongID;
        public uint FileSignature;
        public float NewEncVersion;
        public uint NewGenreCode;
        public float Tempo;
        public ushort[] Level { get; set; } = new ushort[3];
        public uint[] NumEvents { get; set; } = new uint[3];
        public uint[] NumNotes { get; set; } = new uint[3];
        public uint[] NumMeasures { get; set; } = new uint[3];
        public uint[] NumNoteSets { get; set; } = new uint[3];
        public ushort OldEncVersion;
        public ushort OldSongID;
        public string OldGenre { get; set; } = "";
        public uint OldCoverArtSize;
        public float NoteChartVersion;
        public string Title { get; set; } = "";
        public string Artist { get; set; } = "";
        public string Charter { get; set; } = "";
        public string OJMFile { get; set; } = "";
        public uint NewCoverArtSize;
        public uint[] Duration { get; set; } = new uint[3];
        public uint[] DataOffset { get; set; } = new uint[4];
    }

    // Base class for musical events.
    public abstract class Event
    {
        public uint Measure;
        public uint Grid;
        public float Time;
        public bool IsApplied;
    }

    // Represents a tempo change event.
    public class TempoEvent : Event
    {
        public float Value;
    }

    // Represents a note/sound event.
    public class SoundEvent : Event
    {
        public ushort RefID;
        public sbyte Volume;
        public sbyte Pan;
        public byte NoteType;
    }

    // Represents a decoded audio sample.
    public class Sample
    {
        public ushort RefID;
        public uint Filesize;
        public byte BankType;
        public string Name { get; set; } = "";
        // In the C# version, this will hold the raw WAV data.
        public byte[]? AudioData { get; set; }
    }
}
