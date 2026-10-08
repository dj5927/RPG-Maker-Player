window_src = <<'RUBY'
class Window_Selectable < Window_Base
  def update
    @cursor_sprite.visible = self.active
    @help_sprite.visible = true if @index >= 0
  end
end
RUBY

scene_src = <<'RUBY'
class Scene_Language
  def main
  end
end
RUBY

$RGSS_SCRIPTS = [
  [0, 'Window_Selectable ***', 0, window_src],
  [1, 'Scene_Language', 0, scene_src]
]

compat = File.expand_path('../app/src/main/assets/rgss_compat/common/cicpoffs_compat.rb', __dir__)
load compat
patched = $RGSS_SCRIPTS[0][3]
raise 'Freebird marker missing' unless patched.include?('RPGMP_FREEBIRD_NIL_VISIBLE')
raise 'cursor nil guard missing' unless patched.include?('if @cursor_sprite')
raise 'help nil guard missing' unless patched.include?('if @help_sprite')
nil.visible = true
raise 'Freebird nil visible guard missing' unless nil.respond_to?(:visible=)

mouse_skip = File.expand_path('../app/src/main/assets/rgss_compat/cicpoffs/ports/mouse_skip.rb', __dir__)
load mouse_skip
raise 'Mouse.pos_x missing' unless Mouse.respond_to?(:pos_x) && Mouse.pos_x == 0
raise 'Mouse.pos_y missing' unless Mouse.respond_to?(:pos_y) && Mouse.pos_y == 0
raise 'Mouse.position missing' unless Mouse.respond_to?(:position) && Mouse.position == [0, 0]

Object.send(:remove_const, :Preload) if Object.const_defined?(:Preload)
$RGSS_SCRIPTS = [[0, 'Window_Selectable', 0, window_src]]
load compat
plain = $RGSS_SCRIPTS[0][3]
raise 'non-Freebird game was modified' if plain.include?('RPGMP_FREEBIRD_NIL_VISIBLE')

puts 'RGSS_TOTHEMOON_COMPAT_SMOKE_PASS'
