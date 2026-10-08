# encoding: UTF-8
# Android/controller-safe no-op replacement for optional RGSS mouse scripts.

module Mouse
  class << self
    def update(*args); false; end
    def pos(*args); nil; end
    def position(*args); [0, 0]; end
    def pos_x(*args); 0; end
    def pos_y(*args); 0; end
    def x(*args); 0; end
    def y(*args); 0; end
    def grid(*args); nil; end
    def trigger?(*args); false; end
    def press?(*args); false; end
    def repeat?(*args); false; end
    def click?(*args); false; end
    def scroll(*args); 0; end
    def enabled?(*args); false; end
    def show(*args); nil; end
    def hide(*args); nil; end
  end
end unless defined?(Mouse)

if defined?(Mouse)
  module Mouse
    class << self
      def position(*args); [0, 0]; end unless method_defined?(:position)
      def pos_x(*args); 0; end unless method_defined?(:pos_x)
      def pos_y(*args); 0; end unless method_defined?(:pos_y)
    end
  end
end

module IBasicMouse
end unless defined?(IBasicMouse)

class RPGMP_DisabledMouse
  def method_missing(name, *args, &block)
    return false if name.to_s[-1, 1] == '?'
    nil
  end
  def respond_to_missing?(*args); true; end
end

$mouse = RPGMP_DisabledMouse.new unless defined?($mouse) && $mouse

# Legacy Freebird/SephirothSpawn mouse extensions add pathfinding helpers to
# Game_Player. Other scripts (notably UMS) may call them even when mouse input
# is disabled. Keep harmless bridge methods so disabling the mouse extension
# does not break unrelated message/event code.
if defined?(Game_Player)
  class Game_Player
    unless method_defined?(:clear_path)
      def clear_path
        @map = nil
        @runpath = false
        @event = nil if instance_variable_defined?(:@event)
        false
      end
    end

    unless method_defined?(:find_path)
      def find_path(*args)
        clear_path
        false
      end
    end

    unless method_defined?(:run_path)
      def run_path(*args)
        false
      end
    end

    unless method_defined?(:update_pathfinding)
      def update_pathfinding(*args)
        false
      end
    end
  end
end
