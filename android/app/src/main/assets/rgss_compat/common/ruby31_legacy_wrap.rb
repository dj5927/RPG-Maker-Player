# Ruby 3.x compatibility shims for RPG Maker scripts originally written
# against Ruby 1.8/1.9.  Keep this file syntax-compatible with old Rubies;
# each shim activates only when the selected runtime is modern enough.

if RUBY_VERSION.to_s >= '2.0'
  Fixnum = Integer unless defined?(Fixnum)
  Bignum = Integer unless defined?(Bignum)

  class String
    unless method_defined?(:each)
      def each(&block)
        return enum_for(:each) unless block
        each_line(&block)
      end
    end

    unless method_defined?(:to_a)
      def to_a
        lines.to_a
      end
    end
  end

  class Array
    unless method_defined?(:nitems)
      def nitems
        count { |item| !item.nil? }
      end
    end
  end

  class Object
    unless method_defined?(:=~)
      def =~(other)
        nil
      end
    end
  end

  class File
    class << self
      alias_method :exists?, :exist? unless method_defined?(:exists?)
      unless method_defined?(:__rpgmp_original_open)
        alias_method :__rpgmp_original_open, :open
        def open(path, *args, **kwargs, &block)
          begin
            p = path.to_s
            if p.start_with?("Scripts/") && p.end_with?(".rb")
              stack = System.respond_to?(:native_stack_remaining) ? System.native_stack_remaining : nil
              System.puts("[RPGMP-SCRIPT-OPEN] #{p} stack=#{stack}")
            end
          rescue Exception
          end
          __rpgmp_original_open(path, *args, **kwargs, &block)
        end
      end
      unless method_defined?(:__rpgmp_original_read)
        alias_method :__rpgmp_original_read, :read
        def read(path, *args, **kwargs)
          begin
            p = path.to_s
            System.puts("[RPGMP-SCRIPT-FILE-READ] #{p}") if p.start_with?("Scripts/")
          rescue Exception
          end
          __rpgmp_original_read(path, *args, **kwargs)
        end
      end
    end
  end

  class Dir
    class << self
      alias_method :exists?, :exist? unless method_defined?(:exists?)
    end
  end

  if defined?(Thread) && !Thread.respond_to?(:exclusive)
    class Thread
      @@rpgmp_exclusive_mutex = Mutex.new
      def self.exclusive
        @@rpgmp_exclusive_mutex.synchronize { yield }
      end
    end
  end

  begin
    require 'uri'
    if defined?(URI) && URI.const_defined?(:DEFAULT_PARSER)
      unless URI.respond_to?(:escape)
        def URI.escape(str, unsafe = nil)
          URI::DEFAULT_PARSER.escape(str.to_s, unsafe)
        end
      end
      unless URI.respond_to?(:unescape)
        def URI.unescape(str)
          URI::DEFAULT_PARSER.unescape(str.to_s)
        end
      end
    end
  rescue LoadError
  end

  puts "[preload] RPGMP Ruby 3 legacy compatibility active"
end
