#pragma once

#include <mutex>

namespace r1delta::materialsystem_dx11
{
	class TextureLoadScratchBufferGate
	{
	public:
		[[nodiscard]] std::unique_lock<std::recursive_mutex> Acquire()
		{
			return std::unique_lock<std::recursive_mutex>(mutex_);
		}

	private:
		std::recursive_mutex mutex_;
	};
}
