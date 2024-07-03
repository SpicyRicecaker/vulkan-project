#include "lib.hpp"

#ifdef __APPLE__
const char* os = "macos";
#elif _WIN32
const char* os = "windows";
#endif

void DeletionStack::init() {
  // shouldn't be more than this many vulkan objects at a time
  // apparently deque has less allocations and copies than vector,
  // but the implementation is really complicated
  cleanup_functions.reserve(20);
}

void DeletionStack::push(function<void()>&& fn) {
  cleanup_functions.push_back(fn);
}

void DeletionStack::flush() {
  for (auto it = cleanup_functions.rbegin(); it != cleanup_functions.rend();
       it++) {
    (*it)();
  }
  cleanup_functions.clear();
}

void VK_CHECK_CONDITIONAL(VkResult result,
                          string err,
                          vector<VkResult> optionals) {
  bool valid = false;
  for (auto o : optionals) {
    if (o == result) {
      valid = true;
      break;
    }
  }

  if (!valid && result != VK_SUCCESS) {
    println("{}", string_VkResult(result));
    throw runtime_error(err);
  }
}

void VK_CHECK(VkResult result, string err) {
  if (result != VK_SUCCESS) {
    println("{}", string_VkResult(result));
    throw runtime_error(err);
  }
}
