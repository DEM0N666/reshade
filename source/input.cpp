/**
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "hook.hpp"
#include "hook_manager.hpp"
#include "log.hpp"
#include "input.hpp"
#include <Windows.h>
#include <assert.h>
#include <mutex>
#include <unordered_map>

#include "imgui.h"

namespace reshade
{
	static std::mutex s_mutex;
	static std::unordered_map<HWND, std::weak_ptr<input>> s_windows;

	input::input(window_handle window) : _window(window)
	{
		assert(window != nullptr);
	}

	std::shared_ptr<input> input::register_window(window_handle window)
	{
		const HWND parent = GetParent(static_cast<HWND>(window));

		if (parent != nullptr)
		{
			window = parent;
		}

		const std::lock_guard<std::mutex> lock(s_mutex);

		const auto insert = s_windows.emplace(static_cast<HWND>(window), std::weak_ptr<input>());

		if (insert.second || insert.first->second.expired())
		{
			LOG(INFO) << "Starting input capture for window " << window << " ...";

			const auto instance = std::make_shared<input>(window);

			insert.first->second = instance;

			return instance;
		}
		else
		{
			return insert.first->second.lock();
		}
	}
	void input::uninstall()
	{
		s_windows.clear();
	}

	bool input::handle_window_message(const void *message_data)
	{
    return false;
	}

	bool input::is_key_down(unsigned int keycode) const
	{
		assert(keycode < 256);

		return (ImGui::GetIO ().KeysDown [keycode]);
	}
	bool input::is_key_down(unsigned int keycode, bool ctrl, bool shift, bool alt) const
	{
		return is_key_down(keycode) && (!ctrl || is_key_down(VK_CONTROL)) && (!shift || is_key_down(VK_SHIFT)) && (!alt || is_key_down(VK_MENU));
	}
	bool input::is_key_pressed(unsigned int keycode) const
	{
		assert(keycode < 256);

		return (ImGui::GetIO ().KeysDownDuration [keycode] == 0.0f);
	}
	bool input::is_key_pressed(unsigned int keycode, bool ctrl, bool shift, bool alt) const
	{
		return is_key_pressed(keycode) && (!ctrl || is_key_down(VK_CONTROL)) && (!shift || is_key_down(VK_SHIFT)) && (!alt || is_key_down(VK_MENU));
	}
	bool input::is_key_released(unsigned int keycode) const
	{
		assert(keycode < 256);

		return (ImGui::GetIO ().KeysDownDurationPrev [keycode] > 0.0f && (! ImGui::GetIO ().KeysDown [keycode]));
	}
	bool input::is_any_key_down() const
	{
		for (unsigned int i = 0; i < 256; i++)
		{
			if (is_key_down(i))
			{
				return true;
			}
		}

		return false;
	}
	bool input::is_any_key_pressed() const
	{
		return last_key_pressed() != 0;
	}
	bool input::is_any_key_released() const
	{
		return last_key_released() != 0;
	}
	unsigned int input::last_key_pressed() const
	{
		for (unsigned int i = 0; i < 256; i++)
		{
			if (is_key_pressed(i))
			{
				return i;
			}
		}

		return 0;
	}
	unsigned int input::last_key_released() const
	{
		for (unsigned int i = 0; i < 256; i++)
		{
			if (is_key_released(i))
			{
				return i;
			}
		}

		return 0;
	}
	bool input::is_mouse_button_down(unsigned int button) const
	{
		assert(button < 5);

		return (ImGui::GetIO ().MouseDown [button]);
	}
	bool input::is_mouse_button_pressed(unsigned int button) const
	{
		assert(button < 5);

		return (ImGui::GetIO ().MouseClicked [button]);
	}
	bool input::is_mouse_button_released(unsigned int button) const
	{
		assert(button < 5);

		return (ImGui::GetIO ().MouseReleased [button]);
	}
	bool input::is_any_mouse_button_down() const
	{
		for (unsigned int i = 0; i < 5; i++)
		{
			if (is_mouse_button_down(i))
			{
				return true;
			}
		}

		return false;
	}
	bool input::is_any_mouse_button_pressed() const
	{
		for (unsigned int i = 0; i < 5; i++)
		{
			if (is_mouse_button_pressed(i))
			{
				return true;
			}
		}

		return false;
	}
	bool input::is_any_mouse_button_released() const
	{
		for (unsigned int i = 0; i < 5; i++)
		{
			if (is_mouse_button_released(i))
			{
				return true;
			}
		}

		return false;
	}

	unsigned short input::key_to_text(unsigned int keycode) const
	{
		WORD ch = 0;
		return ToAscii(keycode, MapVirtualKey(keycode, MAPVK_VK_TO_VSC), _keys, &ch, 0) ? ch : 0;
	}

	void input::block_mouse_input(bool enable)
	{
		////ImGui::GetIO ().WantCaptureMouse = enable;

		if (enable)
		{
			//ClipCursor(nullptr);
		}
	}
	void input::block_keyboard_input(bool enable)
	{
    ////ImGui::GetIO ().WantCaptureKeyboard = enable;
	}

	void input::next_frame()
	{
		_mouse_wheel_delta = 0;

		//// Update print screen state
		//if ((_keys[VK_SNAPSHOT] & 0x80) == 0 &&
		//	  (static const auto trampoline = reshade::hooks::call(&reshade::HookGetKeyState)GetAsyncKeyState(VK_SNAPSHOT) & 0x8000) != 0)
		//{
		//	_keys[VK_SNAPSHOT] = 0x88;
		//}
	}
}
