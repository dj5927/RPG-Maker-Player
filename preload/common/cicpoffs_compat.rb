# encoding: UTF-8
# Lightweight Ruby 1.8/1.9/3.1 compatible bridge for the useful
# rpgmakermlinux-cicpoffs / Kawariki RGSS plugin patches.

module Preload
  Root = File.expand_path(File.join(File.dirname(__FILE__), '..', 'cicpoffs')) unless const_defined?(:Root)

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
    begin
      if defined?(CFG) && CFG.respond_to?(:[]) && CFG['rgssVersion']
        v = CFG['rgssVersion'].to_i
        return v if v >= 1 && v <= 3
      end
    rescue
    end
    begin
      ini = File.open('Game.ini', 'rb') { |f| f.read }
      return 3 if ini.include?('.rvdata2')
      return 2 if ini.include?('.rvdata')
      return 1 if ini.include?('.rxdata')
    rescue
    end
    3
  end

  def self.apply_cicpoffs_compat
    return unless defined?($RGSS_SCRIPTS) && $RGSS_SCRIPTS.respond_to?(:each)
    libs = File.join(Root, 'libs')
    $LOAD_PATH.unshift(libs) unless $LOAD_PATH.include?(libs)
    patches_file = File.join(Root, 'patches.rb')
    return unless File.file?(patches_file)
    rgss = detect_rgss_version
    ENV['vcode'] = [nil, ':xp', ':vx', ':vxace'][rgss].to_s
    remove_const(:Patches) if const_defined?(:Patches)
    Kernel.load(patches_file)

    denied = {
      'test subscribe' => true,
      "HimeWorks' Simple Audio Encryption: Re-Implement with direct path detection" => true,
      'wfcrypt' => true,
      'Vitaminpl fix' => true
    }
    patches = Patches.reject { |p| denied[p.description] }
    ctx = CompatContext.new($RGSS_SCRIPTS, rgss)
    ctx.mark(:no_font_effects) if ENV['KAWARIKI_MKXP_NO_FONT_EFFECTS'] == '1'

    ctx.each_script do |script|
      src = script.source
      script.source = src.gsub(".encode('SHIFT_JIS')", '') if src.include?(".encode('SHIFT_JIS')")
      patches.each do |patch|
        break if patch.apply(script)
      end
    end
    Preload.print("compat active rgss=#{rgss} patches=#{patches.size}")
  rescue Exception => e
    Preload.print("compat loader failed: #{e.class}: #{e}")
  end
end

Preload.apply_cicpoffs_compat
