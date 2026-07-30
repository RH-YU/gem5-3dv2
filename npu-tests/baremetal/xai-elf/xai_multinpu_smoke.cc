namespace
{

inline void
write_reg_npu(unsigned long mask)
{
    asm volatile("csrw 0x7c0, %0" : : "r"(mask & 0xfUL) : "memory");
}

inline void
WriteDataToNpu(unsigned long bytes, unsigned long npu_dst,
               unsigned long file_index)
{
    register unsigned long byte_count asm("t0") = bytes;
    register unsigned long dst asm("a0") = npu_dst;
    register unsigned long index asm("a1") = file_index;
    asm volatile(".insn r 0x5b, 0x4, 0x00, %0, %1, %2"
                 :
                 : "r"(byte_count), "r"(dst), "r"(index)
                 : "memory");
}

inline void
LoadDataFromNpu(unsigned long bytes, unsigned long npu_src,
                unsigned long file_index)
{
    register unsigned long byte_count asm("t0") = bytes;
    register unsigned long src asm("a0") = npu_src;
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
xai_vadd_v3_v1_v2()
{
    asm volatile(".insn r 0x5b, 0x1, 0x02, x3, x1, x2" ::: "memory");
}

inline void
xai_vstore_v3(unsigned long ub_dst)
{
    asm volatile(".insn r 0x5b, 0x1, 0x01, x0, %0, x3"
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
#define XAI_SYNC_CPU 4

void
run_all_selected_npus()
{
    constexpr unsigned long gm_input = 0x0;
    constexpr unsigned long gm_rhs = 0x10;
    constexpr unsigned long gm_result = 0x20;
    constexpr unsigned long ub_lhs = 0x100000000ULL;
    constexpr unsigned long ub_rhs = 0x100000010ULL;
    constexpr unsigned long ub_result = 0x100000020ULL;
    constexpr unsigned long vector_bytes = 16;
    constexpr unsigned long file_index = 0;
    constexpr unsigned long all_npu_mask = 0xf;

    write_reg_npu(all_npu_mask);

    WriteDataToNpu(32, gm_input, file_index);
    XAI_SYNC_SET(XAI_SYNC_GM_FILE_IO, XAI_SYNC_MTE4, 0);
    XAI_SYNC_WAIT(XAI_SYNC_GM_FILE_IO, XAI_SYNC_MTE4, 0);

    xai_nsetvl(4, 2);
    xai_mte4(vector_bytes, gm_input, ub_lhs);
    xai_mte4(vector_bytes, gm_rhs, ub_rhs);
    XAI_SYNC_SET(XAI_SYNC_MTE4, XAI_SYNC_VCU, 1);
    XAI_SYNC_WAIT(XAI_SYNC_MTE4, XAI_SYNC_VCU, 1);

    xai_vload_v1(ub_lhs);
    xai_vload_v2(ub_rhs);
    xai_vadd_v3_v1_v2();
    xai_vstore_v3(ub_result);
    XAI_SYNC_SET(XAI_SYNC_VCU, XAI_SYNC_MTE2, 2);
    XAI_SYNC_WAIT(XAI_SYNC_VCU, XAI_SYNC_MTE2, 2);

    xai_mte2(vector_bytes, ub_result, gm_result);
    XAI_SYNC_SET(XAI_SYNC_MTE2, XAI_SYNC_GM_FILE_IO, 3);
    XAI_SYNC_WAIT(XAI_SYNC_MTE2, XAI_SYNC_GM_FILE_IO, 3);

    LoadDataFromNpu(vector_bytes, gm_result, file_index);
    XAI_SYNC_SET(XAI_SYNC_GM_FILE_IO, XAI_SYNC_CPU, 4);
    XAI_SYNC_WAIT(XAI_SYNC_GM_FILE_IO, XAI_SYNC_CPU, 4);
}

} // namespace

extern "C" int
main()
{
    run_all_selected_npus();
    return 0;
}
