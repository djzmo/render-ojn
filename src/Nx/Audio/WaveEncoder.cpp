#include "WaveEncoder.hpp"

namespace Nx
{
	namespace Audio
	{
		WaveEncoder::WaveEncoder(const std::string &sourceFileName, int quality)
		{
			m_source = sourceFileName;
			m_quality = quality;
		}

		WaveEncoder::~WaveEncoder()
		{
		}

		bool WaveEncoder::ToMP3(const std::string &destFileName)
		{
			FILE *pcm = fopen(m_source.c_str(), "rb");
			FILE *mp3 = fopen(destFileName.c_str(), "wb");

			fseek(pcm, 0, SEEK_END);
			float pcm_length = ftell(pcm);
			float pcm_read = 0;
			int percentage = 0;
			fseek(pcm, 0, SEEK_SET);

			if(pcm == 0 || mp3 == 0)
			{
				m_lastErrorMessage = "Failed to open output stream.";
				return false;
			}

			int read, write;

			const int PCM_SIZE = 8192;
			const int MP3_SIZE = 8192;

			short int pcm_buffer[PCM_SIZE*2];
			unsigned char mp3_buffer[MP3_SIZE];

			lame_t lame = lame_init();
			lame_set_num_channels(lame, 2);

			if(m_quality == 3)
			{
				lame_set_in_samplerate(lame, 48000);
				lame_set_brate(lame, 320);
				lame_set_quality(lame, 2);
			}
			else if(m_quality == 2)
			{
				lame_set_in_samplerate(lame, 48000);
				lame_set_brate(lame, 192);
				lame_set_quality(lame, 5);
			}
			else if(m_quality == 1)
			{
				lame_set_in_samplerate(lame, 48000);
				lame_set_brate(lame, 128);
				lame_set_quality(lame, 7);
			}

			lame_set_mode(lame, JOINT_STEREO);
			lame_init_params(lame);

			printf("0%%");

			do
			{
				read = fread(pcm_buffer, 2*sizeof(short int), PCM_SIZE, pcm);
				if (read == 0)
					write = lame_encode_flush(lame, mp3_buffer, MP3_SIZE);
				else
					write = lame_encode_buffer_interleaved(lame, pcm_buffer, read, mp3_buffer, MP3_SIZE);
				fwrite(mp3_buffer, write, 1, mp3);

				pcm_read += PCM_SIZE * 2 * sizeof(short int);

				if(percentage < 10)
					printf("\b\b");
				else if(percentage < 100)
					printf("\b\b\b");
				else printf("\b\b\b\b");

				percentage = (int)(pcm_read / pcm_length * 100.f);
				printf("%d%%", percentage);
			} while (read != 0);

			lame_close(lame);
			fclose(mp3);
			fclose(pcm);

			return true;
		}

		bool WaveEncoder::ToOGG(const std::string &destFileName)
		{
			SNDFILE *srcFile;
			SNDFILE *destFile;

			SF_INFO srcInfo;
			SF_INFO destInfo;
			memset(&srcInfo, 0, sizeof(srcInfo));
			memset(&destInfo, 0, sizeof(destInfo));
			srcInfo.format = 0;

			if((srcFile = sf_open(m_source.c_str(), SFM_READ, &srcInfo)) == 0)
			{
				m_lastErrorMessage = "Failed to open input stream.";
				return false;
			}

			destInfo.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
			destInfo.channels = srcInfo.channels;
			destInfo.samplerate = srcInfo.samplerate;

			if((destFile = sf_open(destFileName.c_str(), SFM_WRITE, &destInfo)) == 0)
			{
				m_lastErrorMessage = "Failed to open output stream.";
				return false;
			}

			double quality;
			
			if(m_quality == 3)
				quality = 1.0;
			else if(m_quality == 2)
				quality = 0.8;
			else if(m_quality == 1)
				quality = 0.5;

			sf_command(destFile, SFC_SET_VBR_ENCODING_QUALITY, &quality, sizeof(double));

			const int BUFFER_LENGTH = 8192;
			int buffer[BUFFER_LENGTH];
			int read, totalRead = 0, srcLength = sf_seek(srcFile, 0, SEEK_END), percentage = 0;
			
			sf_seek(srcFile, 0, SEEK_SET);
			printf("0%%");

			while((read = sf_read_int(srcFile, buffer, BUFFER_LENGTH)) > 0)
			{
				sf_write_int(destFile, buffer, read);
				totalRead += read;

				if(percentage < 10)
					printf("\b\b");
				else if(percentage < 100)
					printf("\b\b\b");
				else printf("\b\b\b\b");

				percentage = (int)((float)totalRead / (float)srcLength * 50.f);
				printf("%d%%", percentage);
			}

			sf_close(srcFile);
			sf_close(destFile);

			return true;
		}
	}
}