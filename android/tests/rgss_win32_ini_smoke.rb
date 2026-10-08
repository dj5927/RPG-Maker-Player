require 'tmpdir'

module Graphics
  def self.update; end
end

module Input
  MOUSELEFT = 1
  MOUSERIGHT = 2
  MOUSEMIDDLE = 3
  def self.raw_key_states; Array.new(256, false); end
  def self.press?(*args); false; end
end

module System
  def self.is_windows?; false; end
  def self.puts(*args); end
end

class Win32API
  def initialize(*args); end
  def call(*args); 0; end
end

wrap = File.expand_path('../app/src/main/assets/rgss_compat/common/win32_wrap.rb', __dir__)
load wrap

Dir.mktmpdir do |dir|
  path = File.join(dir, 'ToTheMoon.ini')
  writer = Win32API_Impl::Kernel32::WritePrivateProfileString.new
  reader = Win32API_Impl::Kernel32::GetPrivateProfileString.new

  raise 'initial write failed' unless writer.call(['Language', 'Current', 'German', path]) == 1
  raise 'ini file not created' unless File.file?(path)

  buf = "\0" * 64
  reader.call(['Language', 'Current', 'English', buf, 64, path])
  value = buf.split("\0", 2)[0]
  raise "unexpected read #{value.inspect}" unless value == 'German'

  raise 'update write failed' unless writer.call(['Language', 'Current', 'Korean', path]) == 1
  buf = "\0" * 64
  reader.call(['Language', 'Current', 'English', buf, 64, path])
  value = buf.split("\0", 2)[0]
  raise "unexpected updated read #{value.inspect}" unless value == 'Korean'
end

puts 'RGSS_WIN32_INI_SMOKE_PASS'
