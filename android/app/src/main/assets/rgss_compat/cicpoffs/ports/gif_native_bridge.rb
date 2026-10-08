# encoding: UTF-8
# RPGMP native GifLibrary compatibility bridge.
#
# Old Pokemon Essentials / RGSS projects use gif.dll to split animated GIFs
# into PNG frames. mkxp-z already has a native libnsgif decoder and exposes
# animation controls directly on Bitmap, so keep the game's GifBitmap wrapper
# while delegating animation playback/frame metadata to the native Bitmap.

class GifBitmap
  unless method_defined?(:rpgmp_native_gif_original_initialize)
    alias rpgmp_native_gif_original_initialize initialize
    alias rpgmp_native_gif_original_length length
    alias rpgmp_native_gif_original_currentIndex currentIndex
    alias rpgmp_native_gif_original_frameDelay frameDelay
    alias rpgmp_native_gif_original_totalFrames totalFrames
    alias rpgmp_native_gif_original_update update
    alias rpgmp_native_gif_original_deanimate deanimate
    alias rpgmp_native_gif_original_index []
    alias rpgmp_native_gif_original_each each
  end

  def rpgmp_native_gif_bitmap
    return nil if !@gifbitmaps || @gifbitmaps.length != 1
    bm = @gifbitmaps[0]
    return nil if !bm || !bm.respond_to?(:animated?) || !bm.animated?
    bm
  rescue
    nil
  end

  def initialize(file, hue=0)
    rpgmp_native_gif_original_initialize(file, hue)
    bm = rpgmp_native_gif_bitmap
    if bm
      @rpgmp_native_gif = true
      bm.play if bm.respond_to?(:play)
      @currentIndex = bm.respond_to?(:current_frame) ? bm.current_frame.to_i : 0
    end
  end

  def length
    bm = rpgmp_native_gif_bitmap
    return bm.frame_count.to_i if bm && bm.respond_to?(:frame_count)
    rpgmp_native_gif_original_length
  end

  def currentIndex
    bm = rpgmp_native_gif_bitmap
    return bm.current_frame.to_i if bm && bm.respond_to?(:current_frame)
    rpgmp_native_gif_original_currentIndex
  end

  def frameDelay(index=0)
    bm = rpgmp_native_gif_bitmap
    if bm && bm.respond_to?(:frame_rate)
      fps = bm.frame_rate.to_f
      if fps > 0.0
        base = (Graphics.respond_to?(:frame_rate) ? Graphics.frame_rate.to_f : 60.0)
        delay = (base / fps).round
        return delay < 1 ? 1 : delay
      end
    end
    rpgmp_native_gif_original_frameDelay(index)
  rescue
    1
  end

  def totalFrames
    bm = rpgmp_native_gif_bitmap
    return length * frameDelay(0) if bm
    rpgmp_native_gif_original_totalFrames
  end

  def update
    bm = rpgmp_native_gif_bitmap
    if bm
      begin
        bm.play if bm.respond_to?(:playing) && !bm.playing && bm.respond_to?(:play)
      rescue
      end
      @currentIndex = bm.respond_to?(:current_frame) ? bm.current_frame.to_i : 0
      return
    end
    rpgmp_native_gif_original_update
  end

  def deanimate
    bm = rpgmp_native_gif_bitmap
    if bm
      begin
        bm.goto_and_stop(0) if bm.respond_to?(:goto_and_stop)
      rescue
        bm.stop if bm.respond_to?(:stop)
      end
      @currentIndex = 0
      return bm
    end
    rpgmp_native_gif_original_deanimate
  end

  def [](index)
    bm = rpgmp_native_gif_bitmap
    return rpgmp_native_gif_original_index(index) if !bm
    return bm if !bm.respond_to?(:goto_and_stop) || !bm.respond_to?(:snap_to_bitmap)
    old = bm.respond_to?(:current_frame) ? bm.current_frame.to_i : 0
    playing = bm.respond_to?(:playing) ? !!bm.playing : false
    count = bm.respond_to?(:frame_count) ? bm.frame_count.to_i : 1
    target = index.to_i
    target = 0 if target < 0
    target = count - 1 if count > 0 && target >= count
    bm.goto_and_stop(target)
    snap = bm.snap_to_bitmap
    if playing && bm.respond_to?(:goto_and_play)
      bm.goto_and_play(old)
    else
      bm.goto_and_stop(old)
    end
    snap
  rescue
    rpgmp_native_gif_original_index(index)
  end

  def each
    bm = rpgmp_native_gif_bitmap
    if bm && block_given?
      i = 0
      while i < length
        yield self[i]
        i += 1
      end
      return self
    end
    rpgmp_native_gif_original_each { |item| yield item }
  end
end

begin
  System.puts('[RPGMP GIF] native mkxp-z GifLibrary bridge active') if defined?(System) && System.respond_to?(:puts)
rescue
end
