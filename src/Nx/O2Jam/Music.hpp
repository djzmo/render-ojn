#ifndef NX_O2JAM_MUSIC_HPP
#define NX_O2JAM_MUSIC_HPP

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <fmod.hpp>
#include <fmod_errors.h>

namespace Nx
{
	namespace O2Jam
	{
		struct MusicHeader
		{
			unsigned int NewSongID;
			unsigned int FileSignature;
			float NewEncVersion;
			unsigned int NewGenreCode;
			float Tempo; /* in BPM */
			unsigned short Level[3]; /* Level[3] + Padding[1] */
			unsigned int NumEvents[3];
			unsigned int NumNotes[3];
			unsigned int NumMeasures[3];
			unsigned int NumNoteSets[3];
			unsigned short OldEncVersion;
			unsigned short OldSongID;
			char OldGenre[20]; /* preferred over GenreCode */
			unsigned int OldCoverArtSize; /* BMP music cover */
			float NoteChartVersion; /* chart versioning for notecharters */
			char Title[64];
			char Artist[32];
			char Charter[32];
			char OJMFile[32]; /* OJM file name */
			unsigned int NewCoverArtSize; /* JPEG music cover. My proposal: add PNG support */
			unsigned int Duration[3]; /* length of the chart, in seconds */
			unsigned int DataOffset[4]; /* NoteOffset[3] + newCoverArtOffset[1] */
		};

		struct Event
		{
			unsigned int Measure;
			unsigned int Grid;
			float Time;
			bool IsApplied;
		};

		struct TempoEvent : public Event
		{
			float Value;
		};

		struct SoundEvent : public Event
		{
			unsigned short RefID;
			char Volume;
			char Pan;
			unsigned char NoteType;
		};

		struct EventSet
		{
			unsigned int Measure; /* Up to ~4bil measures */
			unsigned short Channel; /* Up to 65536 channels */
			unsigned short NumEvents; /* Up to 65535 events */
		};

		namespace EventChannel
		{
			enum
			{
				TimeSignature,
				Tempo,
				Note1,
				Note2,
				Note3,
				Note4,
				Note5,
				Note6,
				Note7,
				BGSound
			};
		}

		

		namespace SamplePackageFormat
		{
			enum
			{
				OMC, M30
			};
		}

		namespace SampleEncoding
		{
			enum
			{
				Nami = 16
			};
		}

		namespace OutputFormat
		{
			enum
			{
				WAV, MP3
			};
		}

		struct WaveHeader
		{
			short AudioFormat;
			short NumChannels;
			int SampleRate;
			int BitRate;
			short BlockAlign;
			short BitsPerSample;
		};

		struct Sample
		{
			unsigned short RefID;
			unsigned int Filesize;
			unsigned char BankType;
			char Name[32];
			FMOD::Sound *Data;
			bool AsStream;
			unsigned char *BinData;
		};

		struct FindSample : std::unary_function<Sample, bool>
		{
			unsigned int RefID;

			FindSample(unsigned int RefID) : RefID(RefID) {}

			bool operator ()(Sample const &sample) const
			{
				return sample.RefID == RefID;
			}
		};

		struct SamplePackageHeader
		{
			unsigned int Filesize;
			unsigned char Type;
			unsigned int EncType;
		};

		static const unsigned char RearrangeTable[] = {
		0x10, 0x0E, 0x02, 0x09, 0x04, 0x00, 0x07, 0x01,
		0x06, 0x08, 0x0F, 0x0A, 0x05, 0x0C, 0x03, 0x0D,
		0x0B, 0x07, 0x02, 0x0A, 0x0B, 0x03, 0x05, 0x0D,
		0x08, 0x04, 0x00, 0x0C, 0x06, 0x0F, 0x0E, 0x10,
		0x01, 0x09, 0x0C, 0x0D, 0x03, 0x00, 0x06, 0x09,
		0x0A, 0x01, 0x07, 0x08, 0x10, 0x02, 0x0B, 0x0E,
		0x04, 0x0F, 0x05, 0x08, 0x03, 0x04, 0x0D, 0x06,
		0x05, 0x0B, 0x10, 0x02, 0x0C, 0x07, 0x09, 0x0A,
		0x0F, 0x0E, 0x00, 0x01, 0x0F, 0x02, 0x0C, 0x0D,
		0x00, 0x04, 0x01, 0x05, 0x07, 0x03, 0x09, 0x10,
		0x06, 0x0B, 0x0A, 0x08, 0x0E, 0x00, 0x04, 0x0B,
		0x10, 0x0F, 0x0D, 0x0C, 0x06, 0x05, 0x07, 0x01,
		0x02, 0x03, 0x08, 0x09, 0x0A, 0x0E, 0x03, 0x10,
		0x08, 0x07, 0x06, 0x09, 0x0E, 0x0D, 0x00, 0x0A,
		0x0B, 0x04, 0x05, 0x0C, 0x02, 0x01, 0x0F, 0x04,
		0x0E, 0x10, 0x0F, 0x05, 0x08, 0x07, 0x0B, 0x00,
		0x01, 0x06, 0x02, 0x0C, 0x09, 0x03, 0x0A, 0x0D,
		0x06, 0x0D, 0x0E, 0x07, 0x10, 0x0A, 0x0B, 0x00,
		0x01, 0x0C, 0x0F, 0x02, 0x03, 0x08, 0x09, 0x04,
		0x05, 0x0A, 0x0C, 0x00, 0x08, 0x09, 0x0D, 0x03,
		0x04, 0x05, 0x10, 0x0E, 0x0F, 0x01, 0x02, 0x0B,
		0x06, 0x07, 0x05, 0x06, 0x0C, 0x04, 0x0D, 0x0F,
		0x07, 0x0E, 0x08, 0x01, 0x09, 0x02, 0x10, 0x0A,
		0x0B, 0x00, 0x03, 0x0B, 0x0F, 0x04, 0x0E, 0x03,
		0x01, 0x00, 0x02, 0x0D, 0x0C, 0x06, 0x07, 0x05,
		0x10, 0x09, 0x08, 0x0A, 0x03, 0x02, 0x01, 0x00,
		0x04, 0x0C, 0x0D, 0x0B, 0x10, 0x05, 0x06, 0x0F,
		0x0E, 0x07, 0x09, 0x0A, 0x08, 0x09, 0x0A, 0x00,
		0x07, 0x08, 0x06, 0x10, 0x03, 0x04, 0x01, 0x02,
		0x05, 0x0B, 0x0E, 0x0F, 0x0D, 0x0C, 0x0A, 0x06,
		0x09, 0x0C, 0x0B, 0x10, 0x07, 0x08, 0x00, 0x0F,
		0x03, 0x01, 0x02, 0x05, 0x0D, 0x0E, 0x04, 0x0D,
		0x00, 0x01, 0x0E, 0x02, 0x03, 0x08, 0x0B, 0x07,
		0x0C, 0x09, 0x05, 0x0A, 0x0F, 0x04, 0x06, 0x10,
		0x01, 0x0E, 0x02, 0x03, 0x0D, 0x0B, 0x07, 0x00,
		0x08, 0x0C, 0x09, 0x06, 0x0F, 0x10, 0x05, 0x0A,
		0x04, 0x00};



		namespace PatternDifficulty
		{
			enum
			{
				Easy, Normal, Hard
			};
		}

		class Music
		{
			FMOD::System *m_fmod;

			std::string m_path;
			MusicHeader m_header;
			unsigned int m_loadedDiff;
			bool m_isOK;
			float m_lastRenderGrid, m_lastRenderTime;
			float m_currentRenderTempo;

			int m_asyncProgress;
			int m_accKeyByte, m_accCounter;

			std::vector<Sample> m_samples;
			std::vector<SoundEvent> m_soundEvents;
			std::vector<TempoEvent> m_tempoEvents;

			bool ReadHeader();
			void LoadM30Package(std::fstream &fs, bool stream);
			void LoadOMCPackage(std::fstream &fs, bool stream);
			void DecodeWave(unsigned char *in, unsigned char *out, int length);

			float CalculateTime(unsigned int measure, unsigned int grid, unsigned int numObjects, float bpm);

		public:
			Music(const std::string &path, FMOD::System *fmod);
			~Music();

			MusicHeader GetHeader();
			void SetHeader(MusicHeader header);

			static bool LoadSamplesAsync(Music *instance, bool stream = true);
			bool LoadSamples(bool stream = true);
			void UnloadSamples();

			static bool LoadEventsAsync(Music *instance, unsigned char difficulty = 2);
			bool LoadEvents(unsigned char difficulty = 2);
			void UnloadEvents();

			std::vector<SoundEvent> *GetSoundEvents();
			std::vector<TempoEvent> *GetTempoEvents();
			std::vector<Sample> *GetSamples();

			int GetLoadedDifficulty();
			int GetAsyncProgress();

			bool IsOK() { return m_isOK; }

			static bool SortEventCallback(const Event &e1, const Event &e2);
		};
	}
}

#endif