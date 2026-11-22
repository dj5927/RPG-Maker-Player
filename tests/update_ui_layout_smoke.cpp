#include "update_ui_layout.hpp"
#include <iostream>
#include <stdexcept>

void verify(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  const auto dialog = updateModalRect();
  const auto yes = updateYesRect();
  const auto no = updateNoRect();
  const auto bar = updateProgressTrackRect();
  verify(dialog.x > 0 && dialog.y > 0, "modal start outside screen");
  verify(dialog.x + dialog.w < 1280 && dialog.y + dialog.h < 720, "modal outside screen");
  verify(updateRectContains(dialog, yes), "Yes button outside modal");
  verify(updateRectContains(dialog, no), "No button outside modal");
  verify(updateRectContains(dialog, bar), "progress bar outside modal");
  verify(yes.x + yes.w < no.x, "Yes and No buttons overlap");
  verify(yes.y == no.y && yes.h == no.h, "buttons have inconsistent vertical position");
  verify(bar.w > 400 && bar.h >= 20, "progress track too small");
  std::cout << "UPDATE_UI_MODAL_LAYOUT_SMOKE_PASS\n";
}
