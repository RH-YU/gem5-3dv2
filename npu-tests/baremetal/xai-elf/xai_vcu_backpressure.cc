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
xai_nsetvl(unsigned long avl, unsigned long eew)
{
    asm volatile(".insn r 0x5b, 0x2, 0x00, x0, %0, %1"
                 :
                 : "r"(avl), "r"(eew)
                 : "memory");
}

inline void
xai_mte4(unsigned long bytes, unsigned long gm_src, unsigned long ub_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = gm_src;
    register unsigned long dst asm("a1") = ub_dst;
    asm volatile(".insn r 0x5b, 0x0, 0x00, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

inline void
xai_mte2(unsigned long bytes, unsigned long ub_src, unsigned long gm_dst)
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
xai_vload_v1(unsigned long ub_src)
{
    asm volatile(".insn r 0x5b, 0x1, 0x00, x1, %0, x0"
                 :
                 : "r"(ub_src)
                 : "memory");
}

inline void
xai_vload_v2(unsigned long ub_src)
{
    asm volatile(".insn r 0x5b, 0x1, 0x00, x2, %0, x0"
                 :
                 : "r"(ub_src)
                 : "memory");
}

inline void
xai_vadd_v1_v1_v2()
{
    asm volatile(".insn r 0x5b, 0x1, 0x02, x1, x1, x2" ::: "memory");
}

inline void
xai_vstore_v1(unsigned long ub_dst)
{
    asm volatile(".insn r 0x5b, 0x1, 0x01, x0, %0, x1"
                 :
                 : "r"(ub_dst)
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
#define XAI_SYNC_VCU 2
#define XAI_SYNC_GM_FILE_IO 3

} // namespace

extern "C" int
main()
{
    constexpr unsigned long gm_input = 0x0;
    constexpr unsigned long gm_rhs = 0x10;
    constexpr unsigned long gm_result = 0x20;
    constexpr unsigned long ub_accumulator = 0x100000000ULL;
    constexpr unsigned long ub_rhs = 0x100000010ULL;
    constexpr unsigned long ub_result = 0x100000020ULL;
    constexpr unsigned long vector_bytes = 16;
    constexpr unsigned long file_index = 1;
    constexpr unsigned long blocker_gm = 0x1000;
    constexpr unsigned long blocker_file_index = 2;
    constexpr unsigned long blocker_bytes = 1024 * 1024;
    constexpr unsigned long recursive_add_count = 128;

    WriteDataToGm(32, gm_input, file_index);
    XAI_SYNC_SET(XAI_SYNC_GM_FILE_IO, XAI_SYNC_MTE4, 0);
    XAI_SYNC_WAIT(XAI_SYNC_GM_FILE_IO, XAI_SYNC_MTE4, 0);

    xai_nsetvl(4, 2);
    xai_mte4(vector_bytes, gm_input, ub_accumulator);
    xai_mte4(vector_bytes, gm_rhs, ub_rhs);
    XAI_SYNC_SET(XAI_SYNC_MTE4, XAI_SYNC_VCU, 1);
    XAI_SYNC_WAIT(XAI_SYNC_MTE4, XAI_SYNC_VCU, 1);

    WriteDataToGm(blocker_bytes, blocker_gm, blocker_file_index);
    XAI_SYNC_SET(XAI_SYNC_GM_FILE_IO, XAI_SYNC_VCU, 4);
    XAI_SYNC_WAIT(XAI_SYNC_GM_FILE_IO, XAI_SYNC_VCU, 4);

    xai_vload_v1(ub_accumulator);
    xai_vload_v2(ub_rhs);
    for (unsigned long index = 0; index < recursive_add_count; ++index) {
        xai_vadd_v1_v1_v2();
    }
    xai_vstore_v1(ub_result);
    XAI_SYNC_SET(XAI_SYNC_VCU, XAI_SYNC_MTE2, 2);
    XAI_SYNC_WAIT(XAI_SYNC_VCU, XAI_SYNC_MTE2, 2);

    xai_mte2(vector_bytes, ub_result, gm_result);
    XAI_SYNC_SET(XAI_SYNC_MTE2, XAI_SYNC_GM_FILE_IO, 3);
    XAI_SYNC_WAIT(XAI_SYNC_MTE2, XAI_SYNC_GM_FILE_IO, 3);

    LoadDataFromGm(vector_bytes, gm_result, file_index);

    return 0;
}
