# encoding: UTF-8
# 01_win32_wrap_pad.rb
# 원본: win32_wrap.rb (Ancurio 2014, Splendide Imaginarius 2023-2024, CC0)
# 확장: 핸드헬드(Rocknix 등) 지원을 위한 패드 입력 지원 추가
#
# 용도: Kboard.keyboard() / GetAsyncKeyState() 등으로 키보드를 감지하는
#       RPG Maker 게임을 mkxp-z에서 실행할 때, 실제 키보드 입력 대신
#       패드 버튼 입력을 감지하게 해줌.
#       (Wayland compositor에서 GPTK 가상 키보드가 SDL 앱에 도달 못 하는
#        이슈를 우회하는 핸드헬드 전용 솔루션)
#
# 사용법:
#   1. 이 파일을 mkxp의 preload 폴더에 배치
#   2. 같은 preload 폴더에 게임별 keymap.rb 도 배치 (이 파일보다 먼저 로드되도록
#      파일명에 00_ 접두사 권장)
#   3. keymap.rb 에서 $KEYMAP_PAD 해시로 매핑 지정
#
#   예시 (이터널 세니아):
#     $KEYMAP_PAD = {
#       :C => :X,    # C키 감지 요청 시 Pad X 확인
#       :E => :Z,    # E키 감지 요청 시 Pad R1 확인 (:Z 심볼)
#       :R => :A,    # R키 감지 요청 시 Pad R2 확인 (:A 심볼)
#     }
#
#   게임이 표준 인풋만 쓰면 keymap.rb 없어도 됨 (그냥 원본 win32_wrap 동작)

module Scancodes
	SDL = { :UNKNOWN => 0x00,
		:A => 0x04, :B => 0x05, :C => 0x06, :D => 0x07,
		:E => 0x08, :F => 0x09, :G => 0x0A, :H => 0x0B,
		:I => 0x0C, :J => 0x0D, :K => 0x0E, :L => 0x0F,
		:M => 0x10, :N => 0x11, :O => 0x12, :P => 0x13,
		:Q => 0x14, :R => 0x15, :S => 0x16, :T => 0x17,
		:U => 0x18, :V => 0x19, :W => 0x1A, :X => 0x1B,
		:Y => 0x1C, :Z => 0x1D, :N1 => 0x1E, :N2 => 0x1F,
		:N3 => 0x20, :N4 => 0x21, :N5 => 0x22, :N6 => 0x23,
		:N7 => 0x24, :N8 => 0x25, :N9 => 0x26, :N0 => 0x27,
		:RETURN => 0x28, :ESCAPE => 0x29, :BACKSPACE => 0x2A, :TAB => 0x2B,
		:SPACE => 0x2C, :MINUS => 0x2D, :EQUALS => 0x2E, :LEFTBRACKET => 0x2F,
		:RIGHTBRACKET => 0x30, :BACKSLASH => 0x31, :NONUSHASH => 0x32, :SEMICOLON => 0x33,
		:APOSTROPHE => 0x34, :GRAVE => 0x35, :COMMA => 0x36, :PERIOD => 0x37,
		:SLASH => 0x38, :CAPSLOCK => 0x39, :F1 => 0x3A, :F2 => 0x3B,
		:F3 => 0x3C, :F4 => 0x3D, :F5 => 0x3E, :F6 => 0x3F,
		:F7 => 0x40, :F8 => 0x41, :F9 => 0x42, :F10 => 0x43,
		:F11 => 0x44, :F12 => 0x45, :PRINTSCREEN => 0x46, :SCROLLLOCK => 0x47,
		:PAUSE => 0x48, :INSERT => 0x49, :HOME => 0x4A, :PAGEUP => 0x4B,
		:DELETE => 0x4C, :END => 0x4D, :PAGEDOWN => 0x4E, :RIGHT => 0x4F,
		:LEFT => 0x50, :DOWN => 0x51, :UP => 0x52, :NUMLOCKCLEAR => 0x53,
		:KP_DIVIDE => 0x54, :KP_MULTIPLY => 0x55, :KP_MINUS => 0x56, :KP_PLUS => 0x57,
		:KP_ENTER => 0x58, :KP_1 => 0x59, :KP_2 => 0x5A, :KP_3 => 0x5B,
		:KP_4 => 0x5C, :KP_5 => 0x5D, :KP_6 => 0x5E, :KP_7 => 0x5F,
		:KP_8 => 0x60, :KP_9 => 0x61, :KP_0 => 0x62, :KP_PERIOD => 0x63,
		:NONUSBACKSLASH => 0x64, :APPLICATION => 0x65, :POWER => 0x66, :KP_EQUALS => 0x67,
		:F13 => 0x68, :F14 => 0x69, :F15 => 0x6A, :F16 => 0x6B,
		:F17 => 0x6C, :F18 => 0x6D, :F19 => 0x6E, :F20 => 0x6F,
		:F21 => 0x70, :F22 => 0x71, :F23 => 0x72, :F24 => 0x73,
		:EXECUTE => 0x74, :HELP => 0x75, :MENU => 0x76, :SELECT => 0x77,
		:STOP => 0x78, :AGAIN => 0x79, :UNDO => 0x7A, :CUT => 0x7B,
		:COPY => 0x7C, :PASTE => 0x7D, :FIND => 0x7E, :MUTE => 0x7F,
		:VOLUMEUP => 0x80, :VOLUMEDOWN => 0x81,
		:LCTRL => 0xE0, :LSHIFT => 0xE1, :LALT => 0xE2, :LGUI => 0xE3,
		:RCTRL => 0xE4, :RSHIFT => 0xE5, :RALT => 0xE6, :RGUI => 0xE7,
	}
	SDL.default = SDL[:UNKNOWN]

	WIN32 = {
		:LBUTTON => 0x01, :RBUTTON => 0x02, :MBUTTON => 0x04,
		:BACK => 0x08, :TAB => 0x09, :RETURN => 0x0D, :SHIFT => 0x10,
		:CONTROL => 0x11, :MENU => 0x12, :PAUSE => 0x13, :CAPITAL => 0x14,
		:ESCAPE => 0x1B, :SPACE => 0x20, :PRIOR => 0x21, :NEXT => 0x22,
		:END => 0x23, :HOME => 0x24, :LEFT => 0x25, :UP => 0x26,
		:RIGHT => 0x27, :DOWN => 0x28, :PRINT => 0x2A, :INSERT => 0x2D,
		:DELETE => 0x2E,
		:N0 => 0x30, :N1 => 0x31, :N2 => 0x32, :N3 => 0x33,
		:N4 => 0x34, :N5 => 0x35, :N6 => 0x36, :N7 => 0x37, :N8 => 0x38, :N9 => 0x39,
		:A => 0x41, :B => 0x42, :C => 0x43, :D => 0x44, :E => 0x45, :F => 0x46,
		:G => 0x47, :H => 0x48, :I => 0x49, :J => 0x4A, :K => 0x4B, :L => 0x4C,
		:M => 0x4D, :N => 0x4E, :O => 0x4F, :P => 0x50, :Q => 0x51, :R => 0x52,
		:S => 0x53, :T => 0x54, :U => 0x55, :V => 0x56, :W => 0x57, :X => 0x58,
		:Y => 0x59, :Z => 0x5A,
		:LWIN => 0x5B, :RWIN => 0x5C,
		:NUMPAD0 => 0x60, :NUMPAD1 => 0x61, :NUMPAD2 => 0x62, :NUMPAD3 => 0x63,
		:NUMPAD4 => 0x64, :NUMPAD5 => 0x65, :NUMPAD6 => 0x66, :NUMPAD7 => 0x67,
		:NUMPAD8 => 0x68, :NUMPAD9 => 0x69,
		:MULTIPLY => 0x6A, :ADD => 0x6B, :SEPARATOR => 0x6C, :SUBSTRACT => 0x6D,
		:DECIMAL => 0x6E, :DIVIDE => 0x6F,
		:F1 => 0x70, :F2 => 0x71, :F3 => 0x72, :F4 => 0x73,
		:F5 => 0x74, :F6 => 0x75, :F7 => 0x76, :F8 => 0x77,
		:F9 => 0x78, :F10 => 0x79, :F11 => 0x7A, :F12 => 0x7B,
		:NUMLOCK => 0x90, :SCROLL => 0x91,
		:LSHIFT => 0xA0, :RSHIFT => 0xA1, :LCONTROL => 0xA2, :RCONTROL => 0xA3,
		:LMENU => 0xA4, :RMENU => 0xA5,
		:OEM_1 => 0xBA,
		:OEM_PLUS => 0xBB, :OEM_COMMA => 0xBC, :OEM_MINUS => 0xBD, :OEM_PERIOD => 0xBE,
		:OEM_2 => 0xBF, :OEM_3 => 0xC0, :OEM_4 => 0xDB, :OEM_5 => 0xDC,
		:OEM_6 => 0xDD, :OEM_7 => 0xDE
	}
	WIN32INV = WIN32.invert

	WIN2SDL = {
		:BACK => :BACKSPACE, :CAPITAL => :CAPSLOCK,
		:PRIOR => :PAGEUP, :NEXT => :PAGEDOWN, :PRINT => :PRINTSCREEN,
		:LWIN => :LGUI, :RWIN => :RGUI,
		:NUMPAD0 => :KP_0, :NUMPAD1 => :KP_1, :NUMPAD2 => :KP_2, :NUMPAD3 => :KP_3,
		:NUMPAD4 => :KP_4, :NUMPAD5 => :KP_5, :NUMPAD6 => :KP_6, :NUMPAD7 => :KP_7,
		:NUMPAD8 => :KP_8, :NUMPAD9 => :KP_9,
		:MULTIPLY => :KP_MULTIPLY, :ADD => :KP_PLUS, :SUBSTRACT => :KP_MINUS,
		:DECIMAL => :KP_DECIMAL, :DIVIDE => :KP_DIVIDE,
		:NUMLOCK => :NUMLOCKCLEAR, :SCROLL => :SCROLLLOCK,
		:LCONTROL => :LCTRL, :RCONTROL => :RCTRL,
		:LMENU => :LALT, :RMENU => :RALT,
		:OEM_1 => :SEMICOLON,
		:OEM_PLUS => :EQUALS, :OEM_COMMA => :COMMA, :OEM_MINUS => :MINUS, :OEM_PERIOD => :PERIOD,
		:OEM_2 => :SLASH, :OEM_3 => :GRAVE, :OEM_4 => :LEFTBRACKET, :OEM_5 => :BACKSLASH,
		:OEM_6 => :RIGHTBRACKET, :OEM_7 => :APOSTROPHE
	}
	WIN2SDL.default = :UNKNOWN
end

$win32KeyStates = nil

module Graphics
	class << self
		alias_method(:win32wrap_update, :update)
		def update
			win32wrap_update
			$win32KeyStates = nil
		end
	end
end

def get_raw_keystates
	$win32KeyStates = Input.raw_key_states if $win32KeyStates == nil
	return $win32KeyStates
end

# ★★★ 핸드헬드 전용 확장: 키 감지를 패드 버튼으로 리다이렉트 ★★★
# $KEYMAP_PAD 전역 해시가 있으면 거기 매핑된 키는 패드 버튼 OR 키보드 체크
# 예: $KEYMAP_PAD = { :C => :X }  → C키 감지 요청 시 Pad X 또는 키보드 C 체크
#
# OR 로직이라 GPTK가 복구되어도, 키보드가 있어도 문제없이 동작
# 즉 패드 X 누르거나, 키보드 C 누르거나 둘 중 하나만 눌려도 감지됨
def pad_keystate_pressed(vkey_name)
	return false unless defined?($KEYMAP_PAD) && $KEYMAP_PAD.is_a?(Hash)
	pad_sym = $KEYMAP_PAD[vkey_name]
	return false unless pad_sym
	begin
		return Input.press?(pad_sym)
	rescue
		return false
	end
end

def common_keystate(vkey)
	vkey_name = Scancodes::WIN32INV[vkey]

	# 이하 원본 win32_wrap 로직 (SDL 키보드 상태 확인)
	states = get_raw_keystates
	pressed = false

	if vkey_name == :LBUTTON
		pressed = Input.press?(Input::MOUSELEFT)
	elsif vkey_name == :RBUTTON
		pressed = Input.press?(Input::MOUSERIGHT)
	elsif vkey_name == :MBUTTON
		pressed = Input.press?(Input::MOUSEMIDDLE)
	elsif vkey_name == :SHIFT
		pressed = double_state(states, :LSHIFT, :RSHIFT)
	elsif vkey_name == :MENU
		pressed = double_state(states, :LALT, :RALT)
	elsif vkey_name == :CONTROL
		pressed = double_state(states, :LCTRL, :RCTRL)
	else
		scan = nil
		if Scancodes::SDL.key?(vkey_name)
			scan = vkey_name
		else
			scan = Scancodes::WIN2SDL[vkey_name]
		end
		pressed = state_pressed(states, scan)
	end

	# ★ 패드 OR 키보드 - 둘 중 하나라도 눌리면 true
	# 이렇게 하면 GPTK가 복구되거나 실제 키보드가 있어도 정상 동작
	pressed = pressed || pad_keystate_pressed(vkey_name)

	return pressed ? 1 : 0
end

def memcpy_string(dst, src)
	i = 0
	src.each_byte do |b|
		if dst.respond_to?(:setbyte)
			dst.setbyte(i, b)
		else
			dst[i] = b
		end
		i += 1
	end
end

def state_pressed(states, sdl_scan)
	return states[Scancodes::SDL[sdl_scan]]
end

def double_state(states, left, right)
	return state_pressed(states, left) || state_pressed(states, right)
end

module Win32API_Impl
	module User32
		def self.display_width
			Graphics.respond_to?(:display_width) ? Graphics.display_width : Graphics.width
		end

		def self.display_height
			Graphics.respond_to?(:display_height) ? Graphics.display_height : Graphics.height
		end

		class CreateWindowEx
			def call(args); return 43; end
		end

		class GetDC
			def call(args); return 1; end
		end

		class GetSystemMetrics
			def call(args)
				return User32.display_width if args[0].to_i == 0
				return User32.display_height if args[0].to_i == 1
				return 0
			end
		end

		class GetWindowRect
			def call(args)
				memcpy_string(args[1], [0, 0, Graphics.width, Graphics.height].pack('l4'))
				return 1
			end
		end

		class FillRect
			def call(args); return 1; end
		end

		class ReleaseDC
			def call(args); return 1; end
		end

		class SendInput
			def call(args); return args[0].to_i; end
		end

		class SetWindowLong
			def call(args); return args[2].to_i; end
		end

		class GetWindowLong
			def call(args); return 0; end
		end

		class SetWindowPos
			def call(args)
				w = args[4].to_i
				h = args[5].to_i
				begin
					if Graphics.respond_to?(:fullscreen=) && w >= User32.display_width && h >= User32.display_height
						Graphics.fullscreen = true
					elsif Graphics.respond_to?(:resize_window) && w > 0 && h > 0
						Graphics.fullscreen = false if Graphics.respond_to?(:fullscreen=)
						Graphics.resize_window(w, h, true)
					end
				rescue
				end
				return 1
			end
		end

		class ShowWindow
			def call(args); return 1; end
		end

		class SystemParametersInfo
			def call(args)
				if args[0].to_i == 0x30 && args[2].is_a?(String)
					memcpy_string(args[2], [0, 0, User32.display_width, User32.display_height].pack('l4'))
				end
				return 1
			end
		end

		class UpdateWindow
			def call(args); return 1; end
		end

		class GetForegroundWindow
			def call(args); return 42; end
		end

		class Keybd_event
			Seq = [[0xA4, 0, 0, 0], [0xD, 0, 0, 0], [0xD, 0, 2, 0], [0xA4, 0, 2, 0]]
			Seq2 = [[0x12, 0, 0, 0], [0xD, 0, 0, 0], [0xD, 0, 2, 0], [0x12, 0, 2, 0]]
			def initialize; @index = 0; end
			def call(args)
				seq = [args[0], args[1], args[2], args[3]]
				if seq == Seq[@index] or seq == Seq2[@index]
					@index += 1
				else
					@index = 0
				end
				if @index == 4
					@index = 0
					Graphics.fullscreen = !Graphics.fullscreen
				end
			end
		end

		class GetKeyState
			def call(vkey); return common_keystate(vkey[0]); end
		end
		class GetAsyncKeyState
			PRESSED_BIT = (1 << 15)
			def call(vkey); return common_keystate(vkey[0]) == 1 ? PRESSED_BIT : 0; end
		end
		class GetKeyboardState
			PRESSED_BIT = 0x80
			def call(args)
				out_states = args[0]
				Scancodes::WIN32.each do |name, val|
					pressed = common_keystate(val) == 1
					out_states.setbyte(val, pressed ? PRESSED_BIT : 0)
				end
				return 1
			end
		end
		class ShowCursor
			def initialize; @cursor_count = 0; end
			def call(args)
				if args[0] == 1
					@cursor_count += 1
				else
					@cursor_count -= 1
				end
				Graphics.show_cursor = @cursor_count >= 0
			end
		end
		class GetCursorPos
			def call(args)
				out = [Input.mouse_x, Input.mouse_y].pack('ll')
				memcpy_string(args[0], out)
				return 1
			end
		end
		class GetClientRect
			def call(args)
				return 0 if args[0] != 42
				rect = [0, 0, 640, 480]
				begin
					rect[2] = Graphics.width
					rect[3] = Graphics.height
				rescue
				end
				memcpy_string(args[1], rect.pack('l4'))
				return 1
			end
		end
		class ScreenToClient
			def call(args); return 1; end
		end
		class FindWindowA
			def call(args)
				if args[0] == "RGSS Player"
					return 42
				else
					return 0
				end
			end
		end
		class FindWindow < FindWindowA; end
	end
	module Kernel32
		class GetPrivateProfileString
			def call(args)
				section, key, default_value, buffer, size, filename = args
				value = default_value.to_s
				begin
					current = nil
					File.foreach(filename.to_s) do |line|
						text = line.strip
						if text =~ /^\[(.+)\]$/
							current = $1
						elsif current == section.to_s && text =~ /^([^=]+)=(.*)$/ && $1.strip == key.to_s
							value = $2.strip
							break
						end
					end
				rescue
				end
				max = [size.to_i - 1, 0].max
				value = value.to_s[0, max] || ""
				memcpy_string(buffer, value + "\0")
				return value.bytesize
			end
		end
		class WritePrivateProfileString
			def call(args); return 1; end
		end
	end

	module Gdi32
		class CreateSolidBrush
			def call(args); return 1; end
		end
		class DeleteObject
			def call(args); return 1; end
		end
	end
end

def kappatalize(s)
	s[0, 1] = s[0, 1].upcase
	return s
end

class Win32API
	NATIVE_ON_WINDOWS = true unless const_defined?("NATIVE_ON_WINDOWS")
	TOLERATE_ERRORS = true unless const_defined?("TOLERATE_ERRORS")
	LOG_NATIVE = false unless const_defined?("LOG_NATIVE")

	alias_method :mkxp_native_initialize, :initialize
	def initialize(dll, func, *args)
		@dll = dll
		@func = func
		@called = false

		dll = kappatalize(dll.chomp(".dll"))
		func = kappatalize(func)

		if !System.is_windows? or !NATIVE_ON_WINDOWS
			if Win32API_Impl.const_defined?(dll)
				dll_impl = Win32API_Impl.const_get(dll)
				if dll_impl.const_defined?(func)
					@mkxp_wrap_impl = dll_impl.const_get(func).new
					return
				end
			end
		end

		@mkxp_native_available = false
		begin
			mkxp_native_initialize(@dll, @func, *args)
			@mkxp_native_available = true
			return
		rescue
		end
	end

	alias_method :mkxp_native_call, :call
	def call(*args)
		if @mkxp_wrap_impl
			return @mkxp_wrap_impl.call(args)
		end

		if @mkxp_native_available
			if LOG_NATIVE
				System.puts("[Win32API] [#{@dll}:#{@func}] #{args.to_s}")
			end
			return mkxp_native_call(*args)
		end

		if TOLERATE_ERRORS
			System.puts("[Win32API] [#{@dll}:#{@func}] #{args.to_s}") if !@called
			@called = true
			return 0
		else
			raise RuntimeError, "[Win32API] [#{@dll}:#{@func}] #{args.to_s}"
		end
	end
end

puts "[preload] win32_wrap_pad 로드됨 (패드 지원 버전)"
puts "[preload] $KEYMAP_PAD = #{$KEYMAP_PAD.inspect}" if defined?($KEYMAP_PAD)
