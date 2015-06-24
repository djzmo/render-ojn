#include "File.hpp"

namespace Nx
{
	namespace IO
	{
		bool File::Exists(const std::string &path)
		{
			std::ifstream fs(path.c_str());
			if(fs.is_open())
			{
				fs.close();
				return true;
			}
			else return false;
		}

		void File::Move(const std::string &sourceFileName, const std::string &destFileName)
		{
			rename(sourceFileName.c_str(), destFileName.c_str());
		}

		void File::Delete(const std::string &path)
		{
			unlink(path.c_str());
		}
	}
}