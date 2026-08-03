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

inline unsigned long
niu_target(unsigned long npu_id, unsigned long address)
{
    return ((npu_id & 0xfUL) << 56) | (address & 0x00ffffffffffffffUL);
}

inline void
xai_niu_ub_to_remote_ub(unsigned long bytes, unsigned long ub_src,
                        unsigned long target_npu_id,
                        unsigned long remote_ub_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = ub_src;
    register unsigned long dst asm("a1") =
            niu_target(target_npu_id, remote_ub_dst);
    asm volatile(".insn r 0x5b, 0x7, 0x00, %0, %1, %2"
                 :
                 : "r"(rlen), "r"(src), "r"(dst)
                 : "memory");
}

inline void
xai_niu_ub_to_remote_gm(unsigned long bytes, unsigned long ub_src,
                        unsigned long target_npu_id,
                        unsigned long remote_gm_dst)
{
    register unsigned long rlen asm("t0") = bytes;
    register unsigned long src asm("a0") = ub_src;
    register unsigned long dst asm("a1") =
            niu_target(target_npu_id, remote_gm_dst);
    asm volatile(".insn r 0x5b, 0x7, 0x01, %0, %1, %2"
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

#define XAI_REMOTE_SYNC_SET(src, dst, id, peer_npu) \
    do { \
        register unsigned long remote_info asm("a0") = \
                (((peer_npu) & 0xfUL) << 56) | ((dst) & 0xffUL); \
        asm volatile("addi x0, %0, 0\n\t" \
                     ".insn r 0x5b, 0x2, 0x03, x" XAI_STR(src) \
                     ", x10, x" XAI_STR(id) \
                     : \
                     : "r"(remote_info) \
                     : "memory"); \
    } while (0)

#define XAI_REMOTE_SYNC_WAIT(src, dst, id, peer_npu) \
    do { \
        register unsigned long remote_info asm("a0") = \
                (((peer_npu) & 0xfUL) << 56) | ((dst) & 0xffUL); \
        asm volatile("addi x0, %0, 0\n\t" \
                     ".insn r 0x5b, 0x2, 0x04, x" XAI_STR(src) \
                     ", x10, x" XAI_STR(id) \
                     : \
                     : "r"(remote_info) \
                     : "memory"); \
    } while (0)

#define XAI_SYNC_FILE_IO 3
#define XAI_SYNC_CPU 4
#define XAI_SYNC_NIU 8

} // namespace

extern "C" int
main()
{
    constexpr unsigned long npu0_mask = 0x1;
    constexpr unsigned long npu1_mask = 0x2;
    constexpr unsigned long npu2_mask = 0x4;
    constexpr unsigned long bytes = 256;
    constexpr unsigned long remote_gm_output = 0x2000;
    constexpr unsigned long ub_source = 0x100000000ULL;
    constexpr unsigned long remote_ub_output = 0x100000400ULL;
    constexpr unsigned long input_file_index = 0;
    constexpr unsigned long output_file_index = 0;

    write_reg_npu(npu0_mask);
    WriteDataToNpu(bytes, ub_source, input_file_index);
    XAI_SYNC_SET(XAI_SYNC_FILE_IO, XAI_SYNC_NIU, 0);
    XAI_SYNC_WAIT(XAI_SYNC_FILE_IO, XAI_SYNC_NIU, 0);

    xai_niu_ub_to_remote_ub(bytes, ub_source, 1, remote_ub_output);
    xai_niu_ub_to_remote_gm(bytes, ub_source, 2, remote_gm_output);
    XAI_REMOTE_SYNC_SET(XAI_SYNC_NIU, XAI_SYNC_FILE_IO, 10, 1);
    XAI_REMOTE_SYNC_SET(XAI_SYNC_NIU, XAI_SYNC_FILE_IO, 11, 2);

    write_reg_npu(npu1_mask);
    XAI_REMOTE_SYNC_WAIT(XAI_SYNC_NIU, XAI_SYNC_FILE_IO, 10, 0);
    LoadDataFromNpu(bytes, remote_ub_output, output_file_index);
    XAI_SYNC_SET(XAI_SYNC_FILE_IO, XAI_SYNC_CPU, 3);
    XAI_SYNC_WAIT(XAI_SYNC_FILE_IO, XAI_SYNC_CPU, 3);

    write_reg_npu(npu2_mask);
    XAI_REMOTE_SYNC_WAIT(XAI_SYNC_NIU, XAI_SYNC_FILE_IO, 11, 0);
    LoadDataFromNpu(bytes, remote_gm_output, output_file_index);
    XAI_SYNC_SET(XAI_SYNC_FILE_IO, XAI_SYNC_CPU, 4);
    XAI_SYNC_WAIT(XAI_SYNC_FILE_IO, XAI_SYNC_CPU, 4);

    return 0;
}
