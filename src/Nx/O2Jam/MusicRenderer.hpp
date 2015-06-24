#ifndef NX_O2JAM_MUSICRENDERER_HPP
#define NX_O2JAM_MUSICRENDERER_HPP

#include <windows.h>
#include <cmath>
#include <iostream>
#include "Music.hpp"

namespace Nx
{
	namespace O2Jam
	{
		class MusicRenderer
		{
			FMOD::System *m_fmod;
			std::vector<Sample> *m_samples;
			std::vector<SoundEvent> *m_soundEvents;
			std::vector<TempoEvent> *m_tempoEvents;
			MusicHeader m_header;

			bool m_isRunning;
			unsigned int m_difficulty;
			float m_currentTempo;
			unsigned int m_currentMeasure, m_currentGrid;
			int m_asyncProgress;

			unsigned int m_elapsed;
			LARGE_INTEGER m_timerFreq, m_timerStart;
			bool m_isTimerRunning;

			void BeginTimer();
			void StopTimer();
			void UpdateTimer();
			unsigned int GetElapsedTime();

			int GetSample(unsigned int RefID);
			int GetAsyncProgress() { return m_asyncProgress; }

		public:
			MusicRenderer(FMOD::System *fmod);
			~MusicRenderer();

			void SetSamples(std::vector<Sample> *samples);
			void SetEvents(std::vector<SoundEvent> *soundEvents, std::vector<TempoEvent> *tempoEvents, unsigned int difficulty);
			void SetMusicHeader(MusicHeader header);

			void Abort();

			void RenderToSpeaker();
			static void RenderToSpeaker(MusicRenderer *instance);
			void RenderToFile();
			static void RenderToFile(MusicRenderer *instance);
		};
	}
}

#endif