begin
  require 'zlib'
  require 'rbconfig'

  archive = ARGV[0]
  exit 64 if archive.nil? || !File.file?(archive)

  scripts = File.open(archive, 'rb') { |f| Marshal.load(f) }
  exit 65 unless scripts.is_a?(Array)

  cfg = defined?(RbConfig) ? RbConfig::CONFIG : Config::CONFIG
  ruby_name = cfg['ruby_install_name'] || 'ruby'
  ruby = ENV['MKXP_PROBE_RUBY']
  ruby = File.join(cfg['bindir'], ruby_name) if ruby.nil? || ruby.empty?

  if ENV['MKXP_PROBE_DEEP'] == '1'
    checked = 0
    scripts.each_with_index do |entry, index|
      next unless entry.is_a?(Array) && entry.length >= 3
      packed = entry[2]
      next unless packed.is_a?(String)
      source = Zlib::Inflate.inflate(packed)
      source = source.split(/^__END__\s*$/, 2)[0]
      temp = "/tmp/mkxp-ruby-probe-#{Process.pid}-#{index}.rb"
      File.open(temp, 'wb') { |out| out.write(source); out.write("\n") }
      ok = system("\"#{ruby}\" -c \"#{temp}\" > /dev/null 2>&1")
      File.delete(temp) rescue nil
      unless ok
        warn("deep probe ruby=#{ruby} section=#{index} name=#{entry[1].inspect} ok=false") if ENV['MKXP_PROBE_DEBUG'] == '1'
        exit 2
      end
      checked += 1
    end
    warn("deep probe ruby=#{ruby} sections=#{checked} ok=true") if ENV['MKXP_PROBE_DEBUG'] == '1'
    exit(checked > 0 ? 0 : 4)
  else
    temp = "/tmp/mkxp-ruby-probe-#{Process.pid}.rb"
    File.open(temp, 'wb') do |out|
      scripts.each do |entry|
        next unless entry.is_a?(Array) && entry.length >= 3
        packed = entry[2]
        next unless packed.is_a?(String)
        source = Zlib::Inflate.inflate(packed)
        source.each_line do |line|
          break if line.chomp == '__END__'
          out.write(line)
        end
        out.write("\n")
      end
    end
    ok = system("\"#{ruby}\" -c \"#{temp}\" > /dev/null 2>&1")
    warn("probe ruby=#{ruby} ok=#{ok.inspect}") if ENV['MKXP_PROBE_DEBUG'] == '1'
    File.delete(temp) rescue nil
    exit(ok ? 0 : 2)
  end
rescue StandardError => e
  warn("probe error #{e.class}: #{e.message}") if ENV['MKXP_PROBE_DEBUG'] == '1'
  File.delete(temp) rescue nil if defined?(temp) && temp
  exit 3
end
