#include "SDL/SDL.h"
#include "SDL/SDL_mixer.h"

#include <errno.h>
static void fatal(char *msg) {
  if ( errno ) perror(msg); else fprintf(stderr, "%s\n", msg);
  exit(1);
}

static Mix_Chunk 
  *snd_test,
  *snd_player_out,
  *snd_letsgo,
  *snd_winner,
  *snd_paired,
  *snd_start_loops[7],
  *snd_beats[8],
  *snd_number[7],
  *snd_end;

static int bpms[8] = { 98, 105, 112, 134, 147, 175, 184, 208 };

int sound_getvolume() {
  return Mix_VolumeChunk(snd_player_out, -1);
}

void sound_setvolume(int volume) {
  Mix_Chunk **next;
  for (next = &snd_test; next != &snd_end; next++)
    if (*next)
      Mix_VolumeChunk(*next, volume);
  Mix_PlayChannel(0, snd_test, 0);
}

void sound_setup(void) {
  int audio_rate = 22050;
  Uint16 audio_format = AUDIO_S16;
  int audio_channels = 2;
  int audio_buffers = 512;
  int n;

  SDL_Init(SDL_INIT_AUDIO);

  if(Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers))
    fatal("Can't open audio");
  
  snd_test = Mix_LoadWAV("fx/test.wav");
  if (!snd_test)
    fatal("Couldn't load test sample");
  
  snd_player_out = Mix_LoadWAV("fx/player_out.wav");
  if (!snd_player_out)
    fatal("Couldn't load sample");

  snd_letsgo = Mix_LoadWAV("fx/ready.wav");
  if (!snd_letsgo)
    fatal("Couldn't load sample");

  snd_winner = Mix_LoadWAV("fx/winner.wav");
  if (!snd_winner)
    fatal("Couldn't load sample");
  
  snd_paired = Mix_LoadWAV("fx/paired.wav");
  if (!snd_paired)
    fatal("Couldn't load sample");
    
  for (n=0; n<7; n++) {
    char name[32];

    sprintf(name, "numbers/%d.wav", n+1);
    if (!(snd_number[n] = Mix_LoadWAV(name)))
      fatal("Couldn't load number sample");
      
    sprintf(name, "beats/start%d.wav", n+1);
    if (!(snd_start_loops[n] = Mix_LoadWAV(name)))
      fatal("Couldn't load start sample");
  }
  
  for (n=0; n<8; n++) {
    char name[32];
    sprintf(name, "beats/speed%d.wav", bpms[n]);
    if (!(snd_beats[n] = Mix_LoadWAV(name)))
      fatal("Couldn't load beat");
  }
}

void sound_number(int n)
{
  Mix_PlayChannel(-1, snd_number[n], 0);
}

void sound_player_out(int n)
{
  Mix_PlayChannel(-1, snd_player_out, 0);
}

void sound_letsgo()
{
  Mix_PlayChannel(-1, snd_letsgo, 0);
}

void sound_paired()
{
  Mix_PlayChannel(-1, snd_paired, 0);
}

void sound_winner()
{
  Mix_PlayChannel(-1, snd_winner, 0);
}

static unsigned char mask=0;
void sound_startloops(unsigned char newmask)
{
  int n;
  for (n=0; n<7; n++) {
    int bit=1<<n;
    if ((newmask & bit) && !(mask & bit)) {
      Mix_PlayChannel(n, snd_start_loops[n], -1);
    }
    if (!(newmask & bit) && (mask & bit)) {
      Mix_FadeOutChannel(n, 250);
    }
  }
  mask=newmask;
}
static int beat_speed=0;
static int beat_channel=-1;

void sound_beat(int speed) {
  if (beat_speed != speed) {
    if (beat_channel > -1) {
      Mix_FadeOutChannel(beat_channel, 1000);
      printf("old beat fading out on %d\n", beat_channel);
      beat_channel = -1;
    }
    if (speed > 0) {
      beat_channel = Mix_FadeInChannel(-1, snd_beats[speed-1], -1, beat_speed == 0 ? 6580 : 800);
      printf("new beat fading in on %d\n", beat_channel);
    }
  }
  beat_speed = speed;
}

int done;

void sound_loop() 
{
  SDL_Event event;
  done=0;
  while(!done) {
    while(SDL_PollEvent(&event)) {
    }
  }

  /* This is the cleaning up part */
  Mix_CloseAudio();
  SDL_Quit();

}

