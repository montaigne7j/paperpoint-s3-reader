#pragma once

namespace PageRenderProfiler {

bool isEnabled();
bool setEnabled(bool enabled);

class Scoped {
 public:
  explicit Scoped(bool enabled);
  ~Scoped();

  Scoped(const Scoped&) = delete;
  Scoped& operator=(const Scoped&) = delete;

 private:
  bool previousEnabled;
};

}  // namespace PageRenderProfiler
