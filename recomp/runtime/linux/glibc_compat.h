// Included in every Linux source file by the build. A program built on a recent
// distribution asks for the newest version of each C library function, and then
// refuses to start on a slightly older system such as SteamOS. These three have
// newer variants that differ only in error reporting: use the original ones.
#if defined(__linux__) && defined(__x86_64__)
__asm__(".symver cosh,cosh@GLIBC_2.2.5");
__asm__(".symver sinh,sinh@GLIBC_2.2.5");
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
#endif

// fmod likewise. The C23 variants of the number parsers, which a recent
// compiler selects by itself, are supplied by win32.cpp (they exist only from
// glibc 2.38) so that the program also starts on Debian 12 and similar.
#if defined(__linux__) && defined(__x86_64__)
__asm__(".symver fmod,fmod@GLIBC_2.2.5");
#endif
