#include "OJM.hpp"

namespace Nx
{
	namespace O2Jam
	{
		OJM::OJM(const std::string &path, FMOD::System *fmod)
		{
			m_path = path;
			m_fmod = fmod;
		}

		OJM::OJM(FMOD::System *fmod)
		{
			m_fmod = fmod;
		}

		OJM::~OJM()
		{
			if(!m_wSamples.empty())
			{
				for(std::vector<OJMSample>::iterator i = m_wSamples.begin(); i != m_wSamples.end(); ++i)
				{
					i->Data->release();
					delete[] i->BinData;
				}

				m_wSamples.clear();
			}

			if(!m_mSamples.empty())
			{
				for(std::vector<OJMSample>::iterator i = m_mSamples.begin(); i != m_mSamples.end(); ++i)
				{
					i->Data->release();
					delete[] i->BinData;
				}

				m_mSamples.clear();
			}
		}

		std::vector<OJMSample> *OJM::GetSamples(unsigned char type)
		{
			if(type == BANKTYPE_W)
				return &m_wSamples;
			else return &m_mSamples;
		}

		void OJM::Open(const std::string &path)
		{
			m_path = path;
		}

		bool OJM::LoadSamples()
		{
			std::fstream fs(m_path.c_str(), std::ios::in | std::ios::binary);

			if(!fs.is_open())
				return false;

			char buffer[32];

			fs.read(buffer, 4);

			if(strcmp(buffer, "M30") == 0)
			{
				m_header.Type = OJMTYPE_M30;

				int version, dataOffset;

				fs.read((char *)&version, 4);
				fs.read((char *)&m_header.EncType, 4);
				fs.seekg(4, std::ios::cur);
				fs.read((char *)&dataOffset, 4);

				fs.seekg(dataOffset, std::ios::beg);

				FMOD_CREATESOUNDEXINFO exinfo;
				while(!fs.eof())
				{
					OJMSample sample;
					short bankType;

					fs.read(sample.Name, 32);
					fs.read((char *)&sample.Filesize, 4);
					fs.read((char *)&bankType, 2);

					fs.seekg(6, std::ios::cur);
					fs.read((char *)&sample.RefID, 2);
					fs.seekg(6, std::ios::cur);

					sample.RefID += 1;
					if(bankType == 0)
						sample.BankType = BANKTYPE_M;
					else sample.BankType = BANKTYPE_W;

					if(m_header.EncType == SAMPLEENC_NAMI)
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

						FMOD_RESULT result = m_fmod->createSound((const char *)sample.BinData, FMOD_OPENMEMORY | FMOD_CREATESTREAM, &exinfo, &sample.Data);
						if(result == FMOD_OK)
						{
							if(sample.BankType == BANKTYPE_M)
								m_mSamples.push_back(sample);
							else m_wSamples.push_back(sample);
						}
					}
					else fs.seekg(sample.Filesize, std::ios::cur);
				}
			}
			else if(strcmp(buffer, "OMC") == 0)
			{
				m_header.Type = OJMTYPE_OMC;
			}
			else return false;

			fs.close();

			return true;
		}

		OJMSample *OJM::GetSample(unsigned int ref, unsigned char type)
		{
			if(type == BANKTYPE_W)
			{
				for(std::vector<OJMSample>::iterator i = m_wSamples.begin(); i != m_wSamples.end(); ++i)
				{
					if(i->RefID == ref)
						return &(*i);
				}
			}
			else
			{
				for(std::vector<OJMSample>::iterator i = m_mSamples.begin(); i != m_mSamples.end(); ++i)
				{
					if(i->RefID == ref)
						return &(*i);
				}
			}

			return 0;
		}

		void OJM::UnloadSamples()
		{
		}
	}
}