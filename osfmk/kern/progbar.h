#ifndef PROGBAR_H
#define PROGBAR_H

void progbar_init(const char *label, unsigned int total);
void progbar_update(unsigned int current);
void progbar_finish(void);

#endif
