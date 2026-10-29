#include "viewer_bridge.h"
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

int main() {
  try {
    mjgs::SetWorldPly("first.ply");
    mjgs::ToggleVisible();
    if (*mjgs::HiddenState() != 1) throw std::runtime_error("Toggle failed");
    std::exception_ptr error;
    std::thread other([&] {
      try {
        if (*mjgs::HiddenState() != 0)
          throw std::runtime_error("Viewer state leaked across threads");
        mjgs::SetWorldPly("second.ply");
        mjgs::Shutdown();
      } catch (...) { error = std::current_exception(); }
    });
    other.join();
    if (error) std::rethrow_exception(error);
    if (*mjgs::HiddenState() != 1)
      throw std::runtime_error("Other viewer reset this viewer");
    mjgs::Shutdown();
    if (*mjgs::HiddenState() != 0 || mjgs::ExitRequested())
      throw std::runtime_error("Shutdown failed to reset viewer state");
    std::cout << "Viewer isolation and lifecycle passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
