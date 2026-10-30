#ifdef __clang__
#pragma clang diagnostic ignored "-Wempty-translation-unit"
#endif

// No source-tree headers: exercise the ABI exactly as a language binding does.
extern const char *elg_version(void);

int main(void) {
  const char *version = elg_version();
  return version && *version ? 0 : 1;
}
