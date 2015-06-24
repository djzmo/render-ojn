#ifndef NX_O2JAM_OJN_HPP
#define NX_O2JAM_OJN_HPP

#include "OJM.hpp"

namespace Nx
{
	namespace O2Jam
	{
		struct OJNHeader
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

		struct OJNEvent
		{
			unsigned short RefID;
			char Volume;
			char Pan;
			unsigned char NoteType;
			float TimePos;

			unsigned int Measure;
			unsigned int Grid;
			bool Hit;
		};

		struct OJNEventSet
		{
			unsigned int Measure; /* Up to ~4bil measures */
			unsigned short Channel; /* Up to 65536 channels */
			unsigned short NumEvents; /* Up to 65535 events */
		};

		enum OJNEventChannel
		{
			CHANNEL_MEASURE = 0,
			CHANNEL_BPM,
			CHANNEL_NOTE1,
			CHANNEL_NOTE2,
			CHANNEL_NOTE3,
			CHANNEL_NOTE4,
			CHANNEL_NOTE5,
			CHANNEL_NOTE6,
			CHANNEL_NOTE7,
			CHANNEL_BGSOUND
		};

		enum OJNNoteDifficulty
		{
			DIFF_EASY = 0,
			DIFF_NORMAL,
			DIFF_HARD
		};

		class OJN
		{
			std::string m_path;
			OJNHeader m_header;
			std::vector<OJNEvent> m_events;
			OJM m_ojm;
			FMOD::System *m_fmod;
			float m_lastRenderGrid, m_lastRenderTime;
			float m_lastRenderBPM;

			bool ReadHeader();
			float CalculateTime(unsigned int measure, unsigned int grid, unsigned int numObjects, float bpm);

		public:
			OJN(const std::string &path, FMOD::System *fmod);
			~OJN();

			void GetHeader(OJNHeader *header);
			void SetHeader(OJNHeader header);

			void LoadEvents(unsigned char difficulty = 2);
			void UnloadEvents();

			void RenderToFile(const std::string &out_path, unsigned char difficulty = 2);
			static void RenderToFileAsync(OJN *instance, const std::string &out_path);

			void Save();

			void Close();
			static bool EventComparison(const OJNEvent &e1, const OJNEvent &e2);
		};
	}
}

#endif