#include "psmove.h"

long int random(void);

int main(int argc, char** argv)
{
  struct psmove_state *state = psmove_setup(NULL);
    
  while (1) {
    int i;
    struct psmove_report report;
    
    psmove_poll(state, &report);
    
    for (i=0; i < report.device_reports; i++) {
      struct psmove_device_report *dr = &report.device_report[i];
      
      printf("%s: ax0=%d ay0=%d az0=%d\n", 
        myba2str(&dr->addr),
        psmove_report_get(dr, ax0),
        psmove_report_get(dr, ay0),
        psmove_report_get(dr, az0)
      );
      
      if (psmove_report_get(dr, buttons) & PSMOVE_BUTTON_MASK(T))
        psmove_set_leds(state, &dr->addr, random(), random(), random(), 0, 0);
        
      if (!(psmove_report_get(dr, buttons) & PSMOVE_BUTTON_MASK(T)))
        psmove_reset_leds(state, &dr->addr);
        

    }
  }
}

