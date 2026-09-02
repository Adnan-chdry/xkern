/*
 * the kernels syscall interface inter locker multi_cop
 * generate and read also erase is the main func of this
 */

extern 'C' kernel_ldm(){
    ldm_read();
    ldm_write();
    ldm_gen();
}
