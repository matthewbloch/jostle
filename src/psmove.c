/* modified from http://www.pabr.org/linmctool/ to work in jostle game */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <assert.h>

#include "psmove.h"

struct psmove_state {
  struct motion_dev *devs;
  char *usb_force_master;
  int csk, isk;
};

static void fatal(char *msg) {
  if ( errno ) perror(msg); else fprintf(stderr, "%s\n", msg);
  exit(1);
}

// ----------------------------------------------------------------------
// Replacement for libbluetooth

int mystr2ba(const char *s, bdaddr_t *ba) {
  if ( strlen(s) != 17 ) return 1;
  for ( int i=0; i<6; ++i ) {
    int d = strtol(s+15-3*i, NULL, 16);
    if ( d<0 || d>255 ) return 1;
    ba->b[i] = d;
  }
  return 0;
}

char *myba2str(const bdaddr_t *ba) {
  static char buf[2][18];  // Static buffer valid for two invocations.
  static int index = 0;
  index = (index+1)%2;
  sprintf(buf[index], "%02x:%02x:%02x:%02x:%02x:%02x",
	  ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
  return buf[index];
}

/**********************************************************************/
// Bluetooth HID devices

struct motion_dev {
  int index;
  int csk_command_sent; 
  bdaddr_t addr;
  enum { PSMOVE } type;
  int csk;
  int isk;
  struct motion_dev *next;
};

#define L2CAP_PSM_HIDP_CTRL 0x11
#define L2CAP_PSM_HIDP_INTR 0x13

#define HIDP_TRANS_GET_REPORT    0x40
#define HIDP_TRANS_SET_REPORT    0x50
#define HIDP_DATA_RTYPE_INPUT    0x01
#define HIDP_DATA_RTYPE_OUTPUT   0x02
#define HIDP_DATA_RTYPE_FEATURE  0x03

// Incoming connections.

int l2cap_listen(const bdaddr_t *bdaddr, unsigned short psm) {
  int sk = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if ( sk < 0 ) fatal("socket");

  struct sockaddr_l2 addr = {
    .l2_family = AF_BLUETOOTH,
    .l2_bdaddr = *BDADDR_ANY,
    .l2_psm = htobs(psm),
    .l2_cid = 0,
  };
  if ( bind(sk, (struct sockaddr *)&addr, sizeof(addr)) < 0 ) {
    perror("bind");
    close(sk);
    return -1;
  }

  if ( listen(sk, 5) < 0 ) fatal("listen");
  return sk;
}

struct motion_dev *accept_device(int csk, int isk) {
  fprintf(stderr, "Incoming connection...\n");
  struct motion_dev *dev = malloc(sizeof(struct motion_dev));
  memset(dev, 0, sizeof(struct motion_dev));
  if ( ! dev ) fatal("malloc");

  dev->csk = accept(csk, NULL, NULL);
  if ( dev->csk < 0 ) fatal("accept(CTRL)");
  dev->isk = accept(isk, NULL, NULL);
  if ( dev->isk < 0 ) fatal("accept(INTR)");

  struct sockaddr_l2 addr;
  socklen_t addrlen = sizeof(addr);
  if ( getpeername(dev->isk, (struct sockaddr *)&addr, &addrlen) < 0 )
    fatal("getpeername");
  dev->addr = addr.l2_bdaddr;
  
  {
    // Distinguish SIXAXIS / DS3 / PSMOVE.
    unsigned char resp[64];
    char get03f2[] = { HIDP_TRANS_GET_REPORT | HIDP_DATA_RTYPE_FEATURE | 8,
		       0xf2, sizeof(resp), sizeof(resp)>>8 };
    send(dev->csk, get03f2, sizeof(get03f2), 0);  // 0301 is interesting too.
    int nr = recv(dev->csk, resp, sizeof(resp), 0);
    assert ( nr < 19 );
  }

  return dev;
}

// Outgoing connections.

int l2cap_connect(bdaddr_t *ba, unsigned short psm) {
  int sk = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if ( sk < 0 ) fatal("socket");

  struct sockaddr_l2 daddr = {
    .l2_family = AF_BLUETOOTH,
    .l2_bdaddr = *ba,
    .l2_psm = htobs(psm),
    .l2_cid = 0,
  };
  if ( connect(sk, (struct sockaddr *)&daddr, sizeof(daddr)) < 0 )
    fatal("connect");

  return sk;
}

struct motion_dev *connect_device(bdaddr_t *ba) {
  fprintf(stderr, "Connecting to %s\n", myba2str(ba));
  struct motion_dev *dev = malloc(sizeof(struct motion_dev));
  memset(dev, 0, sizeof(struct motion_dev));
  if ( ! dev ) fatal("malloc");
  dev->addr = *ba;
  dev->csk = l2cap_connect(ba, L2CAP_PSM_HIDP_CTRL);
  dev->isk = l2cap_connect(ba, L2CAP_PSM_HIDP_INTR);
  dev->csk_command_sent = 0;
  return dev;
}

/**********************************************************************/
// Device setup

int hidp_trans(struct motion_dev *dev, char *buf, int len) {
  if (dev->csk_command_sent)
    return -2;
    
  dev->csk_command_sent=1;
  if ( send(dev->csk, buf, len, 0) != len ) fatal("send(CTRL)");
  
  return 0;
}

void hidp_ack(struct motion_dev *dev) {
  char ack;
  int nr = recv(dev->csk, &ack, sizeof(ack), 0);
  if ( nr!=1 || ack!=0 ) fatal("ack");
  dev->csk_command_sent = 0;
}

int psmove_set_leds_by_dev(struct motion_dev *dev, char r, char g, char b, char rumble)
{
    char report[] = {
      HIDP_TRANS_SET_REPORT | HIDP_DATA_RTYPE_OUTPUT,
      2, 0, r, g, b, 0, rumble,
    };

    return hidp_trans(dev, report, sizeof(report));
}

int psmove_set_leds(struct psmove_state *state, bdaddr_t *addr, char r, char g, char b, char rumble, int block)
{
  struct motion_dev *dev = state->devs;
  
  for (dev = state->devs; dev != NULL; dev = dev->next) {
    if (memcmp(&dev->addr, addr, sizeof(bdaddr_t)) == 0) {
      if (psmove_set_leds_by_dev(dev, r, g, b, rumble) == 0) {
        if (block)
          hidp_ack(dev);
        return 0;
      }
      else {
        return -2;
      }
    }
  }
  return -1;
}

void setup_device(struct motion_dev *dev, int n) {
  psmove_set_leds_by_dev(dev, 255, 255, 255, 255);
  hidp_ack(dev);
  psmove_set_leds_by_dev(dev, 0, 0, 0, 0);

  fprintf(stderr, "New device %d %s\n",
	  dev->index, myba2str(&dev->addr));
}

/**********************************************************************/
// Reports

struct psmove_state* psmove_setup(char* usb_force_master) {
  struct psmove_state *state;
  int csk, isk;
  
  csk = l2cap_listen(BDADDR_ANY, L2CAP_PSM_HIDP_CTRL);
  isk = l2cap_listen(BDADDR_ANY, L2CAP_PSM_HIDP_INTR);

  if ( !(csk>=0 && isk>=0) )
      fatal("Unable to listen on HID PSMs (running as root?)");
  state = malloc(sizeof(struct psmove_state));
  memset(state, 0, sizeof(struct psmove_state));
  
  state->csk = csk;
  state->isk = isk;
  state->usb_force_master = usb_force_master;
  
  return state;
}

void psmove_poll(struct psmove_state *state, struct psmove_report* out) {
  fd_set fds; FD_ZERO(&fds);
  int fdmax = 0;
  if ( state->csk >= 0 ) FD_SET(state->csk, &fds);
  if ( state->isk >= 0 ) FD_SET(state->isk, &fds);
  if ( state->csk > fdmax ) fdmax = state->csk;
  if ( state->isk > fdmax ) fdmax = state->isk;
  for ( struct motion_dev *dev=state->devs; dev; dev=dev->next ) {
    FD_SET(dev->csk, &fds); if ( dev->csk > fdmax ) fdmax = dev->csk;
    FD_SET(dev->isk, &fds); if ( dev->isk > fdmax ) fdmax = dev->isk;
  }
  if ( select(fdmax+1,&fds,NULL,NULL,NULL) < 0 ) fatal("select");
  // Incoming connection ?
  if ( state->csk>=0 && FD_ISSET(state->csk,&fds) ) {
    struct motion_dev *dev = accept_device(state->csk, state->isk);
    dev->index = state->devs ? state->devs->index+1 : 0;
    dev->next = state->devs;
    state->devs = dev;
    setup_device(dev, dev->index);
  }
  
  out->device_reports = 0;

  // Incoming input report ?
  for ( struct motion_dev *dev = state->devs; dev; dev = dev->next ) {
    if ( FD_ISSET(dev->csk, &fds) ) {
      hidp_ack(dev);
    }
    if ( FD_ISSET(dev->isk, &fds) ) {
      int nr = recv(dev->isk, out->device_report[out->device_reports].data, 256, 0);
      if ( nr <= 0 ) {
        fprintf(stderr, "%d disconnected\n", dev->index);
        close(dev->csk); close(dev->isk);
        struct motion_dev **pdev;
        for ( pdev=&state->devs; *pdev!=dev; pdev=&(*pdev)->next ) ;
        *pdev = dev->next;
        free(dev);
      } else {
        out->device_report[out->device_reports].addr = dev->addr;
        out->device_reports++;
      }
    }
  }
}
