fixture = <<'RUBY'
module GifLibrary
  @@loadlib=Win32API.new("Kernel32.dll","LoadLibrary",'p','')
  if safeExists?("gif.dll")
    PngDll=@@loadlib.call("gif.dll")
    GifToPngFilesInMemory=Win32API.new("gif.dll","GifToPngFilesInMemory",'plp','l')
  else
    PngDll=nil
  end
end
class GifBitmap
  def initialize(file,hue=0); @gifbitmaps=[]; end
  def length; @gifbitmaps.length; end
  def currentIndex; 0; end
  def frameDelay(index); 1; end
  def totalFrames; 1; end
  def update; end
  def deanimate; nil; end
  def [](index); @gifbitmaps[index]; end
  def each; @gifbitmaps.each { |item| yield item }; end
end
RUBY

$RGSS_SCRIPTS = [[0, 'SpriteWindow', 0, fixture]]
compat = File.expand_path('../app/src/main/assets/rgss_compat/common/cicpoffs_compat.rb', __dir__)
load compat
patched = $RGSS_SCRIPTS[0][3]
raise 'GifLibrary pre-pass did not bypass gif.dll' unless patched.include?('RPGMP_NATIVE_GIF_DLL_BYPASS')
raise 'GifLibrary pre-pass did not append native bridge' unless patched.include?('RPGMP_NATIVE_GIF_BRIDGE')

class Graphics
  def self.frame_rate; 60; end
end

class FakeNativeGifBitmap
  attr_reader :play_calls
  def initialize
    @playing = false
    @current = 0
    @play_calls = 0
  end
  def animated?; true; end
  def play; @playing = true; @play_calls += 1; end
  def stop; @playing = false; end
  def playing; @playing; end
  def frame_count; 4; end
  def current_frame; @current; end
  def frame_rate; 12.0; end
  def goto_and_stop(frame); @current = frame; @playing = false; end
  def goto_and_play(frame); @current = frame; @playing = true; end
  def snap_to_bitmap; [:snapshot, @current]; end
end

class GifBitmap
  def initialize(file, hue=0)
    @gifbitmaps = [FakeNativeGifBitmap.new]
    @currentIndex = 0
  end
  def length; @gifbitmaps.length; end
  def currentIndex; @currentIndex; end
  def frameDelay(index); 1; end
  def totalFrames; 1; end
  def update; @currentIndex += 1; end
  def deanimate; @gifbitmaps[0]; end
  def [](index); @gifbitmaps[index]; end
  def each; @gifbitmaps.each { |item| yield item }; end
end

bridge = File.expand_path('../app/src/main/assets/rgss_compat/cicpoffs/ports/gif_native_bridge.rb', __dir__)
load bridge

gif = GifBitmap.new('dummy.gif')
native = gif.instance_variable_get(:@gifbitmaps)[0]
raise 'native GIF did not auto-play' unless native.playing && native.play_calls == 1
raise 'frame_count bridge failed' unless gif.length == 4
raise 'frame delay bridge failed' unless gif.frameDelay(0) == 5
raise 'total frames bridge failed' unless gif.totalFrames == 20
native.goto_and_play(2)
raise 'current frame bridge failed' unless gif.currentIndex == 2
snap = gif[3]
raise 'frame snapshot bridge failed' unless snap == [:snapshot, 3]
raise 'playback state restore failed' unless native.playing && native.current_frame == 2
gif.update
raise 'native update stopped playback' unless native.playing
first = gif.deanimate
raise 'deanimate returned wrong bitmap' unless first.equal?(native)
raise 'deanimate did not seek first frame' unless native.current_frame == 0 && !native.playing

puts 'RGSS_GIF_COMPAT_SMOKE_PASS'
