/**
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "log.hpp"
#include "filesystem.hpp"
#include "input.hpp"
#include "runtime.hpp"
#include "hook_manager.hpp"
#include "version.h"
#include <Windows.h>

HMODULE g_module_handle = nullptr;


#define SPECIALK_IMPORT(ret) \
  __declspec (dllimport) ##ret __stdcall

enum class SK_RenderAPI
{
  Reserved  = 0x0001,

  // Native API Implementations
  OpenGL    = 0x0002,
  Vulkan    = 0x0004,
  D3D9      = 0x0008,
  D3D9Ex    = 0x0018,
  D3D10     = 0x0020, // Don't care
  D3D11     = 0x0040,
  D3D12     = 0x0080,

  // These aren't native, but we need the bitmask anyway
  D3D8      = 0x2000,
  DDraw     = 0x4000,
  Glide     = 0x8000,

  // Wrapped APIs (D3D12 Flavor)
  D3D11On12 = 0x00C0,

  // Wrapped APIs (D3D11 Flavor)
  D3D8On11  = 0x2040,
  DDrawOn11 = 0x4040,
  GlideOn11 = 0x8040,
};

SPECIALK_IMPORT (SK_RenderAPI)
SK_Render_GetAPIHookMask (void);



BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpvReserved)
{
	UNREFERENCED_PARAMETER(lpvReserved);

	using namespace reshade;

	switch (fdwReason)
	{
		case DLL_PROCESS_ATTACH:
		{
			g_module_handle = hModule;
			runtime::s_reshade_dll_path        = filesystem::get_module_path(hModule);
			runtime::s_target_executable_path  = filesystem::get_module_path(nullptr);
			runtime::s_profile_path            = filesystem::get_profile_path();
			const filesystem::path system_path = filesystem::get_special_folder_path(filesystem::special_folder::system);

			// Localize to Special K's logs/... directory
			const filesystem::path log_path    = runtime::s_profile_path / filesystem::path ("logs\\");

			log::open (log_path / runtime::s_reshade_dll_path.filename_without_extension () + ".log");

#ifdef WIN64
#define VERSION_PLATFORM "64-bit"
#else
#define VERSION_PLATFORM "32-bit"
#endif
			LOG(INFO) << "Initializing crosire's ReShade. Special K Custom version '" VERSION_STRING_FILE "' (" << VERSION_PLATFORM << ") built on '" VERSION_DATE " " VERSION_TIME "' loaded from " << runtime::s_reshade_dll_path << " to " << runtime::s_target_executable_path << " ...";

			auto hook_mask =
				static_cast <int> (SK_Render_GetAPIHookMask ());

			if (hook_mask & (int)SK_RenderAPI::D3D9)
				hooks::register_module(system_path / "d3d9.dll");

			if (hook_mask & (int)SK_RenderAPI::D3D10)
			{
				hooks::register_module(system_path / "d3d10.dll");
				hooks::register_module(system_path / "d3d10_1.dll");
			}

			if (hook_mask & (int)SK_RenderAPI::D3D11)
			{
				hooks::register_module(system_path / "d3d11.dll");
			}

			if ( (hook_mask & (int)SK_RenderAPI::D3D11) ||
			     (hook_mask & (int)SK_RenderAPI::D3D10) )
				hooks::register_module(system_path / "dxgi.dll");

			if (hook_mask & (int)SK_RenderAPI::OpenGL)
				hooks::register_module(system_path / "opengl32.dll");

			hooks::register_module(system_path / "user32.dll");

			LOG(INFO) << "Initialized.";
			break;
		}
		case DLL_PROCESS_DETACH:
		{
			LOG(INFO) << "Exiting ...";

			input::uninstall();
			//hooks::uninstall();

			LOG(INFO) << "Exited.";
			break;
		}
	}

	return TRUE;
}
