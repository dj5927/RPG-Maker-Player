# fix_volume.rb (preload용)
# HZM_VXA::Ini가 class/module 어느 쪽이든 충돌 없이 동작
#
# preload이므로 게임 스크립트보다 먼저 실행됨
# → HZM_VXA::Ini를 class로 미리 정의해두면
#   게임 스크립트가 같은 class를 열어서 내용 추가 가능 (충돌 없음)

INI_FILENAME_FV = "./Game.ini"

def fv_ini_load(section, key, default = 100)
  return default unless File.exist?(INI_FILENAME_FV)
  current_section = nil
  File.foreach(INI_FILENAME_FV) do |line|
    line = line.strip
    if line =~ /^\[(.+)\]$/
      current_section = $1
    elsif current_section == section && line =~ /^([^=]+)=(.*)$/
      if $1.strip == key
        val = $2.strip.to_i
        return (val <= 0 || val > 100) ? default : val
      end
    end
  end
  return default
rescue
  return default
end

def fv_ini_save(section, key, value)
  lines = File.exist?(INI_FILENAME_FV) ? File.readlines(INI_FILENAME_FV) : []
  new_lines = []
  section_found = false
  key_found = false
  current_section = nil
  lines.each do |line|
    stripped = line.strip
    if stripped =~ /^\[(.+)\]$/
      current_section = $1
      if current_section == section
        section_found = true
      elsif section_found && !key_found
        new_lines << "#{key}=#{value}\n"
        key_found = true
      end
    elsif current_section == section && stripped =~ /^#{Regexp.escape(key)}\s*=/
      line = "#{key}=#{value}\n"
      key_found = true
    end
    new_lines << line
  end
  unless section_found
    new_lines << "\n[#{section}]\n"
    new_lines << "#{key}=#{value}\n"
  else
    new_lines << "#{key}=#{value}\n" unless key_found
  end
  File.open(INI_FILENAME_FV, "w") { |f| f.write(new_lines.join.gsub(/\n\n+/, "\n\n")) }
  return true
rescue
  return false
end

# HZM_VXA::Ini를 class로 미리 정의
# 게임 스크립트가 나중에 같은 class를 열어도 충돌 없음
# 게임에 HZM_VXA 자체가 없어도 정의됨
module HZM_VXA
  class Ini
    # 클래스 메서드 (self.load/save 방식으로 쓰는 게임용)
    def self.load(section, key, default = 100)
      fv_ini_load(section, key, default)
    end
    def self.save(section, key, value)
      fv_ini_save(section, key, value)
    end
    def self.init; true; end
    def self.setup; true; end

    # 인스턴스 메서드 (Ini.new(filename)으로 쓰는 게임용)
    def initialize(filename = nil)
      @filename = filename || INI_FILENAME_FV
    end
    def load(section, key, default = 100)
      fv_ini_load(section, key, default)
    end
    def save(section, key, value)
      fv_ini_save(section, key, value)
    end
  end
end
