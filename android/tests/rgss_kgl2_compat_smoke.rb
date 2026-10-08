class Win32API
  attr_reader :original_dll, :original_func
  def initialize(dll, func, *args)
    @original_dll = dll
    @original_func = func
  end
  def call(*args)
    [:original, @original_dll, @original_func, args]
  end
end

class Rect
  attr_reader :x, :y, :width, :height
  def initialize(x, y, width, height)
    @x, @y, @width, @height = x, y, width, height
  end
end

class Color
  attr_reader :red, :green, :blue, :alpha
  def initialize(red, green, blue, alpha=255)
    @red, @green, @blue, @alpha = red, green, blue, alpha
  end
end

class Bitmap
  attr_reader :width, :height, :calls
  def initialize(width, height)
    @width, @height = width, height
    @calls = []
  end
  def rect; Rect.new(0, 0, @width, @height); end
  def clear; @calls << [:clear]; end
  def fill_rect(rect, color); @calls << [:fill_rect, rect, color]; end
  def blt(x, y, src, rect); @calls << [:blt, x, y, src, rect]; end
  def _kgl_invert; @calls << [:_kgl_invert]; end
  def _kgl_compress_alpha; @calls << [:_kgl_compress_alpha]; end
  def _kgl_subtract_rect(x, y, src, rect, opacity)
    @calls << [:_kgl_subtract_rect, x, y, src, rect, opacity]
  end
  def _kgl_shadow_shader_h(x1, x2, y, soft)
    @calls << [:_kgl_shadow_shader_h, x1, x2, y, soft]
    71
  end
  def _kgl_shadow_shader_v(y1, y2, x, wall, soft)
    @calls << [:_kgl_shadow_shader_v, y1, y2, x, wall, soft]
    wall ? 73 : 72
  end
end

wrapper = File.expand_path('../app/src/main/assets/rgss_compat/common/kgl2_wrap.rb', __dir__)
load wrapper

def api(name)
  Win32API.new('System/KGL2.klib', name, '', 'i')
end

raise 'KGL2 version failed' unless api('kglVersion').call == 200
raise 'KGL2 load failed' unless api('kglLoad').call == 1
raise 'KGL2 repeated load code failed' unless api('kglLoad').call == 102

bitmap = Bitmap.new(8, 6)
src = Bitmap.new(3, 2)

raise 'blank failed' unless api('kglBlank').call(bitmap.object_id) == 1 && bitmap.calls[-1] == [:clear]
raise 'invert failed' unless api('kglInvert').call(bitmap.object_id) == 1 && bitmap.calls[-1] == [:_kgl_invert]
raise 'compress alpha failed' unless api('kglCompressAlpha').call(bitmap.object_id) == 1 && bitmap.calls[-1] == [:_kgl_compress_alpha]

status = api('kglClear').call(bitmap.object_id, 0x4d1a2b3c)
call = bitmap.calls[-1]
color = call[2]
raise 'clear failed' unless status == 1 && call[0] == :fill_rect &&
  [color.red, color.green, color.blue, color.alpha] == [0x1a, 0x2b, 0x3c, 0x4d]

same = Bitmap.new(3, 2)
raise 'clone equal failed' unless api('kglClone').call(same.object_id, src.object_id) == 1 && same.calls[-1][0] == :blt
different = Bitmap.new(4, 2)
raise 'clone mismatch code failed' unless api('kglClone').call(different.object_id, src.object_id) == 112

raise 'framebuffer bind failed' unless api('kglBindFramebuffer').call(bitmap.object_id) == 1
raise 'light blending failed' unless api('kglLightBlending').call(1) == 1
raise 'light shader failed' unless api('kglLightShader').call(src.object_id, -1, 1, 127) == 1
light_call = bitmap.calls[-1]
raise 'light shader route failed' unless light_call[0] == :_kgl_subtract_rect && light_call[-1] == 127

raise 'shadowbuffer bind failed' unless api('kglBindShadowbuffer').call(bitmap.object_id) == 1
raise 'soft shadows failed' unless api('kglSoftShadows').call(1) == 1
raise 'shadow H route failed' unless api('kglShadowShaderH').call(1, 5, 2) == 71 && bitmap.calls[-1] == [:_kgl_shadow_shader_h, 1, 5, 2, true]
raise 'shadow V route failed' unless api('kglShadowShaderV').call(1, 4, 2) == 72 && bitmap.calls[-1] == [:_kgl_shadow_shader_v, 1, 4, 2, false, true]
raise 'shadow W route failed' unless api('kglShadowShaderW').call(1, 4, 2) == 73 && bitmap.calls[-1] == [:_kgl_shadow_shader_v, 1, 4, 2, true, true]

raise 'framebuffer unbind failed' unless api('kglUnbindFramebuffer').call == 1
raise 'framebuffer missing error failed' unless api('kglClearFramebuffer').call == 103
raise 'shadowbuffer unbind failed' unless api('kglUnbindShadowbuffer').call == 1
raise 'shadowbuffer missing error failed' unless api('kglShadowShaderH').call(1, 2, 3) == 105

other = Win32API.new('user32.dll', 'MessageBoxA', 'llll', 'i')
result = other.call(1, 2)
raise 'non-KGL Win32API interception regression' unless result[0] == :original && result[1] == 'user32.dll'

puts 'RGSS_KGL2_COMPAT_SMOKE_PASS'
