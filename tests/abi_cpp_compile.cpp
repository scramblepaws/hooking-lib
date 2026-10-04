#include "overlay/overlay.h"
#include "overlay/overlay_c.h"

int main() {
  const overlay::Status status = overlay::init();
  if (status == overlay::Status::ok && overlay_abi_version() == OVERLAY_ABI_VERSION) return 0;
  return 1;
}
