#include "Music.hpp"

namespace Nx
{
	namespace O2Jam
	{
		Music::Music(const std::string &path, FMOD::System *fmod)
		{
			m_path = path;
			m_fmod = fmod;
			m_loadedDiff = -1;
			m_lastRenderGrid = 0.0f;
			m_lastRenderTime = 0.0f;
			m_asyncProgress = 0;

			m_isOK = ReadHeader();
		}

		Music::~Music()
		{
			UnloadEvents();
			UnloadSamples();
		}

		void Music::UnloadEvents()
		{
			if(!m_soundEvents.empty())
				m_soundEvents.clear();

			if(!m_tempoEvents.empty())
				m_tempoEvents.clear();

			m_loadedDiff = -1;
		}

		void Music::UnloadSamples()
		{
			if(!m_samples.empty())
			{
				for(std::vector<Sample>::iterator i = m_samples.begin(); i != m_samples.end(); ++i)
				{
					FMOD_RESULT result = i->Data->release();

					if(i->AsStream)
						delete[] i->BinData;
				}

				m_samples.clear();
			}
		}

		bool Music::ReadHeader()
		{
			std::fstream fs(m_path.c_str(), std::ios::in | std::ios::binary);

			if(!fs.is_open())
				return false;

			fs.read((char *)&m_header.NewSongID, 4);
			fs.read((char *)&m_header.FileSignature, 4);

			if(m_header.FileSignature != 7236207 || m_header.NewSongID < 0 || m_header.NewSongID > 10000)
			{
				fs.close();
				return false;
			}

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
			fs.read(m_header.Artist, 32);
			fs.read(m_header.Charter, 32);
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

		MusicHeader Music::GetHeader()
		{
			return m_header;
		}

		void Music::SetHeader(MusicHeader header)
		{
			m_header = header;
		}

		std::vector<SoundEvent> *Music::GetSoundEvents()
		{
			return &m_soundEvents;
		}

		std::vector<TempoEvent> *Music::GetTempoEvents()
		{
			return &m_tempoEvents;
		}

		std::vector<Sample> *Music::GetSamples()
		{
			return &m_samples;
		}

		void Music::LoadM30Package(std::fstream &fs, bool stream)
		{

			int version, encType, dataOffset;

			fs.seekg(4, std::ios::beg);

			fs.read((char *)&version, 4);
			fs.read((char *)&encType, 4);
			fs.seekg(4, std::ios::cur);
			fs.read((char *)&dataOffset, 4);

			fs.seekg(dataOffset, std::ios::beg);

			FMOD_CREATESOUNDEXINFO exinfo;
			while(!fs.eof())
			{
				Sample sample;
				short bankType;

				fs.read(sample.Name, 32);
				fs.read((char *)&sample.Filesize, 4);
				fs.read((char *)&bankType, 2);

				fs.seekg(6, std::ios::cur);
				fs.read((char *)&sample.RefID, 2);
				fs.seekg(6, std::ios::cur);

				sample.RefID += 1;
				if(bankType == 0)
					sample.RefID += 1000;

				if(encType == SampleEncoding::Nami)
				{
					sample.BinData = new unsigned char[sample.Filesize];
					fs.read((char *)sample.BinData, sample.Filesize);

					unsigned char nami[] = { 0x6E, 0x61, 0x6D, 0x69 };
					for(unsigned int i = 0; i + 3 < sample.Filesize; i += 4)
					{
						for(int j = 0; j < 4; j++)
							sample.BinData[i + j] ^= nami[j];
					}

					memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
					exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
					exinfo.length = sample.Filesize;

					sample.AsStream = stream;

					FMOD_MODE mode = FMOD_OPENMEMORY;
					if(stream)
						mode |= FMOD_CREATESTREAM;
					else mode |= FMOD_CREATESAMPLE;

					FMOD_RESULT result = m_fmod->createSound((const char *)sample.BinData, mode, &exinfo, &sample.Data);
					if(result == FMOD_OK)
					{
						if(!stream)
						{
							delete[] sample.BinData;
							sample.BinData = 0;
						}

						m_samples.push_back(sample);
					}
				}
				else fs.seekg(sample.Filesize, std::ios::cur);
			}
		}

		void Music::DecodeWave(unsigned char *in, unsigned char *out, int length)
		{
			// REARRANGE
			int key = ((length % 17) << 4) + (length % 17);
			int blockSize = length / 17;

			memcpy(in, out, length);

			int inOffset, outOffset;
			for(int block = 0; block < 17; block++)
			{
				inOffset = blockSize * block;
				outOffset = blockSize * RearrangeTable[key];

				memcpy(in + inOffset, out + outOffset, blockSize);

				key++;
			}

			// ACCXOR
			int tmp = 0;
			unsigned char currentByte = 0;

			for(int i = 0; i < length; i++)
			{
				tmp = out[i];
				currentByte = out[i];

				if(((m_accKeyByte << m_accCounter) & 0x80) != 0)
					currentByte = (unsigned char) ~currentByte;

				out[i] = currentByte;
				m_accCounter++;

				if(m_accCounter > 7)
				{
					m_accCounter = 0;
					m_accKeyByte = tmp;
				}
			}
		}

		void Music::LoadOMCPackage(std::fstream &fs, bool stream)
		{
			FMOD_MODE mode = FMOD_OPENMEMORY;
			if(stream) mode |= FMOD_CREATESTREAM;
			else mode |= FMOD_CREATESAMPLE;

			fs.seekg(4, std::ios::beg);

			m_accKeyByte = 0xFF;
			m_accCounter = 0;

			short nwav, nogg, total;
			int wavOffset, oggOffset, filesize;
			fs.read((char *)&nwav, 2);
			fs.read((char *)&nogg, 2);
			fs.read((char *)&wavOffset, 4);
			fs.read((char *)&oggOffset, 4);
			fs.read((char *)&filesize, 4);

			int percentage = 0;
			int currentSnd = 1;
			total = nwav + nogg;

			fs.seekg(wavOffset, std::ios::beg);

			unsigned char *buffer1, *buffer2;
			int refID = 1;
			FMOD_CREATESOUNDEXINFO exinfo;
			while(fs.tellg() < oggOffset)
			{
				Sample sample;
				WaveHeader wave;

				sample.Filesize = filesize;
				sample.AsStream = stream;

				int chunkSize;
				fs.read(sample.Name, 32);
				fs.read((char *)&wave.AudioFormat, 2);
				fs.read((char *)&wave.NumChannels, 2);
				fs.read((char *)&wave.SampleRate, 4);
				fs.read((char *)&wave.BitRate, 4);
				fs.read((char *)&wave.BlockAlign, 2);
				fs.read((char *)&wave.BitsPerSample, 2);
				fs.seekg(4, std::ios::cur);
				fs.read((char *)&chunkSize, 4);

				if(chunkSize == 0)
				{
					refID++;
					continue;
				}

				buffer1 = new unsigned char[chunkSize];
				buffer2 = new unsigned char[chunkSize];
				fs.read((char *)buffer1, chunkSize);

				DecodeWave(buffer1, buffer2, chunkSize);

				delete[] buffer1;

				int pcm = 16, nsize = chunkSize + 16;

				std::stringstream waveStream;
				waveStream.write("RIFF", 4);
				waveStream.write((char *)&nsize, 4);
				waveStream.write("WAVE", 4);
				waveStream.write("fmt ", 4);
				waveStream.write((char *)&pcm, 4);
				waveStream.write((char *)&wave.AudioFormat, 2);
				waveStream.write((char *)&wave.NumChannels, 2);
				waveStream.write((char *)&wave.SampleRate, 4);
				waveStream.write((char *)&wave.BitRate, 4);
				waveStream.write((char *)&wave.BlockAlign, 2);
				waveStream.write((char *)&wave.BitsPerSample, 2);
				waveStream.write("data", 4);
				waveStream.write((char *)&chunkSize, 4);
				waveStream.write((char *)buffer2, chunkSize);

				delete[] buffer2;

				std::string data = waveStream.str();
				sample.BinData = new unsigned char[data.length()];

				memcpy(sample.BinData, data.data(), data.length());
			
				memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
				exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
				exinfo.length = sample.Filesize;

				FMOD_RESULT result = m_fmod->createSound((const char *)sample.BinData, mode, &exinfo, &sample.Data);
				if(result == FMOD_OK)
				{
					if(!stream)
					{
						delete[] sample.BinData;
						sample.BinData = 0;
					}

					sample.RefID = refID;
					m_samples.push_back(sample);
				}

				refID++;

				m_asyncProgress = currentSnd * 100 / total;
				currentSnd++;
			}

			refID = 1001;
			while(!fs.eof())
			{
				Sample sample;
				sample.AsStream = stream;

				fs.read(sample.Name, 32);
				fs.read((char *)&sample.Filesize, 4);

				if(sample.Filesize == 0)
				{
					refID++;
					continue;
				}

				sample.BinData = new unsigned char[sample.Filesize];
				fs.read((char *)sample.BinData, sample.Filesize);

				memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));
				exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
				exinfo.length = sample.Filesize;

				FMOD_RESULT result = m_fmod->createSound((const char *)sample.BinData, mode, &exinfo, &sample.Data);
				if(result == FMOD_OK)
				{
					if(!stream)
					{
						delete[] sample.BinData;
						sample.BinData = 0;
					}

					sample.RefID = refID;
					m_samples.push_back(sample);
				}

				refID++;
			}
		}

		bool Music::LoadSamplesAsync(Music *instance, bool stream)
		{
			return instance->LoadSamples(stream);
		}

		bool Music::LoadSamples(bool stream)
		{
			std::fstream fs(m_header.OJMFile, std::ios::in | std::ios::binary);

			if(!fs.is_open())
				return false;

			char buffer[32];

			fs.read(buffer, 4);

			if(strcmp(buffer, "M30") == 0)
				LoadM30Package(fs, stream);
			else if(strcmp(buffer, "OMC") == 0 || strcmp(buffer, "OJM") == 0)
				LoadOMCPackage(fs, stream);
			else
			{
				fs.close();
				return false;
			}

			fs.close();
			return true;
		}

		float Music::CalculateTime(unsigned int measure, unsigned int grid, unsigned int numObjects, float bpm)
		{
			float mspb = 60.0f / m_currentRenderTempo * 1000.0f;
			float totalGrid = (float)measure * 192.0f + (float)grid;
			float beats = (totalGrid - m_lastRenderGrid) / 48.0f;

			float result = mspb * beats;
			m_lastRenderGrid = totalGrid;
			m_lastRenderTime += result;

			return m_lastRenderTime;
		}

		bool Music::LoadEventsAsync(Music *instance, unsigned char difficulty)
		{
			return instance->LoadEvents(difficulty);
		}

		bool Music::LoadEvents(unsigned char difficulty)
		{
			if(difficulty > 2 || difficulty < 0)
				return false;

			if(!m_soundEvents.empty() || !m_tempoEvents.empty())
				UnloadEvents();

			int position, percentage = 0, length = m_header.DataOffset[difficulty + 1];
			std::fstream fs(m_path.c_str(), std::ios::in | std::ios::binary);

			fs.seekg(m_header.DataOffset[difficulty], std::ios::beg);

			printf("0%%");

			m_currentRenderTempo = m_header.Tempo;
			position = fs.tellg();
			while(position < length)
			{
				EventSet eventSet;
				fs.read((char *)&eventSet.Measure, 4);
				fs.read((char *)&eventSet.Channel, 2);
				fs.read((char *)&eventSet.NumEvents, 2);

				for(int i = 0; i < eventSet.NumEvents; i++)
				{
					unsigned int grid = i * (unsigned int)(192.0f / (float)eventSet.NumEvents);
					if(eventSet.Channel >= 2)
					{
						SoundEvent e;
						e.IsApplied = false;

						fs.read((char *)&e.RefID, 2);
						if(e.RefID > 0)
						{
							e.Measure = eventSet.Measure;
							e.Grid = grid;
							e.Time = CalculateTime(e.Measure, e.Grid, eventSet.NumEvents, m_currentRenderTempo);

							fs.seekg(1, std::ios::cur);
							fs.read((char *)&e.NoteType, 1);

							m_soundEvents.push_back(e);
						}
						else fs.seekg(2, std::ios::cur);
					}
					else if(eventSet.Channel == 1)
					{
						TempoEvent e;
						fs.read((char *)&e.Value, 4);

						if(e.Value > 0)
						{
							e.Measure = eventSet.Measure;
							e.Grid = grid;
							e.Time = CalculateTime(e.Measure, e.Grid, eventSet.NumEvents, m_currentRenderTempo);
							e.IsApplied = false;

							m_currentRenderTempo = e.Value;

							m_tempoEvents.push_back(e);
						}
					}
					else fs.seekg(4, std::ios::cur);
				}

				if(percentage < 10)
					printf("\b\b");
				else if(percentage < 100)
					printf("\b\b\b");
				else printf("\b\b\b\b");

				percentage = (int)((float)position / (float)length * 100.f);
				printf("%d%%", percentage);

				position = fs.tellg();
			}

			fs.close();


			std::sort(m_soundEvents.begin(), m_soundEvents.end(), Music::SortEventCallback);

			if(percentage < 10)
				printf("\b\b");
			else if(percentage < 100)
				printf("\b\b\b");
			else printf("\b\b\b\b");

			percentage = 100;
			printf("%d%%", percentage);

			m_loadedDiff = difficulty;
			return true;
		}

		int Music::GetAsyncProgress()
		{
			return m_asyncProgress;
		}

		int Music::GetLoadedDifficulty()
		{
			return m_loadedDiff;
		}
		
		bool Music::SortEventCallback(const Event &e1, const Event &e2)
		{
			return e1.Time < e2.Time;
		}
	}
}