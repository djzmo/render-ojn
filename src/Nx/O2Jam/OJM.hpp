#ifndef NX_O2JAM_OJM_HPP
#define NX_O2JAM_OJM_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <fmod.hpp>
#include <fmod_errors.h>

namespace Nx
{
	namespace O2Jam
	{
		enum OJMType
		{
			OJMTYPE_OMC = 0,
			OJMTYPE_M30
		};

		enum OJMEncType
		{
			SAMPLEENC_NAMI = 16
		};

		enum OJMSampleBankType
		{
			BANKTYPE_W = 0,
			BANKTYPE_M
		};

		struct OJMSample
		{
			unsigned short RefID;
			unsigned int Filesize;
			unsigned char BankType;
			char Name[32];
			FMOD::Sound *Data;
			unsigned char *BinData;
		};

		struct OJMHeader
		{
			unsigned int Filesize;
			unsigned char Type;
			unsigned int EncType;
		};

		class OJM
		{
			std::string m_path;
			std::vector<OJMSample> m_wSamples;
			std::vector<OJMSample> m_mSamples;
			FMOD::System *m_fmod;
			OJMHeader m_header;

		public:
			OJM(FMOD::System *fmod);
			OJM(const std::string &path, FMOD::System *fmod);
			~OJM();

			std::vector<OJMSample> *GetSamples(unsigned char type);
			OJMSample *GetSample(unsigned int ref, unsigned char type);

			bool LoadSamples();
			void UnloadSamples();

			void Open(const std::string &path);
		};
	}
}

#endif