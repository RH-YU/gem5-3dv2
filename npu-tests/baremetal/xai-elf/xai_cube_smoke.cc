namespace
{

inline void
WriteDataToGm(unsigned long bytes, unsigned long gm_dst,
              unsigned long file_index)
{
    register unsigned long byte_count asm("t0") = bytes;
    register unsigned long dst asm("a0") = gm_dst;
    register unsigned long index asm("a1") = file_index;
    asm volatile(".insn r 0x5b, 0x4, 0x00, %0, %1, %2"
                 :
                 : "r"(byte_count), "r"(dst), "r"(index)
                 : "memory");
}

inline void
LoadDataFromGm(unsigned long bytes, unsigned long gm_src,
               unsigned long file_index)
{
    register unsigned long byte_count asm("t0") = bytes;
    register unsigned long src asm("a0") = gm_src;
    register unsigned long index asm("a1") = file_index;
    asm volatile(".insn r 0x5b, 0x4, 0x01, %0, %1, %2"
                 :
                 : "r"(byte_count), "r"(src), "r"(index)
                 : "memory");
}

inline void
xai_mte4_gm_to_l1(unsigned long bytes, unsigned long gm_src,
                  unsigned long l1_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = gm_src;
    register unsigned long dst asm("a1") = l1_dst;
    asm volatile(".insn r 0x5b, 0x0, 0x02, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

inline void
xai_mte1_l1_to_l0a(unsigned long bytes, unsigned long l1_src,
                   unsigned long l0a_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = l1_src;
    register unsigned long dst asm("a1") = l0a_dst;
    asm volatile(".insn r 0x5b, 0x3, 0x02, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

inline void
xai_mte1_l1_to_l0b(unsigned long bytes, unsigned long l1_src,
                   unsigned long l0b_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = l1_src;
    register unsigned long dst asm("a1") = l0b_dst;
    asm volatile(".insn r 0x5b, 0x3, 0x03, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

inline void
xai_mte1_l1_to_ub(unsigned long bytes, unsigned long l1_src,
                  unsigned long ub_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = l1_src;
    register unsigned long dst asm("a1") = ub_dst;
    asm volatile(".insn r 0x5b, 0x3, 0x01, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

inline void
xai_mte2_ub_to_gm(unsigned long bytes, unsigned long ub_src,
                  unsigned long gm_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = ub_src;
    register unsigned long dst asm("a1") = gm_dst;
    asm volatile(".insn r 0x5b, 0x0, 0x01, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

inline void
xai_cube_mma_fp32(unsigned long l0c_dst, unsigned long l0a_src,
                 unsigned long l0b_src)
{
    register unsigned long dst asm("t0") = l0c_dst;
    register unsigned long lhs asm("a0") = l0a_src;
    register unsigned long rhs asm("a1") = l0b_src;
    asm volatile(".insn r 0x5b, 0x6, 0x00, %0, %1, %2"
                 :
                 : "r"(dst), "r"(lhs), "r"(rhs)
                 : "memory");
}

inline void
xai_fixpipe_l0c_to_l1(unsigned long bytes, unsigned long l0c_src,
                      unsigned long l1_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = l0c_src;
    register unsigned long dst asm("a1") = l1_dst;
    asm volatile(".insn r 0x5b, 0x5, 0x00, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

#define XAI_STR_HELPER(value) #value
#define XAI_STR(value) XAI_STR_HELPER(value)
#define XAI_SYNC_SET(src, dst, id) \
    asm volatile(".insn r 0x5b, 0x2, 0x01, x" XAI_STR(src) ", x" \
                 XAI_STR(dst) ", x" XAI_STR(id) ::: "memory")
#define XAI_SYNC_WAIT(src, dst, id) \
    asm volatile(".insn r 0x5b, 0x2, 0x02, x" XAI_STR(src) ", x" \
                 XAI_STR(dst) ", x" XAI_STR(id) ::: "memory")

#define XAI_SYNC_MTE4 0
#define XAI_SYNC_MTE2 1
#define XAI_SYNC_GM_FILE_IO 3
#define XAI_SYNC_CPU 4
#define XAI_SYNC_MTE1 5
#define XAI_SYNC_CUBE 6
#define XAI_SYNC_FIXPIPE 7

} // namespace

extern "C" int
main()
{
    constexpr unsigned long gm_input = 0x0;
    constexpr unsigned long gm_result = gm_input + 0x600;

    constexpr unsigned long ub_result = 0x100000000ULL;

    constexpr unsigned long l1_base = 0x100080000ULL;
    constexpr unsigned long l1_a = l1_base;
    constexpr unsigned long l1_b = l1_base + 0x200;
    constexpr unsigned long l1_result = l1_base + 0x600;

    constexpr unsigned long l0a_base = 0x100100000ULL;
    constexpr unsigned long l0b_base = 0x100110000ULL;
    constexpr unsigned long l0c_base = 0x100120000ULL;

    constexpr unsigned long matrix_a_bytes = 8 * 16 * 4;
    constexpr unsigned long matrix_b_bytes = 16 * 16 * 4;
    constexpr unsigned long matrix_c_bytes = 8 * 16 * 4;
    constexpr unsigned long input_bytes = matrix_a_bytes + matrix_b_bytes;
    constexpr unsigned long file_index = 0;

    WriteDataToGm(input_bytes, gm_input, file_index);
    XAI_SYNC_SET(XAI_SYNC_GM_FILE_IO, XAI_SYNC_MTE4, 0);
    XAI_SYNC_WAIT(XAI_SYNC_GM_FILE_IO, XAI_SYNC_MTE4, 0);

    xai_mte4_gm_to_l1(input_bytes, gm_input, l1_base);
    XAI_SYNC_SET(XAI_SYNC_MTE4, XAI_SYNC_MTE1, 1);
    XAI_SYNC_WAIT(XAI_SYNC_MTE4, XAI_SYNC_MTE1, 1);

    xai_mte1_l1_to_l0a(matrix_a_bytes, l1_a, l0a_base);
    xai_mte1_l1_to_l0b(matrix_b_bytes, l1_b, l0b_base);
    XAI_SYNC_SET(XAI_SYNC_MTE1, XAI_SYNC_CUBE, 2);
    XAI_SYNC_WAIT(XAI_SYNC_MTE1, XAI_SYNC_CUBE, 2);

    xai_cube_mma_fp32(l0c_base, l0a_base, l0b_base);
    XAI_SYNC_SET(XAI_SYNC_CUBE, XAI_SYNC_FIXPIPE, 3);
    XAI_SYNC_WAIT(XAI_SYNC_CUBE, XAI_SYNC_FIXPIPE, 3);

    xai_fixpipe_l0c_to_l1(matrix_c_bytes, l0c_base, l1_result);
    XAI_SYNC_SET(XAI_SYNC_FIXPIPE, XAI_SYNC_MTE1, 4);
    XAI_SYNC_WAIT(XAI_SYNC_FIXPIPE, XAI_SYNC_MTE1, 4);

    xai_mte1_l1_to_ub(matrix_c_bytes, l1_result, ub_result);
    XAI_SYNC_SET(XAI_SYNC_MTE1, XAI_SYNC_MTE2, 5);
    XAI_SYNC_WAIT(XAI_SYNC_MTE1, XAI_SYNC_MTE2, 5);

    xai_mte2_ub_to_gm(matrix_c_bytes, ub_result, gm_result);
    XAI_SYNC_SET(XAI_SYNC_MTE2, XAI_SYNC_GM_FILE_IO, 6);
    XAI_SYNC_WAIT(XAI_SYNC_MTE2, XAI_SYNC_GM_FILE_IO, 6);

    LoadDataFromGm(matrix_c_bytes, gm_result, file_index);
    XAI_SYNC_SET(XAI_SYNC_GM_FILE_IO, XAI_SYNC_CPU, 7);
    XAI_SYNC_WAIT(XAI_SYNC_GM_FILE_IO, XAI_SYNC_CPU, 7);

    return 0;
}
