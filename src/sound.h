#ifndef __SOUND_H
#define __SOUND_H

void sound_setup(void);
void sound_startloops(unsigned char newmask);
void sound_player_out(int player);
void sound_number(int n);
void sound_beat(int speed);
void sound_letsgo();
void sound_winner();
void sound_paired();
void sound_setvolume(int volume);
int sound_getvolume();

#endif

