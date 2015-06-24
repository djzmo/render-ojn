#include "OJN.hpp"

namespace Nx
{
	namespace O2Jam
	{
		OJN::OJN(const std::string &path, FMOD::System *fmod) : m_ojm(fmod)
		{
			m_path = path;
			m_fmod = fmod;
			m_lastRenderGrid = 0;
			m_lastRenderTime = 0;

			ReadHeader();
		}

		OJN::~OJN()
		{
		}

		bool OJN::ReadHeader()
		{
			std::fstream fs(m_path.c_str(), std::ios::in | std::ios::binary);

			if(!fs.is_open())
				return false;

			fs.read((char *)&m_header.NewSongID, 4);
			fs.read((char *)&m_header.FileSignature, 4);
			fs.read((char *)&m_header.NewEncVersion, 4);
			fs.read((char *)&m_header.NewGenreCode, 4);
			fs.read((char *)&m_header.Tempo, 4);

			fs.read((char *)&m_header.Level[0], 2);
			fs.read((char *)&m_header.Level[1], 2);
			fs.read((char *)&m_header.Level[2], 2);
			fs.seekg(2, std::ios::cur);

			fs.read((char *)&m_header.NumEvents[0], 4);
			fs.read((char *)&m_header.NumEvents[1], 4);
			fs.read((char *)&m_header.NumEvents[2], 4);

			fs.read((char *)&m_header.NumNotes[0], 4);
			fs.read((char *)&m_header.NumNotes[1], 4);
			fs.read((char *)&m_header.NumNotes[2], 4);

			fs.read((char *)&m_header.NumMeasures[0], 4);
			fs.read((char *)&m_header.NumMeasures[1], 4);
			fs.read((char *)&m_header.NumMeasures[2], 4);

			fs.read((char *)&m_header.NumNoteSets[0], 4);
			fs.read((char *)&m_header.NumNoteSets[1], 4);
			fs.read((char *)&m_header.NumNoteSets[2], 4);
			
			fs.read((char *)&m_header.OldEncVersion, 2);
			fs.read((char *)&m_header.OldSongID, 2);
			
			fs.read(m_header.OldGenre, 20);
			fs.read((char *)&m_header.OldCoverArtSize, 4);
			fs.read((char *)&m_header.NoteChartVersion, 4);
			
			fs.read(m_header.Title, 64);
			fs.read(m_header.Charter, 32);
			fs.read(m_header.Artist, 32);
			fs.read(m_header.OJMFile, 32);
			
			fs.read((char *)&m_header.NewCoverArtSize, 4);

			fs.read((char *)&m_header.Duration[0], 4);
			fs.read((char *)&m_header.Duration[1], 4);
			fs.read((char *)&m_header.Duration[2], 4);

			fs.read((char *)&m_header.DataOffset[0], 4);
			fs.read((char *)&m_header.DataOffset[1], 4);
			fs.read((char *)&m_header.DataOffset[2], 4);
			fs.read((char *)&m_header.DataOffset[3], 4);

			fs.close();

			return true;
		}

		void OJN::GetHeader(OJNHeader *header)
		{
			*header = m_header;
		}

		void OJN::SetHeader(OJNHeader header)
		{
			m_header = header;
		}

		float OJN::CalculateTime(unsigned int measure, unsigned int grid, unsigned int numObjects, float bpm)
		{
			/*float bpms = bpm / 60.0f / 1000.0f;
			float gm, gg, gn;
			float mul_divider = 192 / numObjects;
			gm = measure - m_lastRenderMeasure;
			if(measure > m_lastRenderMeasure + 1)
				gg = (192 - m_lastRenderGrid) * mul_divider;
			else
				gg = (grid - m_lastRenderGrid) * mul_divider;*/
			float mspb = 60.0f / bpm * 1000.0f; // millisecond per beat
			float mul_divider = 192.0f / (float)numObjects;
			float totalGrid = (float)measure * 192.0f + (float)grid * mul_divider;
			float beats = (totalGrid - m_lastRenderGrid) / 48.0f;

			float result = mspb * beats;
			m_lastRenderGrid = totalGrid;
			m_lastRenderTime += result;

			return m_lastRenderTime;
		}

		bool OJN::EventComparison(const OJNEvent &e1, const OJNEvent &e2)
		{
			if(e1.Measure != e2.Measure)
				return e1.Measure < e2.Measure;
			if(e1.Grid != e2.Grid)
				return e1.Grid < e2.Grid;

			return false;
		}

		void OJN::LoadEvents(unsigned char difficulty)
		{
			if(difficulty > 2 || difficulty < 0)
				return;

			if(!m_events.empty())
				UnloadEvents();

			std::fstream fs(m_path.c_str(), std::ios::in | std::ios::binary);

			fs.seekg(m_header.DataOffset[difficulty], std::ios::beg);

			float currentBPM = m_header.Tempo;
			m_lastRenderBPM = currentBPM;
			while((unsigned)fs.tellg() < m_header.DataOffset[difficulty + 1])
			{
				OJNEventSet eventSet;
				fs.read((char *)&eventSet.Measure, 4);
				fs.read((char *)&eventSet.Channel, 2);
				fs.read((char *)&eventSet.NumEvents, 2);

				for(int i = 0; i < eventSet.NumEvents; i++)
				{
					OJNEvent e;

					if(eventSet.Channel >= 2)
					{
						fs.read((char *)&e.RefID, 2);
						if(e.RefID > 0)
						{
							float multiplier = 192.0f / (float)eventSet.NumEvents;
							e.Measure = eventSet.Measure;
							e.Grid = i * multiplier;
							e.Hit = false;

							e.TimePos = CalculateTime(eventSet.Measure, i, eventSet.NumEvents, currentBPM);
							fs.seekg(1, std::ios::cur);
							fs.read((char *)&e.NoteType, 1);

							m_events.push_back(e);
						}
						else fs.seekg(2, std::ios::cur);
					}
					else if(eventSet.Channel == 1)
					{
						float tbpm;
						fs.read((char *)&tbpm, 4);

						if(tbpm > 0.0f)
							currentBPM = tbpm;
					}
					else fs.seekg(4, std::ios::cur);
				}
			}

			fs.close();

			std::sort(m_events.begin(), m_events.end(), OJN::EventComparison);
		}

		void OJN::UnloadEvents()
		{
			m_events.clear();
		}

		void OJN::RenderToFile(const std::string &out_path, unsigned char difficulty)
		{
			if(m_events.empty())
				LoadEvents(difficulty);

			m_ojm.Open(m_header.OJMFile);
			m_ojm.LoadSamples();

			float top = m_events.back().TimePos + 2000.0f;
			float currentBPM = m_header.Tempo;

			LARGE_INTEGER freq;
			LARGE_INTEGER startTime;
			unsigned int elapsed = 0, previous = 0;

			QueryPerformanceFrequency(&freq);
			QueryPerformanceCounter(&startTime);

			/*float refreshRate = 1024.0f / 48000.0f;
			float totalCalls = (float)m_header.Duration[difficulty] / refreshRate;
			float totalTime = 0.0f;
			int ectr = 0;
			for(int i = 0; i < ((int)totalCalls) + 1; i++)
			{
				if(ectr == m_events.size())
					break;

				OJNEvent e = m_events[ectr];
				if(e.TimePos <= totalTime)
				{
					if(e.NoteType != 3)
					{
						OJMSample *sample = (e.NoteType == 0 || e.NoteType == 2) ? m_ojm.GetSample(e.RefID, BANKTYPE_W) : m_ojm.GetSample(e.RefID, BANKTYPE_M);
						if(sample)
						{
							FMOD_RESULT result = m_fmod->playSound(FMOD_CHANNEL_FREE, sample->Data, false, 0);
							//std::cout << e.TimePos << ":" << e.RefID << ":" << FMOD_ErrorString(result) << std::endl;
							std::cout << i << " / " << totalCalls << ": " << FMOD_ErrorString(result) << std::endl;
						}
					}

					ectr++;
				}

				m_fmod->update();
				totalTime += refreshRate * 1000.0f;
			}*/

			int i = 0;
			float refreshRate = 1000.0f / 120.0f; //1024.0f / 48000.0f;
			unsigned int currentMeasure = 0;
			unsigned int currentGrid = 0;
			float mspb = 60.0f / currentBPM * 1000.0f; // 1 beat = 48 grid
			float mspg = mspb / 48.0f;
			float msGridCtr = 0.0f;
			while(elapsed < top)
			{
				if(elapsed - previous >= refreshRate)
					previous = elapsed;

				if((float)elapsed - msGridCtr >= mspg)
				{
					msGridCtr = elapsed;
					currentGrid++;

					if(currentGrid % 48 == 0)
					{
						std::cout << "Measure: " << currentMeasure << std::endl << "Beat: " << (currentGrid / 48.0f) << std::endl;
					}

					if(currentGrid == 192)
					{
						currentMeasure++;
						currentGrid = 0;
					}
				}

				if(elapsed == previous)
				{
					if(i == m_events.size())
						break;

					OJNEvent e = m_events[i];
					if(e.TimePos <= elapsed)
					{
						if(e.NoteType != 3)
						{
							OJMSample *sample = (e.NoteType == 0 || e.NoteType == 2) ? m_ojm.GetSample(e.RefID, BANKTYPE_W) : m_ojm.GetSample(e.RefID, BANKTYPE_M);
							if(sample)
							{
								FMOD_RESULT result = m_fmod->playSound(FMOD_CHANNEL_FREE, sample->Data, false, 0);
								std::cout << e.TimePos << " : " << e.Measure << " : " << e.Grid << std::endl;
							}
						}

						i++;
					}

					m_fmod->update();
				}

				LARGE_INTEGER elapsed_li;
				QueryPerformanceCounter(&elapsed_li);
				elapsed = static_cast<unsigned int>((elapsed_li.QuadPart - startTime.QuadPart) * 1000 / freq.QuadPart);
			}
		}

		void OJN::RenderToFileAsync(OJN *instance, const std::string &out_path)
		{
			instance->RenderToFile(out_path);
		}
	}
}