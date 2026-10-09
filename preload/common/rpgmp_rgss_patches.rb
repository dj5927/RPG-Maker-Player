# encoding: UTF-8
# Per-game RGSS JSON source patching before the mkxp-z eval loop.
# Compatible with Ruby 1.8 / 1.9 / 3.1; never modifies Scripts.*data.
module RPGMP_RgssPatches
  MAX_BYTES = 4194304

  # A small self-contained JSON reader. No json gem / native JSON extension
  # dependency, since the Android Ruby 1.8/1.9 load paths can differ.
  class JSONReader
    def initialize(src)
      @s, @i = src, 0
    end
    def parse
      v = value(0)
      ws
      raise 'trailing JSON' unless @i == @s.size
      v
    end
    def ws
      @i += 1 while @i < @s.size && " \t\r\n".include?(@s[@i, 1])
    end
    def take(ch)
      ws
      raise "expected #{ch}" unless @s[@i, 1] == ch
      @i += 1
    end
    def string
      take('"')
      out = ''
      while @i < @s.size
        c = @s[@i, 1]
        @i += 1
        return out if c == '"'
        raise 'control character in JSON string' if c.unpack('C')[0] < 32
        if c != '\\'
          out << c
          next
        end
        e = @s[@i, 1]
        @i += 1
        case e
        when '"', '\\', '/'
          out << e
        when 'b'; out << "\b"
        when 'f'; out << "\f"
        when 'n'; out << "\n"
        when 'r'; out << "\r"
        when 't'; out << "\t"
        when 'u'
          hex = @s[@i, 4]
          raise 'invalid unicode escape' unless hex && hex =~ /\A[0-9a-fA-F]{4}\z/
          code = hex.to_i(16)
          @i += 4
          if code >= 0xd800 && code <= 0xdbff
            raise 'invalid surrogate pair' unless @s[@i, 2] == '\\u'
            @i += 2
            low = @s[@i, 4]
            raise 'invalid low surrogate' unless low && low =~ /\A[0-9a-fA-F]{4}\z/
            @i += 4
            low = low.to_i(16)
            raise 'invalid low surrogate' unless low >= 0xdc00 && low <= 0xdfff
            code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00
          elsif code >= 0xdc00 && code <= 0xdfff
            raise 'lone low surrogate'
          end
          out << [code].pack('U')
        else
          raise 'invalid JSON escape'
        end
      end
      raise 'unterminated JSON string'
    end
    def value(depth)
      raise 'JSON nested too deeply' if depth > 20
      ws
      c = @s[@i, 1]
      if c == '"'
        string
      elsif c == '{'
        take('{')
        object = {}
        ws
        if @s[@i, 1] != '}'
          loop do
            name = string
            raise 'duplicate JSON key' if object.has_key?(name)
            take(':')
            object[name] = value(depth + 1)
            ws
            break unless @s[@i, 1] == ','
            @i += 1
          end
        end
        take('}')
        object
      elsif c == '['
        take('[')
        ary = []
        ws
        if @s[@i, 1] != ']'
          loop do
            ary << value(depth + 1)
            ws
            break unless @s[@i, 1] == ','
            @i += 1
          end
        end
        take(']')
        ary
      elsif @s[@i, 4] == 'true'
        @i += 4
        true
      elsif @s[@i, 5] == 'false'
        @i += 5
        false
      elsif @s[@i, 4] == 'null'
        @i += 4
        nil
      else
        rest = @s[@i..-1]
        m = /\A-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?/.match(rest)
        raise 'invalid JSON value' unless m
        token = m[0]
        @i += token.size
        token =~ /[.eE]/ ? token.to_f : token.to_i
      end
    end
  end

  def self.log(message)
    STDOUT.puts('[RPGMP-RGSS-PATCH] ' + message.to_s)
  rescue Exception
  end

  def self.game_root
    env = ENV['RPGMP_RGSS_GAME_ROOT']
    return env if env && !env.empty? && File.directory?(env)
    if defined?(CFG) && CFG.respond_to?(:[])
      begin
        folder = CFG['gameFolder']
        return folder if folder && File.directory?(folder.to_s)
      rescue Exception
      end
    end
    Dir.pwd
  end

  def self.data_file_fingerprint(basename)
    data_dir = File.join(game_root, 'Data')
    return nil unless File.directory?(data_dir)
    path = File.join(data_dir, basename)
    unless File.file?(path)
      entry = Dir.entries(data_dir).find {|n| n.downcase == basename.downcase}
      return nil unless entry
      path = File.join(data_dir, entry)
    end
    begin
      require 'digest/sha2'
      digest = Digest::SHA256.new
      File.open(path, 'rb') do |file|
        while (block = file.read(65536))
          digest.update(block)
        end
      end
      digest.hexdigest
    rescue LoadError
      log('SHA-256 unavailable, registry not applied')
      nil
    end
  end

  # Match exact game CONTENT, never the install directory or display title.
  # Each registry entry may accept multiple original script archive hashes
  # for game updates or translation variants.
  def self.manifest_for_current_game
    central = ENV['RPGMP_PATCH_DB']
    if !central || central.empty?
      # SteamOS preload resides under <player-root>/preload/common.
      # Android sets RPGMP_PATCH_DB from the selected mkxp library root.
      central = File.join(File.dirname(File.dirname(File.dirname(__FILE__))),
                          '_compat', 'patches.json')
    end
    if File.file?(central)
      raise 'central registry too large' if File.size(central) > MAX_BYTES
      bytes = File.open(central, 'rb') {|f| f.read}
      bytes = bytes[3..-1] if bytes[0, 3] == "\xef\xbb\xbf"
      data = JSONReader.new(bytes).parse
      raise 'central schema=2 required' unless data.kind_of?(Hash) && data['schema'] == 2
      if data['enabled'] == true
        games = data['games']
        raise 'games must be object' unless games.kind_of?(Hash)
        script_file = {'XP'=>'Scripts.rxdata','VX'=>'Scripts.rvdata',
                       'VXACE'=>'Scripts.rvdata2'}[engine_name]
        if script_file
          fingerprints = {}
          required = [script_file, 'System.' + script_file.split('.').last]
          required.each {|filename| fingerprints[filename] = data_file_fingerprint(filename)}
          content_ready = required.all? {|filename| fingerprints[filename]}
          if content_ready
          matches = []
          games.each do |id, row|
            next unless row.kind_of?(Hash) && row['engine'] == engine_name
            variants = row['fingerprints']
            next unless variants.kind_of?(Array) && variants.size > 0 && variants.size <= 32
            variants.each do |variant|
              next unless variant.kind_of?(Hash) && variant.size >= 2 && variant.size <= 8
              next unless required.all? {|filename| variant.has_key?('Data/' + filename)}
              good = true
              variant.each do |file, expected|
                if !file.kind_of?(String) || !file.start_with?('Data/') ||
                    !['Scripts', 'System', 'MapInfos'].include?(File.basename(file).split('.')[0]) ||
                    !file.end_with?('.' + script_file.split('.').last) ||
                    !expected.kind_of?(String) || !(expected =~ /\A[0-9a-fA-F]{64}\z/)
                  good = false
                  break
                end
                name = File.basename(file)
                fingerprints[name] ||= data_file_fingerprint(name)
                if !fingerprints[name] || fingerprints[name] != expected.downcase
                  good = false
                  break
                end
              end
              if good
                matches << [id,row]
                break
              end
            end
          end
          if matches.size > 1
            log('ambiguous fingerprint in registry; skipped')
            return nil
          end
          if matches.size == 1
            id, selected = matches[0]
            log('central content match id=' + id + ' engine=' + engine_name)
            return selected
          end
          log('central no fingerprint match engine=' + engine_name)
          end
        end
      else
        log('central registry disabled')
      end
    end
    # Legacy game-specific manifests remain supported. A matching entry in
    # the central registry always takes precedence, even when disabled.
    local = File.join(game_root, 'rpgmp-patches.json')
    return nil unless File.file?(local)
    raise 'local manifest too large' if File.size(local) > 262144
    bytes = File.open(local, 'rb') {|f| f.read}
    bytes = bytes[3..-1] if bytes[0, 3] == "\xef\xbb\xbf"
    JSONReader.new(bytes).parse
  end

  def self.engine_name
    v = if defined?(Preload) && Preload.respond_to?(:detect_rgss_version)
          Preload.detect_rgss_version
        else
          nil
        end
    {1 => 'XP', 2 => 'VX', 3 => 'VXACE'}[v]
  end

  def self.sha256(data)
    require 'digest/sha2'
    Digest::SHA256.hexdigest(data)
  rescue LoadError
    nil
  end

  def self.apply
    return unless defined?($RGSS_SCRIPTS) && $RGSS_SCRIPTS.kind_of?(Array)
    data = manifest_for_current_game
    return unless data
    raise 'patch data invalid' unless data.kind_of?(Hash)
    raise 'local schema=1 required' if data.has_key?('schema') && data['schema'] != 1
    return log('manifest disabled') unless data['enabled'] == true
    entries = data['patches']
    raise 'invalid patches array' unless entries.kind_of?(Array) && entries.size <= 100
    engine = engine_name
    return log('RGSS engine unknown; skipped') unless engine
    # Validate ALL rules before changing any source; a malformed rule cannot
    # leave the in-memory script array half-patched.
    ids = {}
    active = []
    entries.each do |p|
      raise 'patch must be object' unless p.kind_of?(Hash)
      id = p['id']
      raise 'invalid or duplicate id' unless id.kind_of?(String) &&
        id =~ /\A[A-Za-z0-9._-]{1,70}\z/ && !ids[id]
      ids[id] = true
      target = p['engine']
      raise 'invalid engine' unless ['XP', 'VX', 'VXACE'].include?(target)
      if p.has_key?('enabled')
        raise 'invalid enabled flag' unless p['enabled'] == true || p['enabled'] == false
      end
      next if p['enabled'] == false || target != engine
      name = p['section']
      index = p['section_index']
      raise 'must select section by name or index' unless
        (name.kind_of?(String) && !name.empty? && name.size <= 150) ||
        (index.kind_of?(Integer) && index >= 0 && index < $RGSS_SCRIPTS.size)
      if p.has_key?('section_index') && !(index.kind_of?(Integer) && index >= 0 && index < $RGSS_SCRIPTS.size)
        raise 'invalid section_index'
      end
      find, replace = p['find'], p['replace']
      raise 'invalid find/replace' unless find.kind_of?(String) && !find.empty? &&
        find.size <= 8192 && replace.kind_of?(String) && replace.size <= 32768
      raise 'expected_matches must equal 1' unless !p.has_key?('expected_matches') ||
        (p['expected_matches'].kind_of?(Integer) && p['expected_matches'] == 1)
      hash = p['sha256_before']
      raise 'invalid hash' if hash && !(hash.kind_of?(String) && hash =~ /\A[0-9a-fA-F]{64}\z/)
      ruby = p['ruby']
      raise 'invalid Ruby version' if ruby && !['1.8', '1.9', '3.1'].include?(ruby)
      next if ruby && RUBY_VERSION[0, 3] != ruby
      active << p
    end

    originals = {}
    staged = {}
    active.each do |p|
      candidates = []
      $RGSS_SCRIPTS.each_with_index do |section, i|
        next unless section.kind_of?(Array) && section.size > 3 && section[3].kind_of?(String)
        next if p.has_key?('section_index') && i != p['section_index']
        next if p['section'] && section[1].to_s != p['section']
        candidates << i
      end
      if candidates.size != 1
        log("section-skip id=#{p['id']} matches=#{candidates.size}")
        next
      end
      idx = candidates[0]
      originals[idx] ||= $RGSS_SCRIPTS[idx][3]
      source = staged.has_key?(idx) ? staged[idx] : originals[idx]
      if p['sha256_before']
        digest = sha256(originals[idx])
        if !digest || digest.downcase != p['sha256_before'].downcase
          log("hash-skip id=#{p['id']}")
          next
        end
      end
      count = 0
      position = 0
      while (found = source.index(p['find'], position))
        count += 1
        break if count > 1
        position = found + p['find'].size
      end
      if count != 1
        log("match-skip id=#{p['id']} count=#{count}")
        next
      end
      # Literal replacement: String#sub(replacement) would interpret Ruby
      # backrefs such as \\1, which is unsafe for arbitrary script sources.
      staged[idx] = source.sub(p['find']) { p['replace'] }
      log("applied id=#{p['id']} section=#{idx}")
    end
    staged.each do |idx, source|
      $RGSS_SCRIPTS[idx][3] = source
    end
    log("ready engine=#{engine} ruby=#{RUBY_VERSION} sections=#{staged.size}")
  rescue Exception => error
    log("rejected #{error.class}: #{error.message}")
  end
end

RPGMP_RgssPatches.apply
