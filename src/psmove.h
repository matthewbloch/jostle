#ifndef __PSMOVE_H
#define __PSMOVE_H

#include <bluetooth/bluetooth.h> /* for bdaddr_t */

struct psmove_state; /** opaque state */

/** Report from a given */
struct psmove_device_report {
  bdaddr_t addr;
  unsigned char data[256];
};

struct psmove_report {
  int device_reports;
  struct psmove_device_report device_report[7];
};

static inline unsigned char psmove_report__trigger(unsigned char* d) { return d[6]; }
static inline int psmove_report__buttons(unsigned char* d) { return d[2] | (d[1] << 8) | ((d[3] & 0x01) << 16) | ((d[4] & 0xF0) << 13); }
static inline short psmove_report__ax0(unsigned char* d) { return d[13] + d[14]*256 - 32768; }
static inline short psmove_report__ay0(unsigned char* d) { return d[15] + d[16]*256 - 32768; }
static inline short psmove_report__az0(unsigned char* d) { return d[17] + d[18]*256 - 32768; }
static inline short psmove_report__ax1(unsigned char* d) { return d[19] + d[20]*256 - 32768; }
static inline short psmove_report__ay1(unsigned char* d) { return d[21] + d[22]*256 - 32768; }
static inline short psmove_report__az1(unsigned char* d) { return d[23] + d[24]*256 - 32768; }
static inline short psmove_report__gx0(unsigned char* d) { return d[25] + d[26]*256 - 32768; }
static inline short psmove_report__gy0(unsigned char* d) { return d[27] + d[28]*256 - 32768; }
static inline short psmove_report__gz0(unsigned char* d) { return d[29] + d[30]*256 - 32768; }
static inline short psmove_report__gx1(unsigned char* d) { return d[31] + d[32]*256 - 32768; }
static inline short psmove_report__gy1(unsigned char* d) { return d[33] + d[34]*256 - 32768; }
static inline short psmove_report__gz1(unsigned char* d) { return d[35] + d[36]*256 - 32768; }
static inline short psmove_report__mx(unsigned char* d) { return (d[38]<<12) | (d[39]<<4); }
static inline short psmove_report__my(unsigned char* d) { return (d[40]<<8)  | (d[41] & 0xf0); }
static inline short psmove_report__mz(unsigned char* d) { return (d[41]<<12) | (d[42]<<4); }

#define psmove_report_get(report, val) psmove_report__##val(report->data + 1)

/* List of Buttons, based on MoveOnPC's moveonpc.h */
enum {
    L2,
    R2,
    L1,
    R1,
    TRIANGLE,
    CIRCLE,
    CROSS,
    SQUARE,
    SELECT,
    L3,
    R3,
    START,
    UP,
    RIGHT,
    DOWN,
    LEFT,
    PS,
    UNK1,
    UNK2,
    MOVE,
    T,
    UNK3,
    UNK4,
    UNK5,
    BUTTON_COUNT,
};

#define PSMOVE_BUTTON_MASK(x) (1<<x)

/** Turns given bdaddr_t into printable string, uses two-entry static buffer
  * so copy the reults if you need to use more than 2 in a printf etc.
  */
char *myba2str(const bdaddr_t *ba);

int mystr2ba(const char *s, bdaddr_t *ba);

/** Set up USB & Bluetooth connections, optionally forcing the master address 
  * to the given master (specified as 'aa:bb:cc:dd:ee:ff' string).  Returns
  * a state structure.
  */
struct psmove_state* psmove_setup(char* usb_force_master);

/** Poll the connected Bluetooth devices - will block until the first device
  * connects, and dump reports into the given output structure (which the caller
  * must initialise).
  */
void psmove_poll(struct psmove_state *state, struct psmove_report* out);

/** Set the LED and rumble state of the given controller, idenfitied by 
  * bluetooth address.  If block is set to 1, will wait until command 
  * acknowledged by controller (and freeze feedback from other controllers while
  * that happens).  Returns 0 if successful, -1 if controller wasn't found, and
  * -2 if you sent two commands too close together, so the second one will
  * have been ignored.
  */
int psmove_set_leds(struct psmove_state *state, bdaddr_t *addr, char r, char g, char b, char rumble, int block);
#define psmove_reset_leds(s, a) psmove_set_leds((s), (a), 0, 0, 0, 0, 0)

#endif

