#include <iostream>
#include <algorithm>
#include <string>
#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <tag.h>
#include <xiphcomment.h>
#include <fileref.h>

#include "Nx/IO.hpp"
#include "Nx/Audio/WaveEncoder.hpp"
#include "Nx/O2Jam/Music.hpp"
#include "Nx/O2Jam/MusicRenderer.hpp"
//#include "Nx/O2Jam/OJN.hpp"

using namespace Nx::IO;
using namespace Nx::O2Jam;
using namespace Nx::Audio;
namespace po = boost::program_options;

void ERRCHECK(FMOD_RESULT result)
{
	if(result != FMOD_OK)
	{
		std::cout << FMOD_ErrorString(result) << std::endl;
		exit(-1);
	}
}

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";


static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_decode(std::string const& encoded_string)
{
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_]; in_++;
    if (i ==4) {
      for (i = 0; i <4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret += char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j <4; j++)
      char_array_4[j] = 0;

    for (j = 0; j <4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
  }

  return ret;
}

FMOD::System *InitFMOD(bool wavwriter = false, bool nrt = true)
{
	FMOD::System    *system; 
	FMOD_RESULT      result; 
	unsigned int     version; 
	int              numdrivers; 
	FMOD_SPEAKERMODE speakermode; 
	FMOD_CAPS        caps; 
	char             name[256]; 
	 
	/* 
		Create a System object and initialize. 
	*/ 
	result = FMOD::System_Create(&system); 
	ERRCHECK(result); 
	 
	result = system->getVersion(&version); 
	ERRCHECK(result); 
	 
	if (version < FMOD_VERSION) 
	{ 
		printf("Error!  You are using an old version of FMOD %08x.  This program requires %08x\n", version, FMOD_VERSION); 
		return 0; 
	} 
	 
	result = system->getNumDrivers(&numdrivers); 
	ERRCHECK(result); 
	 
	if (numdrivers == 0) 
	{ 
		result = system->setOutput(FMOD_OUTPUTTYPE_NOSOUND); 
		ERRCHECK(result); 
	} 
	else 
	{ 
		result = system->getDriverCaps(0, &caps, 0, &speakermode); 
		ERRCHECK(result); 
	 
		/* 
			Set the user selected speaker mode. 
		*/ 
		result = system->setSpeakerMode(speakermode); 
		ERRCHECK(result); 
	 
		if (caps & FMOD_CAPS_HARDWARE_EMULATED) 
		{ 
			/* 
				The user has the 'Acceleration' slider set to off!  This is really bad  
				for latency! You might want to warn the user about this. 
			*/ 
			result = system->setDSPBufferSize(1024, 10); 
			ERRCHECK(result); 
		} 
	 
		result = system->getDriverInfo(0, name, 256, 0); 
		ERRCHECK(result); 
	 
		if (strstr(name, "SigmaTel")) 
		{ 
			/* 
				Sigmatel sound devices crackle for some reason if the format is PCM 16bit
				PCM floating point output seems to solve it. 
			*/ 
			result = system->setSoftwareFormat(48000, FMOD_SOUND_FORMAT_PCMFLOAT, 0,0, FMOD_DSP_RESAMPLER_LINEAR); 
			ERRCHECK(result); 
		} 
	} 
	
	if(wavwriter)
	{
		if(nrt)
			system->setOutput(FMOD_OUTPUTTYPE_WAVWRITER_NRT);
		else system->setOutput(FMOD_OUTPUTTYPE_WAVWRITER);
		result = system->init(100, FMOD_INIT_NORMAL, (void *)"_render.tmp"); 
	}
	else result = system->init(100, FMOD_INIT_NORMAL, 0); 

	if (result == FMOD_ERR_OUTPUT_CREATEBUFFER) 
	{ 
		/* 
			Ok, the speaker mode selected isn't supported by this soundcard.  Switch it  
			back to stereo... 
		*/ 
		result = system->setSpeakerMode(FMOD_SPEAKERMODE_STEREO); 
		ERRCHECK(result); 
	 
		/* 
			... and re-init. 
		*/ 
		result = system->init(100, FMOD_INIT_NORMAL, 0); 
	} 

	ERRCHECK(result); 
	return system;
}

void RenderOJN(const std::string &inFile, const std::string &outFile, bool play, int idiff, const std::string &format, const std::string &rendermode)
{
}

int main(int argc, char **argv)
{
	std::cout << "\n================================\n";
	std::cout << "RenderOJN\n";
	std::cout << "version 0.8 beta\n";
	std::cout << "http://djzmo.com/p/RenderOJN\n";
	std::cout << "(C) 2012 - DJZMO\n";
	std::cout << "================================\n\n";

	if(File::Exists("_render.tmp"))
		File::Delete("_render.tmp");

	std::string tagComment = "R2VuZXJhdGVkIGJ5IFJlbmRlck9KTiB2MC44IGJldGENCmh0dHA6Ly9kanptby5jb20vcC9SZW5kZXJPSk4=";
	std::string genre[] = { "Ballad", "Rock", "Dance", "Techno", "Hip-hop", "Soul/R&B", "Jazz", "Funk", "Classical", "Traditional", "Etc" };
	FMOD::System *system = 0;
	if(argc >= 2 && strcmp(argv[1], "--help") != 0)
	{
		std::string inFile = argv[1];
		if(!File::Exists(inFile))
		{
			std::cout << "Failed to open input file.\n";
			return 0;
		}

		std::cout << "Initializing..\n";

		po::options_description desc("options");
		desc.add_options()
			("play", "")
			("difficulty", po::value<char>(), "")
			("format", po::value<std::string>(), "")
			("outfile", po::value<std::string>(), "")
			("rendermode", po::value<std::string>(), "")
			("quality", po::value<int>(), "");

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		bool playMode = false;
		std::string outPath = std::string(argv[1]), format = "mp3", mode = "quick";
		char diff = 'h';
		int quality = 3;

		if(vm.count("play"))
		{
			system = InitFMOD();
			playMode = true;
		}
		else
		{
			if(vm.count("outfile"))
				outPath = vm["outfile"].as<std::string>();

			if(vm.count("difficulty"))
				diff = vm["difficulty"].as<char>();

			if(vm.count("format"))
				format = vm["format"].as<std::string>();

			if(vm.count("rendermode"))
				mode = vm["rendermode"].as<std::string>();


			if(diff != 'h' && diff != 'n' && diff != 'e')
				diff = 'h';

			if(format != "wav" && format != "mp3" && format != "ogg")
				format = "mp3";

			if(mode != "quick" && mode != "realtime")
				mode = "quick";

			if(quality > 1 || quality > 3)
				quality = 3;

			if(mode == "quick")
				system = InitFMOD(true);
			else system = InitFMOD(true, false);
		}

		if(system)
		{
			Music music(inFile, system);

			if(!music.IsOK())
			{
				std::cout << "Input file is not a correctly-formatted OJN.\n";
				return 0;
			}

			outPath = outPath + "." + format;

			MusicHeader header = music.GetHeader();

			int idiff = 2;
			if(diff == 'n') idiff = 1;
			else if(diff == 'e') idiff = 0;

			std::cout << "Options:\n";
			std::cout <<     "\n  Input File:              " << inFile;
			if(!playMode)
			{
				std::cout <<     "\n  Output File:             " << outPath;
				std::cout <<     "\n  Output Format:           " << format;
				std::cout <<     "\n  Output Quality:          " << ((quality == 3) ? "Best" : (quality == 2) ? "Standard" : "Poor");
				std::cout <<     "\n  Rendering Mode:          " << mode;
			}
			else std::cout <<     "\n  Rendering Mode:          realtime (playback)";

			if(mode == "realtime" || playMode)
			{
				boost::posix_time::time_duration td = boost::posix_time::seconds(header.Duration[idiff]);
				std::string duration = boost::posix_time::to_simple_string(td);

				std::cout << "\n  Approx. Rendering Time:  " << duration;
				std::cout << "\n\n  In realtime mode, it is highly recommended to do nothing with your computer";
				std::cout << "\n  while rendering a keysounded music to preserve output quality.\n\n";
			}
			else
				std::cout << "\n\n  In quick mode, output quality might not be satisfying on keysounded music.\n\n";

			std::cout << "Loading samples.. ";
			music.LoadSamples(playMode);
			std::cout << "\nLoading events.. ";
			music.LoadEvents(idiff);
			printf("\n");

			std::vector<SoundEvent> *soundEvents = music.GetSoundEvents();
			std::vector<TempoEvent> *tempoEvents = music.GetTempoEvents();
			std::vector<Sample> *samples = music.GetSamples();

			if(soundEvents->empty())
			{
				std::cout << "Failed to load events.\n";
				return 0;
			}

			if(samples->empty())
			{
				std::cout << "Failed to load samples.\n";
				return 0;
			}

			MusicRenderer renderer(system);
			renderer.SetEvents(soundEvents, tempoEvents, music.GetLoadedDifficulty());
			renderer.SetSamples(samples);
			renderer.SetMusicHeader(header);

			std::cout << "Rendering... ";
			if(playMode)
			{
				renderer.RenderToSpeaker();

				std::cout << "\nCleaning up stuffs..\n";

				music.UnloadSamples();
				music.UnloadEvents();

				system->close();
				system->release();
			}
			else
			{
				if(mode == "quick")
					renderer.RenderToFile();
				else renderer.RenderToSpeaker();
				//boost::thread t1(MusicRenderer::RenderToFileAsync, &renderer);

				std::cout << "\nEncoding... ";

				music.UnloadSamples();
				music.UnloadEvents();

				system->close();
				system->release();

				if(format == "wav")
					File::Move("_render.tmp", outPath);
				else
				{
					WaveEncoder encoder("_render.tmp", quality);

					if(format == "mp3")
					{
						if(encoder.ToMP3(outPath))
						{
							std::cout << "\nTagging...\n";
							TagLib::FileRef tag(outPath.c_str());
							tag.tag()->setTitle(header.Title);
							tag.tag()->setArtist(header.Artist);
							tag.tag()->setTrack(header.NewSongID);
							tag.tag()->setGenre(genre[header.NewGenreCode]);
							tag.tag()->setComment(base64_decode(tagComment));
							tag.save();
						}
						else std::cout << encoder.GetLastErrorMessage();
					}
					else if(format == "ogg")
					{
						if(encoder.ToOGG(outPath))
						{
							std::cout << "\nTagging...\n";
							TagLib::FileRef tag(outPath.c_str());
							TagLib::Ogg::XiphComment *oggtag = static_cast<TagLib::Ogg::XiphComment *>(tag.tag());
							oggtag->setTitle(header.Title);
							oggtag->setArtist(header.Artist);
							oggtag->setTrack(header.NewSongID);
							oggtag->setGenre(genre[header.NewGenreCode]);
							oggtag->setComment(base64_decode(tagComment));
							oggtag->removeField("ENCODER");
							tag.save();
						}
						else std::cout << encoder.GetLastErrorMessage();
					}

					File::Delete("_render.tmp");
				}

				std::cout << "\n";
			}

			std::cout << "Done! :)\n";
		}
		else std::cout << "Error initializing FMOD." << std::endl;
	}
	else
	{
		std::cout << "Usage: RenderOJN [inputfile [options]]\n\n";
		std::cout << "Rendering Options:\n\n";
		std::cout << "  --rendermode <mode>       Rendering Mode (quick, realtime). Default: quick\n";
		std::cout << "  --format <format>         Output Format (wav, mp3, ogg). Default: mp3\n";
		std::cout << "  --outfile <filename>      Output Filename. Default: <inputfile>.<format>\n";
		std::cout << "                            Whitespace is not allowed.\n";
		std::cout << "  --quality <quality>       Output Quality (for mp3 and ogg). Default: 3\n";
		std::cout << "                            3 - Best, 2 - Standard, 1 - Poor\n\n";
		std::cout << "Misc. Options:\n\n";
		std::cout << "  --difficulty <difficulty> Note Difficulty (e, n, h). Default: h\n";
		std::cout << "  --play                    Play the music instead of generating an output file\n";
		std::cout << "  --help                    Display this text\n\n";
		std::cout << "Example: RenderOJN o2ma100.ojn --outfile BachAlive.mp3 --quality 2\n";
		std::cout << "         RenderOJN o2ma100.ojn --rendermode realtime --format wav\n";
		std::cout << "         RenderOJN o2ma100.ojn --play\n";
		std::cout << "         RenderOJN --help\n";

		if(argc >= 2 && strcmp(argv[1], "--help") == 0)
		{
			std::cout << std::endl;
			::system("pause");
		}
	}

	return 0;
}