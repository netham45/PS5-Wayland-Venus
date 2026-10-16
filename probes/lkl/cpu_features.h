/* Hardware census names/bit positions from lkl-freebsd/host/freebsd/x86_decoder.c.
 * This reports HOST bits, not the decoder's filtered guest CPUID advertisement. */
static const struct { unsigned leaf, word, bit; const char *name; } features[] = {
    {1,3,0,"fpu"}, {1,3,4,"tsc"}, {1,3,8,"cx8"}, {1,3,15,"cmov"},
    {1,3,19,"clflush"}, {1,3,23,"mmx"}, {1,3,24,"fxsr"}, {1,3,25,"sse"},
    {1,3,26,"sse2"}, {1,3,28,"ht"}, {1,2,0,"pni"}, {1,2,1,"pclmulqdq"},
    {1,2,9,"ssse3"}, {1,2,12,"fma"}, {1,2,13,"cx16"}, {1,2,19,"sse4_1"},
    {1,2,20,"sse4_2"}, {1,2,22,"movbe"}, {1,2,23,"popcnt"}, {1,2,25,"aes"},
    {1,2,26,"xsave"}, {1,2,27,"osxsave"}, {1,2,28,"avx"}, {1,2,29,"f16c"},
    {1,2,30,"rdrand"}, {7,1,3,"bmi1"}, {7,1,5,"avx2"}, {7,1,8,"bmi2"},
    {7,1,9,"erms"}, {7,1,10,"invpcid"}, {7,1,18,"rdseed"}, {7,1,19,"adx"},
    {7,1,20,"smap"}, {7,1,23,"clflushopt"}, {7,1,24,"clwb"}, {7,1,29,"sha_ni"},
    {7,2,3,"pku"}, {7,2,4,"ospke"}, {7,2,8,"gfni"}, {7,2,9,"vaes"},
    {7,2,10,"vpclmulqdq"}, {7,2,22,"rdpid"}, {7,2,27,"movdiri"}, {7,2,28,"movdir64b"},
    {7,3,14,"serialize"}, {0x80000001,2,0,"lahf_lm"}, {0x80000001,2,5,"abm"},
    {0x80000001,2,6,"sse4a"}, {0x80000001,2,8,"prefetchw"}, {0x80000001,2,11,"xop"},
    {0x80000001,2,16,"fma4"}, {0x80000001,2,21,"tbm"}, {0x80000001,3,11,"syscall"},
    {0x80000001,3,20,"nx"}, {0x80000001,3,22,"mmxext"}, {0x80000001,3,25,"fxsr_opt"},
    {0x80000001,3,26,"pdpe1gb"}, {0x80000001,3,27,"rdtscp"}, {0x80000001,3,29,"lm"},
    {0x80000001,3,30,"3dnowext"}, {0x80000001,3,31,"3dnow"}
};
