#include "psmove.h"
#include "sound.h"

#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>

enum jostle_gamestate {
  IDLE, 
  PLAYING,
  WINNER
};

struct jostle_gamestate_idle {
    long start_last_pushed, start_letsgo;
};
struct jostle_gamestate_playing {
    int speed;
    long next_speed_change_at;
};
struct jostle_gamestate_winner {
    int num;
    long won_at;
};

struct jostle_controller {
  bdaddr_t addr;
  int buttons;
  int is_playing;
  int jostle, jostle1;
  
  time_t last_sent;
  int last_r, last_g, last_b, last_rumble;
  long out_time;
};

static unsigned char flash_colours[7][3] = {
  {255,0,0},{0,255,0},{0,0,255},
  {255,255,0},{255,0,255},{0,255,255},
  {160,160,160}
};

struct jostle_game {
  int finished;
  int jostle_reference;
  
  int controllers;
  struct jostle_controller controller[7];
  
  int state;
  union {
    struct jostle_gamestate_idle  idle;
    struct jostle_gamestate_playing playing;
    struct jostle_gamestate_winner winner;
  } data;
};

struct jostle_game game;
struct psmove_state *psmove;

static void fatal(char *msg) {
  if ( errno ) perror(msg); else fprintf(stderr, "%s\n", msg);
  exit(1);
}

void set_leds(int num, char r, char g, char b, char rumble)
{
  time_t now = time(NULL);
  if (now - game.controller[num].last_sent >= 1 ||
      game.controller[num].last_r != r ||
      game.controller[num].last_g != g ||
      game.controller[num].last_b != b ||
      game.controller[num].last_rumble != rumble) {
    game.controller[num].last_r = r; 
    game.controller[num].last_g = g;
    game.controller[num].last_b = b;
    game.controller[num].last_rumble = rumble;
    game.controller[num].last_sent = now;
    psmove_set_leds(psmove, &game.controller[num].addr, r, g, b, rumble, 0);
  }
}

static struct timeval started={0,0};
long micros() {
  struct timeval now;
  if (started.tv_sec == 0) {
    gettimeofday(&started, NULL);
    return 0;
  }
  gettimeofday(&now, NULL);
  return 
    (now.tv_usec + now.tv_sec*1000000) - 
    (started.tv_usec + started.tv_sec*1000000);
}

void update(int is_new, int num, int pressed_buttons, int released_buttons)
{
  unsigned char pressed=0;
  unsigned char *rgb = flash_colours[num];
  int n2;
  int threshold = game.state == IDLE ? 
    game.jostle_reference * 2 : 
    game.jostle_reference * (2+game.data.playing.speed);
  int is_jostled;
  int is_warning;
  
  is_jostled = game.controller[num].jostle > threshold;
  is_warning = !is_jostled && game.controller[num].jostle > threshold - game.jostle_reference;
  
  {
    printf("jostle(%d) = %d, = reference * %d\n",
      num,
      game.controller[num].jostle,
      game.controller[num].jostle / game.jostle_reference
    );
  }
  
  if (0)
  {
    printf("jostle(%d) value %d reference %d: %d %d %d\n", num, 
      game.jostle_reference,
      game.controller[num].jostle,
      game.controller[num].jostle > (game.jostle_reference * (1+0)),
      game.controller[num].jostle > (game.jostle_reference * (1+1)),
      game.controller[num].jostle > (game.jostle_reference * (1+2))
    );
  }
  
  for (n2=0; n2<7; n2++)
    if (game.controller[n2].buttons & PSMOVE_BUTTON_MASK(T))
      pressed = pressed | 1<<n2;
  
  if (game.state == IDLE) {
  
    /* if the trigger is pulled or released, silence the beat, update start loops */
    if (pressed_buttons & PSMOVE_BUTTON_MASK(T) ||
        released_buttons & PSMOVE_BUTTON_MASK(T) ||
        is_jostled) {
      sound_startloops(pressed);
      game.data.idle.start_last_pushed = pressed == 0 ? 0 : micros();
      game.data.idle.start_letsgo = 0;
      sound_beat(0);
      game.controller[num].is_playing = pressed & (1<<num);
    }
    
    if ((pressed_buttons & PSMOVE_BUTTON_MASK(START)) &&
        (pressed_buttons & PSMOVE_BUTTON_MASK(SELECT))) {
      game.finished = 1;
      return;
    }
    
    if (pressed_buttons & PSMOVE_BUTTON_MASK(TRIANGLE)) {
      game.jostle_reference = 0;
      set_leds(num, 255, 255, 255, 0);
    }
    
    if (pressed_buttons & PSMOVE_BUTTON_MASK(CIRCLE)) {
      sound_setvolume((sound_getvolume() + 8) % 128);
    }
    else if (game.controller[num].buttons & PSMOVE_BUTTON_MASK(TRIANGLE)) {
      if (game.controller[num].jostle > game.jostle_reference)
        game.jostle_reference = game.controller[num].jostle;
      printf("reference value = %d\n", game.jostle_reference);
    }
    else {
      float mult = (1.0 + sinf((float) micros() / (3000000.0/((float)num+1)))) / 2.0;
      
      if (is_jostled && !(game.controller[num].buttons & PSMOVE_BUTTON_MASK(T))) {
        if (game.controller[num].out_time == 0) {
          sound_player_out(num);
          set_leds(num, 0, 0, 0, 255);
        }
        game.controller[num].out_time = micros();
      }
      else {
        if (micros() - game.controller[num].out_time > 1500000 ||
            is_new ||
            released_buttons & PSMOVE_BUTTON_MASK(TRIANGLE)) {
          game.controller[num].out_time = 0;
          set_leds(num, mult*rgb[0], mult*rgb[1], mult*rgb[2], is_warning ? 128 : 0);
        }
        else {
          set_leds(num, 0, 0, 0, 0);
        }
      }
    }
    
    if (micros() - game.data.idle.start_last_pushed > 3000000 && 
      pressed != 0 && (pressed & (pressed-1)) != 0) {
      sound_beat(5);
      if (micros() - game.data.idle.start_last_pushed > 7700000) {
        if (game.data.idle.start_letsgo == 0) {
          sound_startloops(0);
          sound_letsgo();
          game.data.idle.start_letsgo = micros();
        }
      }
      if (micros() - game.data.idle.start_last_pushed > 9580000) {
        game.state = PLAYING;
        memset(&game.data.playing, 0, sizeof(struct jostle_gamestate_playing));
      }
    }
  }
  
  if (game.state == PLAYING) {
    if (game.data.playing.speed == 0) {
      /* always start on speed 2 for 20-30s */
      game.data.playing.speed = 2;
      game.data.playing.next_speed_change_at = micros() + 
        (random() % 10000000) + 10000000;
    }
    
    if (micros() > game.data.playing.next_speed_change_at) {
      /*if (random() & 1)
        game.data.playing.speed++;
      else
        game.data.playing.speed--;
      game.data.playing.speed = (game.data.playing.speed % 8) + 1;*/
        
      game.data.playing.speed = rand() % 8;
      
      game.data.playing.next_speed_change_at = micros() + 
        (random() % 10000000) + 3000000;
      sound_beat(game.data.playing.speed);
    }
    
    if (is_jostled && game.controller[num].out_time == 0) {
      int still_in=0, last_one=0;
      
      game.controller[num].out_time = micros();
      sound_player_out(num);
      
      for (n2=0; n2<7; n2++) {
        if (game.controller[n2].is_playing && game.controller[n2].out_time == 0) {
          still_in++;
          last_one=n2;
        }
      }
      
      if (still_in == 1) {
        sound_beat(0);
        sound_winner();
        game.state = WINNER;
        memset(&game.data.winner, 0, sizeof(struct jostle_gamestate_winner));
        game.data.winner.num = last_one;
        game.data.winner.won_at = micros();
      }
    }
    
    if (game.controller[num].out_time != 0 || !game.controller[num].is_playing)
      set_leds(num, 0, 0, 0, 0);
    else
      set_leds(num, rgb[0], rgb[1], rgb[2], is_warning ? 128 : 0);
  }
  
  if (game.state == WINNER) {
    sound_beat(0);
    if (num == game.data.winner.num)
      set_leds(num, random(), random(), random(), 0);
    else
      set_leds(num, 0, 0, 0, 0);
      
    game.controller[num].is_playing = 0;
    
    if (micros() - game.data.winner.won_at > 5000000) {
      game.state = IDLE;
    }
  }
}

void* psmove_updater(void* unused)
{
  while (!game.finished) {
    int i;
    struct psmove_report report;
    
    psmove_poll(psmove, &report);
    
    for (i=0; i < report.device_reports; i++) {
      int j;
      struct psmove_device_report *dr = &report.device_report[i];
      short new_jostle;
      int is_new = 0;
      int new_buttons, pushed_buttons, released_buttons;
      
      /* find our record of which controller this is */
      for (j=0; j < game.controllers && memcmp(&game.controller[j].addr, &dr->addr, sizeof(bdaddr_t)) != 0; j++)
        ;
      if (j == game.controllers) {
        /* new report */
        if (j == 7)
          fatal("too many controllers");
        memcpy(&game.controller[j].addr, &dr->addr, sizeof(bdaddr_t));
        game.controllers++;
        is_new = 1;
      }

      /* rough distance */
      new_jostle = (int) sqrt(
        (psmove_report_get(dr, gx0)*psmove_report_get(dr, gx0)) +
        (psmove_report_get(dr, gy0)*psmove_report_get(dr, gy0)) +
        (psmove_report_get(dr, gz0)*psmove_report_get(dr, gz0))
      ) + (int) sqrt(
        (psmove_report_get(dr, gx1)*psmove_report_get(dr, gx1)) +
        (psmove_report_get(dr, gy1)*psmove_report_get(dr, gy1)) +
        (psmove_report_get(dr, gz1)*psmove_report_get(dr, gz1))
      );
      
      new_jostle /= 2;
      
      game.controller[j].jostle   = new_jostle;
      game.controller[j].jostle1 += new_jostle - 800;
      if (game.controller[j].jostle1 < 0)
        game.controller[j].jostle1 = 0;
      if (game.controller[j].jostle1 > 25600)
        game.controller[j].jostle1 = 25600;
      
      new_buttons = psmove_report_get(dr, buttons);
      pushed_buttons   = (game.controller[j].buttons ^ new_buttons) & new_buttons;
      released_buttons = (game.controller[j].buttons ^ new_buttons) & game.controller[j].buttons;
      game.controller[j].buttons = new_buttons; 
      
      update(is_new, j, pushed_buttons, released_buttons);
        
      //printf("%012li %s: %d %d %x %d\n", micros(), myba2str(&dr->addr), game.controller[j].jostle, game.controller[j].jostle1, game.controller[j].buttons, psmove_report_get(dr, trigger));
    }
  }
  
  return NULL;
}

void* game_runner(void* unused)
{
  return NULL;
}

void usb_scan();
void* usb_scanner(void* unused)
{
  while (!game.finished) {
    usb_scan();
    sleep(2);
  }
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t thread_psmove, thread_game, thread_usb;
  sound_setup();
  psmove = psmove_setup(NULL);

  memset(&game, 0, sizeof(game));
  game.state = IDLE;
  game.jostle_reference = 300;
  
  if (pthread_create(&thread_psmove, NULL, psmove_updater, NULL) < 0)
    fatal("psmove thread");
  if (pthread_create(&thread_game, NULL, game_runner, NULL) < 0)
    fatal("game thread");
  if (pthread_create(&thread_usb, NULL, usb_scanner , NULL) < 0)
    fatal("usb thread");
  pthread_join(thread_psmove, NULL);
  pthread_join(thread_game, NULL);
}

