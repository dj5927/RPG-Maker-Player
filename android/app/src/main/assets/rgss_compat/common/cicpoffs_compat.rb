# encoding: UTF-8
# Lightweight Ruby 1.8/1.9/3.1 compatible bridge for the useful
# rpgmakermlinux-cicpoffs / Kawariki RGSS plugin patches.

module Preload
  # The Android native VM evaluates this script with runCustomScript.
  # Use the absolute file path from Java, rather than assuming __FILE__
  # always refers to the installed copy in all Ruby 1.8 builds.
  AndroidCompatFile = ENV['RPGMP_CICPOFFS_COMPAT'] unless const_defined?(:AndroidCompatFile)
  SourceFile = (AndroidCompatFile && File.file?(AndroidCompatFile)) ?
    AndroidCompatFile : __FILE__ unless const_defined?(:SourceFile)
  Root = File.expand_path(File.join(File.dirname(SourceFile), '..', 'cicpoffs')) unless const_defined?(:Root)

  def self.print(text)
    STDOUT.puts('[cicpoffs] ' + text.to_s)
  rescue
  end

  def self.require(name)
    return true if name.to_s == 'PreloadIni.rb' && const_defined?(:Ini)
    path = File.join(Root, 'libs', name.to_s)
    Kernel.load(path) if File.file?(path)
    true
  end

  module Ini
    def self.read_all(filename)
      result = {}
      current = ''
      return result unless File.file?(filename.to_s)
      File.open(filename.to_s, 'rb') do |f|
        f.each_line do |line|
          text = line.to_s.strip
          next if text.empty? || text[0, 1] == ';' || text[0, 1] == '#'
          if text =~ /^\[(.*)\]$/
            current = $1.to_s
            result[current.downcase] ||= {}
          elsif text =~ /^([^=]+)=(.*)$/
            result[current.downcase] ||= {}
            result[current.downcase][$1.to_s.strip.downcase] = $2.to_s.strip
          end
        end
      end
      result
    rescue
      {}
    end

    def self.readIniString(filename, section, key)
      all = read_all(filename)
      sec = all[section.to_s.downcase]
      sec ? sec[key.to_s.downcase] : nil
    end

    def self.writeIniString(filename, section, key, value)
      all = read_all(filename)
      secname = section.to_s
      keyname = key.to_s
      all[secname.downcase] ||= {}
      all[secname.downcase][keyname.downcase] = value.to_s
      File.open(filename.to_s, 'wb') do |f|
        all.keys.sort.each do |sec|
          f.write("[#{sec}]\r\n") unless sec.empty?
          all[sec].keys.sort.each do |k|
            f.write("#{k}=#{all[sec][k]}\r\n")
          end
          f.write("\r\n")
        end
      end
      1
    rescue
      0
    end
  end

  class CompatContext
    def initialize(scripts, rgss_version)
      @scripts = scripts
      @rgss_version = rgss_version
      @flags = {}
    end
    def [](key)
      return @rgss_version if key == :rgss_version
      @flags[key]
    end
    def flag?(key); !!@flags[key]; end
    def mark(*keys); keys.each { |k| @flags[k] = true }; end
    def each_script
      i = 0
      while i < @scripts.size
        yield CompatScript.new(self, i, @scripts[i])
        i += 1
      end
    end
  end

  class CompatScript
    attr_reader :context, :index
    def initialize(context, index, entry)
      @context, @index, @entry = context, index, entry
      @imported_checked = false
      @imported_key = nil
    end
    def name; (@entry && @entry[1] ? @entry[1] : '').to_s; end
    def source; (@entry && @entry[3] ? @entry[3] : '').to_s; end
    def source=(value); @entry[3] = value.to_s if @entry; end
    def loc; "##{@index} '#{name}'"; end
    def remove; self.source = ''; end
    def load_file(path); self.source = File.open(path, 'rb') { |f| f.read }; end
    def imported_key
      return @imported_key if @imported_checked
      @imported_checked = true
      src = source
      m = src.match(/\$imported\s*(?:\|\|=\s*\{\s*\})?\s*\[\s*(:[A-Za-z0-9_]+|['\"][^'\"]+['\"])\s*\]\s*=/)
      if m
        raw = m[1]
        @imported_key = raw[0,1] == ':' ? raw[1..-1].to_sym : raw[1...-1]
      end
      @imported_key
    rescue
      nil
    end
  end

  class Patch
    attr_reader :description
    def initialize(description = nil)
      @description = description.to_s
      @conditions = []
      @actions = []
      @terminal = false
    end
    def if?(&block); @conditions << block; self; end
    def include?(text); if? { |s| s.source.include?(text.to_s) }; end
    def match?(*patterns)
      re = Regexp.union(*patterns)
      if? { |s| !!(s.source =~ re) }
    end
    def imported?(key)
      if key.nil?
        if? { |s| s.imported_key.nil? }
      else
        if? { |s| s.imported_key.to_s == key.to_s }
      end
    end
    def flag?(flag); if? { |s| s.context.flag?(flag) }; end
    def then!(&block); @actions << block; self; end
    def sub!(pattern, replacement = nil, &block)
      if block
        @actions << proc { |s| s.source = s.source.gsub(pattern, &block) }
      else
        @actions << proc { |s| s.source = s.source.gsub(pattern, replacement) }
      end
      self
    end
    def flag!(*flags); @actions << proc { |s| s.context.mark(*flags) }; self; end
    def remove!; @actions << proc { |s| s.remove }; @terminal = true; self; end
    def replace!(filename)
      @actions << proc { |s| s.load_file(File.join(Preload::Root, 'ports', filename.to_s)) }
      @terminal = true
      self
    end
    def next!; @terminal = true; self; end
    def applicable?(script)
      @conditions.each { |c| return false unless c.call(script) }
      true
    rescue
      false
    end
    def apply(script)
      return false unless applicable?(script)
      Preload.print("Patch #{script.loc}: #{@description}")
      @actions.each { |a| a.call(script) }
      @terminal
    rescue Exception => e
      Preload.print("Patch failed #{@description}: #{e.class}: #{e}")
      false
    end
  end

  def self.detect_rgss_version
    named = {'XP'=>1, 'VX'=>2, 'VXACE'=>3}[ENV['RPGMP_RGSS_ENGINE']]
    return named if named
    begin
      if defined?(CFG) && CFG.respond_to?(:[]) && CFG['rgssVersion']
        v = CFG['rgssVersion'].to_i
        return v if v >= 1 && v <= 3
      end
    rescue
    end
    begin
      root = ENV['RPGMP_RGSS_GAME_ROOT']
      path = root && File.file?(File.join(root,'Game.ini')) ?
        File.join(root,'Game.ini') : 'Game.ini'
      ini = File.open(path, 'rb') { |f| f.read }
      return 3 if ini.include?('.rvdata2')
      return 2 if ini.include?('.rvdata')
      return 1 if ini.include?('.rxdata')
    rescue
    end
    3
  end

  def self.mouse_mode
    path = File.expand_path(File.join(File.dirname(__FILE__), '..', 'mouse_mode.txt'))
    mode = File.file?(path) ? File.open(path, 'rb') { |f| f.read }.to_s.strip.upcase : 'AUTO'
    %w[AUTO PATCH PASS ORIGINAL].include?(mode) ? mode : 'AUTO'
  rescue
    'AUTO'
  end

  def self.mouse_script?(script)
    name = script.name.to_s
    src = script.source.to_s
    return true if src.include?('SUPER SIMPLE MOUSE SCRIPT')
    return true if src.include?('include IBasicMouse')
    return true if legacy_mouse_extension?(script)
    return true if name =~ /mouse/i && src =~ /Win32API\.new|GetCursorPos|ScreenToClient|GetAsyncKeyState|ShowCursor/i
    return true if name =~ /mouse/i && src =~ /module\s+Mouse|class\s+Mouse/i
    false
  rescue
    false
  end

  def self.legacy_mouse_extension?(script)
    name = script.name.to_s
    src = script.source.to_s
    return true if name.strip =~ /\Amouse\s*[23]\z/i
    return false unless name =~ /mouse/i || src.include?('sephlamchop_mousesys_')
    return true if src.include?('sephlamchop_mousesys_gmplyr_update') &&
                   (src.include?('clear_path') || src.include?('find_path'))
    return true if src.include?('sephlamchop_mousesys_scnmap_update') &&
                   (src.include?('Mouse.grid') || src.include?('$mouse_sprite'))
    false
  rescue
    false
  end

  def self.risky_android_mouse_script?(script)
    name = script.name.to_s
    src = script.source.to_s
    return true if legacy_mouse_extension?(script)
    return false unless name =~ /mouse/i
    !!(src =~ /Win32API\.new/i && src =~ /GetCursorPos|ScreenToClient|GetAsyncKeyState|ShowCursor|SetCursorPos/i)
  rescue
    false
  end

  def self.dldb_cheat_script?(script)
    name = script.name.to_s
    src = script.source.to_s
    compact_name = ascii_compact(name)
    return true if compact_name.include?('rpgvxacecheatsystem')
    return true if src.include?('Dldb_Setting') &&
                   (name =~ /cheat|dldb/i || src =~ /dldb\.info|blog\/506|Dldb_/i)
    false
  rescue
    false
  end

  def self.rewrite_top_level_module_include!(script)
    src = script.source.to_s
    first_code = nil
    src.each_line do |line|
      next if line =~ /^\s*(?:#|$)/
      first_code = line.strip
      break
    end
    return false unless first_code =~ /^include\s+([A-Z][A-Za-z0-9_:]*)\s*$/
    mod = $1.to_s
    replaced = false
    script.source = src.sub(/^include\s+[A-Z][A-Za-z0-9_:]*\s*$/) do
      replaced = true
      "Object.send(:include, #{mod})"
    end
    replaced
  rescue
    false
  end

  def self.ascii_compact(text)
    out = ''
    text.to_s.each_byte do |b|
      if b >= 65 && b <= 90
        out << (b + 32).chr
      elsif (b >= 97 && b <= 122) || (b >= 48 && b <= 57)
        out << b.chr
      end
    end
    out
  rescue
    ''
  end

  def self.klein_bitmap_wrapper?(script)
    name = ascii_compact(script.name)
    return true if name == 'kleinbitmapfunctions'
    src = script.source.to_s
    return false unless src.include?('KleinBitmap.dll')
    !!(src =~ /scroll_up|scroll_down|to_negative|to_retro|add_outline|klein_bitmap_version/i)
  rescue
    false
  end

  def self.legacy_giflibrary_wrapper?(script)
    src = script.source.to_s
    return false unless src.include?('module GifLibrary')
    return false unless src.include?('class GifBitmap')
    return false unless src.include?('GifToPngFilesInMemory')
    !!(src =~ /gif\.dll/i)
  rescue
    false
  end

  def self.patch_native_giflibrary!(script)
    src = script.source.to_s
    return false if src.include?('RPGMP_NATIVE_GIF_BRIDGE')
    patched = src.gsub(/if\s+safeExists\?\(\s*(["'])gif\.dll\1\s*\)(?:\s*&&\s*!\$MKXP)?/i,
                       'if false # RPGMP_NATIVE_GIF_DLL_BYPASS')
    return false if patched == src
    bridge = File.join(Root, 'ports', 'gif_native_bridge.rb')
    return false unless File.file?(bridge)
    bridge_src = File.open(bridge, 'rb') { |f| f.read }
    script.source = patched + "\n\n# RPGMP_NATIVE_GIF_BRIDGE\n" + bridge_src
    true
  rescue Exception => e
    Preload.print("GIF native bridge patch failed #{script.loc}: #{e.class}: #{e}")
    false
  end

  def self.freebird_language_scene?
    return false unless defined?($RGSS_SCRIPTS) && $RGSS_SCRIPTS.respond_to?(:each)
    $RGSS_SCRIPTS.each do |entry|
      name = (entry && entry[1] ? entry[1] : '').to_s
      src = (entry && entry[3] ? entry[3] : '').to_s
      return true if name =~ /Scene_Language/i
      return true if src.include?('class Scene_Language')
    end
    false
  rescue
    false
  end

  def self.patch_freebird_selectable_visible!(script)
    return false unless script.name.to_s =~ /Window_Selectable/i
    src = script.source.to_s
    return false if src.include?('RPGMP_FREEBIRD_NIL_VISIBLE')
    changed = false
    patched = src.gsub(/^([ \t]*)(@[A-Za-z_][A-Za-z0-9_]*)\.visible[ \t]*=[ \t]*(.+)$/) do
      indent = $1
      receiver = $2
      rhs = $3
      changed = true
      "#{indent}if #{receiver} # RPGMP_FREEBIRD_NIL_VISIBLE\n" +
        "#{indent}  #{receiver}.visible = #{rhs}\n" +
        "#{indent}end"
    end
    script.source = patched if changed
    changed
  rescue Exception => e
    Preload.print("FREEBIRD NIL VISIBLE patch failed #{script.loc}: #{e.class}: #{e}")
    false
  end

  def self.install_freebird_nil_visible_guard!
    return if NilClass.method_defined?(:rpgmp_freebird_visible=)
    NilClass.class_eval do
      def rpgmp_freebird_visible=(value)
        value
      end
      alias_method :visible=, :rpgmp_freebird_visible= unless method_defined?(:visible=)
    end
    Preload.print("FREEBIRD NIL visible= guard active")
  rescue Exception => e
    Preload.print("FREEBIRD NIL visible= guard failed: #{e.class}: #{e}")
  end

  def self.apply_cicpoffs_compat
    return unless defined?($RGSS_SCRIPTS) && $RGSS_SCRIPTS.respond_to?(:each)
    libs = File.join(Root, 'libs')
    $LOAD_PATH.unshift(libs) unless $LOAD_PATH.include?(libs)
    patches_file = File.join(Root, 'patches.rb')
    return unless File.file?(patches_file)
    rgss = detect_rgss_version
    ENV['vcode'] = [nil, ':xp', ':vx', ':vxace'][rgss].to_s
    if rgss < 3 && RUBY_VERSION.to_s >= '2.0' && !Integer.method_defined?(:to_a)
      Integer.class_eval do
        def to_a
          [self]
        end
      end
      Preload.print("RPGMP_LEGACY_INTEGER_TO_A active")
    end
    remove_const(:Patches) if const_defined?(:Patches)
    Kernel.load(patches_file)

    denied = {
      'test subscribe' => true,
      "HimeWorks' Simple Audio Encryption: Re-Implement with direct path detection" => true,
      'wfcrypt' => true,
      'Vitaminpl fix' => true
    }
    patches = Patches.reject { |p| denied[p.description] }
    mode = mouse_mode
    if mode == 'ORIGINAL'
      patches = patches.reject { |p| p.description.to_s =~ /mouse/i }
    end
    ctx = CompatContext.new($RGSS_SCRIPTS, rgss)
    ctx.mark(:no_font_effects) if ENV['KAWARIKI_MKXP_NO_FONT_EFFECTS'] == '1'
    freebird_language = freebird_language_scene?
    install_freebird_nil_visible_guard! if freebird_language

    # Critical Android compatibility pre-pass. Do this before the general
    # Kawariki patch loop so a malformed/oddly encoded earlier script cannot
    # prevent a later known-bad mouse/cheat section from being neutralized.
    ctx.each_script do |script|
      if rewrite_top_level_module_include!(script)
        Preload.print("COMMON TOPLEVEL INCLUDE PREPASS #{script.loc}")
      end

      if klein_bitmap_wrapper?(script)
        Preload.print("KLEIN BITMAP PREPASS native compat #{script.loc}")
        script.remove
        next
      end

      if legacy_giflibrary_wrapper?(script) && patch_native_giflibrary!(script)
        Preload.print("GIFLIBRARY PREPASS native mkxp-z bridge #{script.loc}")
      end

      if freebird_language && patch_freebird_selectable_visible!(script)
        Preload.print("FREEBIRD NIL VISIBLE PREPASS #{script.loc}")
      end

      if dldb_cheat_script?(script)
        Preload.print("DLDB CHEAT PREPASS #{script.loc}")
        script.remove
        next
      end

      if mode == 'PASS' && mouse_script?(script)
        Preload.print("Mouse PREPASS PASS #{script.loc}")
        script.load_file(File.join(Root, 'ports', 'mouse_skip.rb'))
        next
      elsif mode == 'AUTO' && legacy_mouse_extension?(script)
        Preload.print("Mouse PREPASS AUTO BRIDGE legacy extension #{script.loc}")
        script.load_file(File.join(Root, 'ports', 'mouse_skip.rb'))
        next
      elsif mode == 'AUTO' && risky_android_mouse_script?(script)
        Preload.print("Mouse PREPASS AUTO PASS #{script.loc}")
        script.load_file(File.join(Root, 'ports', 'mouse_skip.rb'))
        next
      end
    end

    ctx.each_script do |script|
      begin
        src = script.source
        next if src.nil? || src.empty?
        script.source = src.gsub(".encode('SHIFT_JIS')", '') if src.include?(".encode('SHIFT_JIS')")

        patches.each do |patch|
          break if patch.apply(script)
        end
      rescue Exception => e
        Preload.print("Script patch skipped #{script.loc}: #{e.class}: #{e}")
      end
    end
    Preload.print("mouse mode=#{mode}")
    Preload.print("compat active rgss=#{rgss} patches=#{patches.size}")
  rescue Exception => e
    Preload.print("compat loader failed: #{e.class}: #{e}")
  end
end

Preload.apply_cicpoffs_compat

# Optional per-game RGSS JSON patches: execute after existing compatibility
# passes, before the mkxp-z VM evals the decompressed script sections.
begin
  rpgmp_adapter = File.join(File.dirname(Preload::SourceFile), 'rpgmp_rgss_patches.rb')
  if File.file?(rpgmp_adapter)
    Kernel.load(rpgmp_adapter)
  else
    path = ENV['RPGMP_PATCH_LOG']
    if path && !path.empty?
      File.open(path, 'ab') {|f| f.write('[RPGMP-RGSS-PATCH] adapter file missing ' + rpgmp_adapter + "\n")}
    end
  end
rescue Exception => rpgmp_patch_error
  message = '[RPGMP-RGSS-PATCH] loader skipped ' +
            rpgmp_patch_error.class.to_s + ': ' + rpgmp_patch_error.to_s
  STDOUT.puts(message)
  begin
    path = ENV['RPGMP_PATCH_LOG']
    File.open(path, 'ab') {|f| f.write(message + "\n")} if path && !path.empty?
  rescue Exception
  end
end
