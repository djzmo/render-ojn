#ifndef NX_IO_FILE_HPP
#define NX_IO_FILE_HPP

#include <string>
#include <fstream>

namespace Nx
{
	namespace IO
	{
		class File
		{
		public:
			static bool Exists(const std::string &path);
			static void Move(const std::string &sourceFileName, const std::string &destFileName);
			static void Delete(const std::string &path);
		};
	}
}

#endif