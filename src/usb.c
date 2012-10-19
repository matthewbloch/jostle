#include <errno.h>
#include <usb.h>
#include "psmove.h"
#include "sound.h"

#define USB_DIR_IN 0x80
#define USB_DIR_OUT 0
#define USB_GET_REPORT 0x01
#define USB_SET_REPORT 0x09
#define VENDOR_SONY 0x054c
#define PRODUCT_PSMOVE 0x03d5

static void fatal(char *msg) {
  if ( errno ) perror(msg); else fprintf(stderr, "%s\n", msg);
  exit(1);
}

void usb_pair_device(struct usb_device *dev, int itfnum) {
  usb_dev_handle *devh = usb_open(dev);
  if ( ! devh ) fatal("usb_open");
  usb_detach_kernel_driver_np(devh, itfnum);
  int res = usb_claim_interface(devh, itfnum);
  if ( res < 0 ) fatal("usb_claim_interface");

  bdaddr_t current_ba;  // Current pairing address.

  switch ( dev->descriptor.idProduct ) {
  case PRODUCT_PSMOVE: {
    fprintf(stderr, "USB: PS MOVE\n");
    unsigned char msg[16];
    int res = usb_control_msg
      (devh, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
       USB_GET_REPORT, 0x0304, itfnum, (void*)msg, sizeof(msg), 5000);
    if ( res < 0 ) {
      /* unexpected disconnection, ignore */
      return;
    }
    for ( int i=0; i<6; ++i ) current_ba.b[i] = msg[10+i];
    break;
  }
  }

  bdaddr_t ba;  // New pairing address.

  {
    char ba_s[18];
    FILE *f = popen("hcitool dev", "r");
    if ( !f || fscanf(f, "%*s\n%*s %17s", ba_s)!=1 || mystr2ba(ba_s, &ba) )
      fatal("Unable to retrieve local bd_addr from `hcitool dev`.\n");
    pclose(f);
  }

  // Perform pairing.

  if ( ! bacmp(&current_ba, &ba) ) {
    fprintf(stderr, "  Already paired to %s\n", myba2str(&ba));
  } else {
    fprintf(stderr, "  Changing master from %s to %s\n",
	    myba2str(&current_ba), myba2str(&ba));
    switch ( dev->descriptor.idProduct ) {
    case PRODUCT_PSMOVE: {
      char msg[]= { 0x05, ba.b[0], ba.b[1], ba.b[2], ba.b[3],
		    ba.b[4], ba.b[5], 0x10, 0x01, 0x02, 0x12 };
      res = usb_control_msg
	(devh, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
	 USB_SET_REPORT, 0x0305, itfnum, msg, sizeof(msg), 5000);
      if ( res < 0 ) fatal("usb_control_msg(write master)");
      break;
    }
    }
    sound_paired();
  }

  fprintf(stderr, "  Now press the PS button.\n");
}


void usb_scan() {
  static struct usb_bus *busses;
  
  if (!busses) {
    usb_init();
    if ( usb_find_busses() < 0 ) fatal("usb_find_busses");
    busses = usb_get_busses();
    if ( ! busses ) fatal("usb_get_busses");
  }
  
  if ( usb_find_devices() < 0 ) fatal("usb_find_devices");
  
  struct usb_bus *bus;
  for ( bus=busses; bus; bus=bus->next ) {
    struct usb_device *dev;
    for ( dev=bus->devices; dev; dev=dev->next) {
      struct usb_config_descriptor *cfg;
      for ( cfg = dev->config;
	    cfg < dev->config + dev->descriptor.bNumConfigurations;
	    ++cfg ) {
	int itfnum;
	for ( itfnum=0; itfnum<cfg->bNumInterfaces; ++itfnum ) {
	  struct usb_interface *itf = &cfg->interface[itfnum];
	  struct usb_interface_descriptor *alt;
	  for ( alt = itf->altsetting;
		alt < itf->altsetting + itf->num_altsetting;
		++alt ) {
	    if ( dev->descriptor.idVendor == VENDOR_SONY &&
		  dev->descriptor.idProduct == PRODUCT_PSMOVE &&
		 alt->bInterfaceClass == 3 )
	      usb_pair_device(dev, cfg->interface->altsetting->bInterfaceNumber/*itfnum*/);
	  }
	}
      }
    }
  }
}

