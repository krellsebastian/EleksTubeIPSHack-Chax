#ifndef BUTTONS_H
#define BUTTONS_H
/*
 * A simple helper class to call common functions on all buttons at once.
 */

#include "Button_SL.hpp"
#include "Hardware.h"

class Buttons {
public:
  Buttons() : left(BUTTON_LEFT_PIN), mode(BUTTON_MODE_PIN), right(BUTTON_RIGHT_PIN), power(BUTTON_POWER_PIN) {}

  void begin()
    { left.begin(); mode.begin(); right.begin(); power.begin(); left.setDebounceTime_ms(100); 
    mode.setDebounceTime_ms(100); right.setDebounceTime_ms(100); power.setDebounceTime_ms(100);}
    
  // Just making them public, so we don't have to proxy everything.
  Btn::Button left, mode, right, power;
private: 
};

#endif // BUTTONS_H
