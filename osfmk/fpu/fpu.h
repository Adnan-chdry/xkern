#ifndef XKERN_FPU_H
#define XKERN_FPU_H

int fpu_has_sse(void);
int fpu_has_sse2(void);
void fpu_init(void);

void fpu_save(void);
void fpu_restore(void);

void fpu_test_run(void);

#endif
