# SteamOS RGSS: make direct Ruby read-only game resource APIs use the same
# case-insensitive PhysFS lookup as Bitmap/Audio. Loaded BEFORE game scripts.
# Never rewrite writes, save files, config files or absolute/system paths.
# Ruby 1.8.7, 1.9.3 and 3.1.3 compatible; no require-time dependencies.

if ENV['MKXP_PORTABLE_RTP_ROOT'] && !ENV['MKXP_PORTABLE_RTP_ROOT'].empty? &&
   defined?(System) && System.respond_to?(:rpgmp_resource_exists?) &&
   !defined?(RPGMPResourceCasefold)

  module RPGMPResourceCasefold
    RESOURCE_ROOTS = %w[graphics audio fonts movies data]

    def self.virtual_path(path)
      return nil unless path.kind_of?(String) && !path.empty? && path.size <= 4096
      return nil if path.include?("\000") || path.include?(':')
      converted = path.tr('\\', '/')
      converted = converted[2, converted.length - 2] while converted[0, 2] == './'
      return nil if converted.nil? || converted.empty? || converted[0, 1] == '/'
      segments = converted.split('/', -1)
      return nil if segments.size < 1 || segments.any? {|s| s.empty? || s == '.' || s == '..'}
      return nil unless RESOURCE_ROOTS.include?(segments[0].downcase)
      converted
    end

    def self.exists?(name)
      path = virtual_path(name)
      path && System.rpgmp_resource_exists?(path) ? true : false
    rescue StandardError
      false
    end

    def self.file?(name)
      path = virtual_path(name)
      path && System.rpgmp_resource_file?(path) ? true : false
    rescue StandardError
      false
    end

    def self.directory?(name)
      path = virtual_path(name)
      path && System.rpgmp_resource_directory?(path) ? true : false
    rescue StandardError
      false
    end

    def self.disk_file(name)
      path = virtual_path(name)
      path ? System.rpgmp_resource_disk_path(path) : nil
    rescue StandardError
      nil
    end

    def self.disk_directory(name)
      path = virtual_path(name)
      path ? System.rpgmp_resource_disk_directory(path) : nil
    rescue StandardError
      nil
    end

    def self.readonly_mode?(mode)
      return true if mode.nil?
      if mode.kind_of?(String)
        return mode[0, 1] == 'r' && mode.index('+').nil?
      end
      if mode.kind_of?(Integer)
        forbidden = File::WRONLY | File::RDWR | File::CREAT |
                    File::TRUNC | File::APPEND
        return (mode & forbidden) == 0
      end
      false
    end

    def self.read_arguments(args)
      return args unless args.size > 0 && args[0].kind_of?(String)
      match = disk_file(args[0])
      return args unless match && match != args[0]
      changed = args.dup
      changed[0] = match
      changed
    end
  end

  class << FileTest
    alias_method :rpgmp_original_exist?, :exist?
    alias_method :rpgmp_original_file?, :file?
    alias_method :rpgmp_original_directory?, :directory?
    def exist?(path)
      rpgmp_original_exist?(path) || RPGMPResourceCasefold.exists?(path)
    end
    alias_method :exists?, :exist?
    def file?(path)
      rpgmp_original_file?(path) || RPGMPResourceCasefold.file?(path)
    end
    def directory?(path)
      rpgmp_original_directory?(path) || RPGMPResourceCasefold.directory?(path)
    end
  end

  class << File
    alias_method :rpgmp_original_exist?, :exist?
    alias_method :rpgmp_original_file?, :file?
    alias_method :rpgmp_original_directory?, :directory?
    alias_method :rpgmp_original_open, :open
    alias_method :rpgmp_original_read, :read
    alias_method :rpgmp_original_readlines, :readlines
    alias_method :rpgmp_original_foreach, :foreach
    alias_method :rpgmp_original_stat, :stat
    alias_method :rpgmp_original_size, :size

    def exist?(path)
      rpgmp_original_exist?(path) || RPGMPResourceCasefold.exists?(path)
    end
    alias_method :exists?, :exist?
    def file?(path)
      rpgmp_original_file?(path) || RPGMPResourceCasefold.file?(path)
    end
    def directory?(path)
      rpgmp_original_directory?(path) || RPGMPResourceCasefold.directory?(path)
    end
    def open(*args, &block)
      if args.size > 0 && RPGMPResourceCasefold.readonly_mode?(args[1])
        args = RPGMPResourceCasefold.read_arguments(args)
      end
      rpgmp_original_open(*args, &block)
    end
    def read(*args)
      rpgmp_original_read(*RPGMPResourceCasefold.read_arguments(args))
    end
    def readlines(*args)
      rpgmp_original_readlines(*RPGMPResourceCasefold.read_arguments(args))
    end
    def foreach(*args, &block)
      rpgmp_original_foreach(*RPGMPResourceCasefold.read_arguments(args), &block)
    end
    def stat(*args)
      rpgmp_original_stat(*RPGMPResourceCasefold.read_arguments(args))
    end
    def size(*args)
      rpgmp_original_size(*RPGMPResourceCasefold.read_arguments(args))
    end

    if File.respond_to?(:binread)
      alias_method :rpgmp_original_binread, :binread
      def binread(*args)
        rpgmp_original_binread(*RPGMPResourceCasefold.read_arguments(args))
      end
    end
  end

  class << Dir
    alias_method :rpgmp_original_entries, :entries
    alias_method :rpgmp_original_foreach, :foreach
    alias_method :rpgmp_original_glob, :glob
    def entries(path, *args)
      virtual = RPGMPResourceCasefold.virtual_path(path)
      if virtual && args.empty? && System.respond_to?(:rpgmp_resource_entries)
        values = System.rpgmp_resource_entries(virtual)
        return values if values
      end
      resolved = RPGMPResourceCasefold.disk_directory(path)
      rpgmp_original_entries(resolved || path, *args)
    end
    def foreach(path, *args, &block)
      virtual = RPGMPResourceCasefold.virtual_path(path)
      if virtual && args.empty? && block && System.respond_to?(:rpgmp_resource_entries)
        values = System.rpgmp_resource_entries(virtual)
        if values
          values.each {|entry| block.call(entry)}
          return nil
        end
      end
      resolved = RPGMPResourceCasefold.disk_directory(path)
      rpgmp_original_foreach(resolved || path, *args, &block)
    end
    def glob(pattern, *args, &block)
      virtual = RPGMPResourceCasefold.virtual_path(pattern)
      if virtual && virtual.index('{').nil? && virtual.index('}').nil?
        slash = virtual.rindex('/')
        if slash
          folder = virtual[0, slash]
          wildcard = virtual[slash + 1, virtual.size]
          if wildcard && wildcard =~ /[*?\[]/ && folder !~ /[*?\[\]]/
            resolved = RPGMPResourceCasefold.disk_directory(folder)
            if resolved
              flags = args[0].kind_of?(Integer) ? args[0] : 0
              found = entries(folder).select do |entry|
                entry != '.' && entry != '..' &&
                  File.fnmatch?(wildcard.downcase, entry.downcase, flags)
              end.sort.map {|entry| folder + '/' + entry}
              if block
                found.each {|entry| block.call(entry)}
                return nil
              end
              return found
            end
          end
        end
      end
      rpgmp_original_glob(pattern, *args, &block)
    end
  end

  if defined?(System) && System.respond_to?(:puts)
    System.puts('[RPGMP-CASEFOLD] installed read-only RGSS resource bridge for ' + RUBY_VERSION)
  end
end
