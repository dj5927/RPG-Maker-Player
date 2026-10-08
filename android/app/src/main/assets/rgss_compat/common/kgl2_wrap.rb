# encoding: UTF-8
# KGL2.klib compatibility for mkxp-z / Android.
#
# Ruby API contract based on the public-domain (CC0) kgl2_wrap.rb by
# white-axe (2026), with RPGMP changes for Ruby 1.8 compatibility and the
# Android Win32API wrapper. Native pixel operations live in Bitmap binding.

module KGL2_Impl
  class << self
    attr_accessor :initialized
    attr_accessor :light_blending
    attr_accessor :soft_shadows
    attr_accessor :framebuffer
    attr_accessor :shadowbuffer
  end

  self.initialized = false
  self.light_blending = false
  self.soft_shadows = false
  self.framebuffer = nil
  self.shadowbuffer = nil

  class KglVersion
    def call; 200; end
  end

  class KglLoad
    def call
      return 102 if KGL2_Impl.initialized
      KGL2_Impl.initialized = true
      1
    end
  end

  class KglBlank
    def call(bitmap_id)
      ObjectSpace._id2ref(bitmap_id).clear
      1
    end
  end

  class KglClear
    def call(bitmap_id, packed)
      bitmap = ObjectSpace._id2ref(bitmap_id)
      color = Color.new((packed >> 16) & 0xff, (packed >> 8) & 0xff,
                        packed & 0xff, (packed >> 24) & 0xff)
      bitmap.fill_rect(bitmap.rect, color)
      1
    end
  end

  class KglInvert
    def call(bitmap_id)
      ObjectSpace._id2ref(bitmap_id)._kgl_invert
      1
    end
  end

  class KglClone
    def call(dst_id, src_id)
      dst = ObjectSpace._id2ref(dst_id)
      src = ObjectSpace._id2ref(src_id)
      return 112 if dst.width != src.width || dst.height != src.height
      dst.clear
      dst.blt(0, 0, src, src.rect)
      1
    end
  end

  class KglBindFramebuffer
    def call(bitmap_id)
      return 101 unless KGL2_Impl.initialized
      KGL2_Impl.framebuffer = ObjectSpace._id2ref(bitmap_id)
      1
    end
  end

  class KglBindShadowbuffer
    def call(bitmap_id)
      return 101 unless KGL2_Impl.initialized
      KGL2_Impl.shadowbuffer = ObjectSpace._id2ref(bitmap_id)
      1
    end
  end

  class KglUnbindFramebuffer
    def call
      KGL2_Impl.framebuffer = nil
      1
    end
  end

  class KglUnbindShadowbuffer
    def call
      KGL2_Impl.shadowbuffer = nil
      1
    end
  end

  class KglClearFramebuffer
    def call
      return 103 if KGL2_Impl.framebuffer.nil?
      KGL2_Impl.framebuffer.clear
      1
    end
  end

  class KglCompressAlpha
    def call(bitmap_id)
      ObjectSpace._id2ref(bitmap_id)._kgl_compress_alpha
      1
    end
  end

  class KglLightBlending
    def call(enabled)
      KGL2_Impl.light_blending = enabled && enabled != 0 ? true : false
      1
    end
  end

  class KglLightShader
    def call(bitmap_id, x, y, opacity)
      return 103 if KGL2_Impl.framebuffer.nil?
      bitmap = ObjectSpace._id2ref(bitmap_id)
      framebuffer = KGL2_Impl.framebuffer
      x = x.to_i
      y = y.to_i

      fw = framebuffer.width - [x, 0].min
      fh = framebuffer.height - [y, 0].min
      bw = bitmap.width + [x, 0].max
      bh = bitmap.height + [y, 0].max
      width = [fw, bw].min - x.abs
      height = [fh, bh].min - y.abs
      return 111 if width < 0 || height < 0

      framebuffer_x = [x, 0].max
      framebuffer_y = [y, 0].max
      bitmap_x = -[x, 0].min
      bitmap_y = -[y, 0].min
      opacity = opacity > 100 ? 255 : 0 unless KGL2_Impl.light_blending

      framebuffer._kgl_subtract_rect(
        framebuffer_x, framebuffer_y, bitmap,
        Rect.new(bitmap_x, bitmap_y, width, height), opacity
      )
      1
    end
  end

  class KglSoftShadows
    def call(enabled)
      KGL2_Impl.soft_shadows = enabled && enabled != 0 ? true : false
      1
    end
  end

  class KglShadowShaderH
    def call(x1, x2, y)
      return 105 if KGL2_Impl.shadowbuffer.nil?
      KGL2_Impl.shadowbuffer._kgl_shadow_shader_h(
        x1, x2, y, KGL2_Impl.soft_shadows)
    end
  end

  class KglShadowShaderV
    def call(y1, y2, x)
      return 105 if KGL2_Impl.shadowbuffer.nil?
      KGL2_Impl.shadowbuffer._kgl_shadow_shader_v(
        y1, y2, x, false, KGL2_Impl.soft_shadows)
    end
  end

  class KglShadowShaderW
    def call(y1, y2, x)
      return 105 if KGL2_Impl.shadowbuffer.nil?
      KGL2_Impl.shadowbuffer._kgl_shadow_shader_v(
        y1, y2, x, true, KGL2_Impl.soft_shadows)
    end
  end
end

class Win32API
  unless method_defined?(:rpgmp_kgl2_native_initialize)
    alias_method :rpgmp_kgl2_native_initialize, :initialize
    alias_method :rpgmp_kgl2_native_call, :call

    def initialize(dll, func, *args)
      @rpgmp_kgl2_impl = nil
      dll_name = dll.to_s.tr('\\', '/').split('/').last.to_s.downcase
      if dll_name == 'kgl2.klib'
        fn = func.to_s
        const_name = fn[0, 1].to_s.upcase + fn[1, fn.length].to_s
        begin
          if KGL2_Impl.const_defined?(const_name)
            @rpgmp_kgl2_impl = KGL2_Impl.const_get(const_name).new
            return
          end
        rescue Exception
          @rpgmp_kgl2_impl = nil
        end
      end
      rpgmp_kgl2_native_initialize(dll, func, *args)
    end

    def call(*args)
      return @rpgmp_kgl2_impl.call(*args) if @rpgmp_kgl2_impl
      rpgmp_kgl2_native_call(*args)
    end
  end
end

begin
  System.puts('[RPGMP KGL2] compatibility bridge loaded') if defined?(System) && System.respond_to?(:puts)
rescue
end
