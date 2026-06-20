#include <HalStorage.h>

#include <cstdio>
#include <new>

void OFR_fclose(void* stream) {
  auto* file = static_cast<HalFile*>(stream);
  if (file != nullptr) {
    if (*file) file->close();
    delete file;
  }
}

void* OFR_fopen(const char* filename, const char* mode) {
  if (filename == nullptr || mode == nullptr || mode[0] != 'r') return nullptr;

  auto* file = new (std::nothrow) HalFile();
  if (file == nullptr) return nullptr;

  if (!Storage.openFileForRead("TTF", filename, *file)) {
    delete file;
    return nullptr;
  }
  return file;
}

size_t OFR_fread(void* ptr, size_t size, size_t nmemb, void* stream) {
  if (ptr == nullptr || stream == nullptr || size == 0 || nmemb == 0) return 0;
  auto* file = static_cast<HalFile*>(stream);
  const size_t requested = size * nmemb;
  const int readBytes = file->read(ptr, requested);
  if (readBytes <= 0) return 0;
  return static_cast<size_t>(readBytes) / size;
}

int OFR_fseek(void* stream, long int offset, int whence) {
  if (stream == nullptr) return -1;
  auto* file = static_cast<HalFile*>(stream);

  int64_t target = 0;
  if (whence == SEEK_SET) {
    target = offset;
  } else if (whence == SEEK_CUR) {
    target = static_cast<int64_t>(file->position()) + offset;
  } else if (whence == SEEK_END) {
    target = static_cast<int64_t>(file->size()) + offset;
  } else {
    return -1;
  }

  if (target < 0) return -1;
  return file->seekSet(static_cast<size_t>(target)) ? 0 : -1;
}

long int OFR_ftell(void* stream) {
  if (stream == nullptr) return -1L;
  auto* file = static_cast<HalFile*>(stream);
  return static_cast<long int>(file->position());
}
