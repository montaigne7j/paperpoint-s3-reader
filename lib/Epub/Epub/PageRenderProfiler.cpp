#include "PageRenderProfiler.h"

namespace PageRenderProfiler {
namespace {
bool gEnabled = false;
}

bool isEnabled() {
  return gEnabled;
}

bool setEnabled(const bool enabled) {
  const bool previous = gEnabled;
  gEnabled = enabled;
  return previous;
}

Scoped::Scoped(const bool enabled)
    : previousEnabled(setEnabled(enabled)) {}

Scoped::~Scoped() {
  setEnabled(previousEnabled);
}

}  // namespace PageRenderProfiler
