# RPGMP Android native-extension compatibility layer.
#
# This file is an original implementation.  It does not contain code from
# proprietary JoiPlay/RPGJoy components or from third-party Windows DLLs.
# It only provides documented/public RGSS API shapes on top of mkxp-z APIs.

module RPGMPNativeExtCompat
  VERSION = "1.0"

  def self.full_rect(bitmap)
    Rect.new(0, 0, bitmap.width, bitmap.height)
  end

  def self.copy_bitmap(bitmap)
    out = Bitmap.new(bitmap.width, bitmap.height)
    out.blt(0, 0, bitmap, full_rect(bitmap))
    out
  end

  def self.color_copy(color, alpha = nil)
    a = alpha.nil? ? color.alpha : alpha
    Color.new(color.red, color.green, color.blue, a)
  end
end

# --------------------------------------------------------------------------
# Screen capture compatibility
# --------------------------------------------------------------------------
if defined?(Graphics) && Graphics.respond_to?(:snap_to_bitmap)
  unless Graphics.respond_to?(:screenshot)
    def Graphics.screenshot(filename = nil)
      bitmap = Graphics.snap_to_bitmap
      if filename && bitmap.respond_to?(:to_file)
        path = filename.to_s
        path += ".png" if File.extname(path).to_s.empty?
        bitmap.to_file(path)
      end
      bitmap
    end
  end

  unless Graphics.respond_to?(:capture_screen)
    def Graphics.capture_screen(filename = nil)
      Graphics.screenshot(filename)
    end
  end

  unless Graphics.respond_to?(:save_screenshot)
    def Graphics.save_screenshot(filename = "Screenshot.png")
      bitmap = Graphics.screenshot(filename)
      begin
        bitmap.dispose if bitmap && bitmap.respond_to?(:dispose) && !bitmap.disposed?
      rescue
      end
      filename
    end
  end
end

# --------------------------------------------------------------------------
# Klein Bitmap Functions compatible API (clean-room implementation)
# --------------------------------------------------------------------------
if defined?(Bitmap)
  class Bitmap
    unless method_defined?(:scroll_up)
      def scroll_up(speed)
        amount = speed.to_i
        return self if height <= 0
        amount %= height
        return self if amount == 0
        tmp = RPGMPNativeExtCompat.copy_bitmap(self)
        clear
        blt(0, 0, tmp, Rect.new(0, amount, width, height - amount))
        blt(0, height - amount, tmp, Rect.new(0, 0, width, amount))
        tmp.dispose
        self
      end
    end

    unless method_defined?(:scroll_down)
      def scroll_down(speed)
        amount = speed.to_i
        return self if height <= 0
        amount %= height
        return self if amount == 0
        scroll_up(height - amount)
      end
    end

    unless method_defined?(:scroll_left)
      def scroll_left(speed)
        amount = speed.to_i
        return self if width <= 0
        amount %= width
        return self if amount == 0
        tmp = RPGMPNativeExtCompat.copy_bitmap(self)
        clear
        blt(0, 0, tmp, Rect.new(amount, 0, width - amount, height))
        blt(width - amount, 0, tmp, Rect.new(0, 0, amount, height))
        tmp.dispose
        self
      end
    end

    unless method_defined?(:scroll_right)
      def scroll_right(speed)
        amount = speed.to_i
        return self if width <= 0
        amount %= width
        return self if amount == 0
        scroll_left(width - amount)
      end
    end

    unless method_defined?(:to_negative)
      def to_negative
        y = 0
        while y < height
          x = 0
          while x < width
            c = get_pixel(x, y)
            if c.alpha > 0
              set_pixel(x, y, Color.new(255 - c.red, 255 - c.green, 255 - c.blue, c.alpha))
            end
            x += 1
          end
          y += 1
        end
        self
      end
    end

    unless method_defined?(:to_retro)
      def to_retro(c1, c2, c3, c4)
        palette = [c1, c2, c3, c4]
        y = 0
        while y < height
          x = 0
          while x < width
            c = get_pixel(x, y)
            if c.alpha > 0
              lum = (c.red * 30 + c.green * 59 + c.blue * 11) / 100
              index = lum < 64 ? 0 : (lum < 128 ? 1 : (lum < 192 ? 2 : 3))
              pc = palette[index]
              set_pixel(x, y, RPGMPNativeExtCompat.color_copy(pc, c.alpha))
            end
            x += 1
          end
          y += 1
        end
        self
      end
    end

    unless method_defined?(:add_outline)
      def add_outline(color, pixelsize = 1)
        radius = [pixelsize.to_i, 1].max
        source = RPGMPNativeExtCompat.copy_bitmap(self)
        y = 0
        while y < height
          x = 0
          while x < width
            current = source.get_pixel(x, y)
            if current.alpha <= 0
              hit = false
              dy = -radius
              while dy <= radius && !hit
                dx = -radius
                while dx <= radius
                  if dx != 0 || dy != 0
                    sx = x + dx
                    sy = y + dy
                    if sx >= 0 && sy >= 0 && sx < width && sy < height
                      if source.get_pixel(sx, sy).alpha > 0
                        hit = true
                        break
                      end
                    end
                  end
                  dx += 1
                end
                dy += 1
              end
              set_pixel(x, y, color) if hit
            end
            x += 1
          end
          y += 1
        end
        source.dispose
        self
      end
    end
  end
end

if defined?(Sprite)
  class Sprite
    unless method_defined?(:set_pattern)
      def set_pattern(pattern, alpha = 255, everyframe = false)
        delete_pattern if instance_variable_defined?(:@rpgmp_klein_pattern_bitmap)
        return false if bitmap.nil?

        pattern_bitmap = pattern
        owned_pattern = false
        unless pattern_bitmap.is_a?(Bitmap)
          begin
            pattern_bitmap = Bitmap.new(pattern.to_s)
            owned_pattern = true
          rescue
            return false
          end
        end

        original = bitmap
        composed = RPGMPNativeExtCompat.copy_bitmap(original)
        if pattern_bitmap.width > 0 && pattern_bitmap.height > 0
          y = 0
          while y < composed.height
            x = 0
            while x < composed.width
              w = [pattern_bitmap.width, composed.width - x].min
              h = [pattern_bitmap.height, composed.height - y].min
              composed.blt(x, y, pattern_bitmap, Rect.new(0, 0, w, h), alpha.to_i)
              x += pattern_bitmap.width
            end
            y += pattern_bitmap.height
          end
        end

        pattern_bitmap.dispose if owned_pattern
        @rpgmp_klein_original_bitmap = original
        @rpgmp_klein_pattern_bitmap = composed
        @rpgmp_klein_pattern_everyframe = everyframe ? true : false
        self.bitmap = composed
        true
      end
    end

    unless method_defined?(:delete_pattern)
      def delete_pattern
        overlay = instance_variable_defined?(:@rpgmp_klein_pattern_bitmap) ? @rpgmp_klein_pattern_bitmap : nil
        original = instance_variable_defined?(:@rpgmp_klein_original_bitmap) ? @rpgmp_klein_original_bitmap : nil
        self.bitmap = original if overlay && original && self.bitmap.equal?(overlay)
        begin
          overlay.dispose if overlay && overlay.respond_to?(:dispose) && !overlay.disposed?
        rescue
        end
        @rpgmp_klein_pattern_bitmap = nil
        @rpgmp_klein_original_bitmap = nil
        @rpgmp_klein_pattern_everyframe = false
        true
      end
    end
  end
end

module Kernel
  unless method_defined?(:klein_bitmap_version)
    def klein_bitmap_version
      "RPGMP-KleinCompat-1.0"
    end
  end

  unless method_defined?(:compare_klein_bitmaps)
    def compare_klein_bitmaps(a, b)
      return false if a.nil? || b.nil?
      return false unless a.respond_to?(:width) && b.respond_to?(:width)
      return false unless a.width == b.width && a.height == b.height
      y = 0
      while y < a.height
        x = 0
        while x < a.width
          ca = a.get_pixel(x, y)
          cb = b.get_pixel(x, y)
          return false unless ca.red == cb.red && ca.green == cb.green && ca.blue == cb.blue && ca.alpha == cb.alpha
          x += 1
        end
        y += 1
      end
      true
    end
  end

  unless method_defined?(:klein_dll)
    def klein_dll(func, send = nil, get = nil)
      0
    end
  end
end

puts "[preload] RPGMP native extension compatibility loaded"
