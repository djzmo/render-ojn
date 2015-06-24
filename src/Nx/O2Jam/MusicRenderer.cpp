#include "MusicRenderer.hpp"

namespace Nx
{
	namespace O2Jam
	{
		MusicRenderer::MusicRenderer(FMOD::System *fmod)
		{
			m_fmod = fmod;
			m_isRunning = false;
			m_isTimerRunning = false;
			m_elapsed = 0;
			m_difficulty = -1;
			m_currentMeasure = 0;
			m_currentGrid = 0;
			m_asyncProgress = 0;
		}

		MusicRenderer::~MusicRenderer()
		{
		}

		void MusicRenderer::SetSamples(std::vector<Sample> *samples)
		{
			m_samples = samples;
		}

		void MusicRenderer::SetEvents(std::vector<SoundEvent> *soundEvents, std::vector<TempoEvent> *tempoEvents, unsigned int difficulty)
		{
			m_soundEvents = soundEvents;
			m_tempoEvents = tempoEvents;
			m_difficulty = difficulty;
		}

		void MusicRenderer::SetMusicHeader(MusicHeader header)
		{
			m_header = header;
		}

		void MusicRenderer::Abort()
		{
			m_isRunning = false;
		}

		int MusicRenderer::GetSample(unsigned int RefID)
		{
			for(std::vector<Sample>::size_type i = 0; i < m_samples->size(); i++)
			{
				if(m_samples->at(i).RefID == RefID)
					return i;
			}

			return -1;
		}

		void MusicRenderer::RenderToFile()
		{
			SoundEvent currentSoundEvent;

			float duration = m_header.Duration[m_difficulty] * 1000.0f + 1000;
			std::vector<SoundEvent>::size_type eventCtr = 0, totalEvents = m_soundEvents->size();
			float elapsed = 0.0f;
			float rate = 1024.0f / 48000.0f * 1000.0f;
			int percentage = 0;

			printf("0%%");

			while(elapsed < duration)
			{
				if(eventCtr < totalEvents)
				{
					currentSoundEvent = m_soundEvents->at(eventCtr);

					if(currentSoundEvent.Time <= elapsed)
					{
						if(currentSoundEvent.NoteType != 3)
						{
							int sampleIndex = -1;
							if(currentSoundEvent.NoteType == 4)
								sampleIndex = GetSample(currentSoundEvent.RefID + 1000);
							else
								sampleIndex = GetSample(currentSoundEvent.RefID);

							if(sampleIndex >= 0)
							{
								Sample sample = m_samples->at(sampleIndex);
								FMOD_RESULT result = m_fmod->playSound(FMOD_CHANNEL_FREE, sample.Data, false, 0);
							}
						}

						eventCtr++;
					}
				}

				m_asyncProgress = elapsed * 100 / duration;

				m_fmod->update();
				elapsed += rate;

				if(percentage < 10)
					printf("\b\b");
				else if(percentage < 100)
					printf("\b\b\b");
				else printf("\b\b\b\b");

				percentage = (int)(elapsed / duration * 100.f);
				printf("%d%%", percentage);
			}
		}

		void MusicRenderer::RenderToSpeaker()
		{
			SoundEvent currentSoundEvent;

			unsigned int duration = m_header.Duration[m_difficulty] * 1000 + 1000;
			std::vector<SoundEvent>::size_type eventCtr = 0, totalEvents = m_soundEvents->size();
			unsigned int elapsed = 0;
			int percentage = 0;

			printf("0%%");

			BeginTimer();
			while(elapsed < duration)
			{
				if(eventCtr < totalEvents)
				{
					currentSoundEvent = m_soundEvents->at(eventCtr);

					if(currentSoundEvent.Time <= elapsed)
					{
						if(currentSoundEvent.NoteType != 3)
						{
							int sampleIndex = -1;
							if(currentSoundEvent.NoteType == 4)
								sampleIndex = GetSample(currentSoundEvent.RefID + 1000);
							else
								sampleIndex = GetSample(currentSoundEvent.RefID);

							if(sampleIndex >= 0)
							{
								Sample sample = m_samples->at(sampleIndex);
								FMOD_RESULT result = m_fmod->playSound(FMOD_CHANNEL_FREE, sample.Data, false, 0);
							}
						}

						eventCtr++;
					}
				}

				m_fmod->update();
				UpdateTimer();
				elapsed = GetElapsedTime();

				if(percentage < 10)
					printf("\b\b");
				else if(percentage < 100)
					printf("\b\b\b");
				else printf("\b\b\b\b");

				percentage = (int)((float)elapsed / (float)duration * 100.f);
				printf("%d%%", percentage);
			}

			StopTimer();
		}

		void MusicRenderer::BeginTimer()
		{
			m_elapsed = 0;
			QueryPerformanceFrequency(&m_timerFreq);
			QueryPerformanceCounter(&m_timerStart);
			m_isTimerRunning = true;
		}

		void MusicRenderer::StopTimer()
		{
			m_isTimerRunning = false;
		}

		void MusicRenderer::UpdateTimer()
		{
			if(m_isTimerRunning)
			{
				LARGE_INTEGER elapsed;
				QueryPerformanceCounter(&elapsed);
				m_elapsed = static_cast<unsigned int>((elapsed.QuadPart - m_timerStart.QuadPart) * 1000 / m_timerFreq.QuadPart);
			}
		}

		unsigned int MusicRenderer::GetElapsedTime()
		{
			return m_elapsed;
		}
	}
}