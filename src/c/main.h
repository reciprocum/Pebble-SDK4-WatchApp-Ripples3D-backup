/*
   WatchApp: Ripples 3D
   File    : main.h
   Author  : Afonso Santos, Portugal
   Notes   : Dedicated to all the @PebbleDev team and to @KatharineBerry in particular
           : ... for her CloudPebble online dev environment that made this possible.

   Last revision: 13h45 September 11 2016  GMT
*/

#include "Config.h"


// World related

#ifdef PBL_PLATFORM_APLITE
  #define GRID_LINES        25
#else
  #define GRID_LINES        31
#endif

#define  GRID_SCALE                 6.283185f
#define  CAM3D_DISTANCEFROMORIGIN   8.75f


/* -----------   PHYSICS PARAMETERS   ----------- */

//  With a lubrication value of 6 drag is 1/2^6 (1/64 ~1.5%) of speed. Should kill all speed in about 100 frames (~4s)
//  Increase this value for the speed to last longer
//  Decrease this value for the speed to dissipate faster
#define OSCILLATOR_LUBRICATION_LEVEL    6

//  Controls how fast a wrist tilt will influence the oscilator horizontal moving speed.
//  Increase this value for a "heavier" feeling
//  Decrease this value for a "lighter" feeling
#define OSCILLATOR_INERTIA_LEVEL        2


/* -----------   Default modes   ----------- */

//#define  OSCILLATOR_MODE_DEFAULT     OSCILLATOR_MODE_FLOATING
//#define  OSCILLATOR_MODE_DEFAULT     OSCILLATOR_MODE_BOUNCING

#define  ANTIALIASING_DEFAULT       false

#ifdef GIF
  #define  OSCILLATOR_MODE_DEFAULT    OSCILLATOR_MODE_ANCHORED
  #define  PLOTTER_MODE_DEFAULT       PLOTTER_MODE_LINES
  #define  ANIMATION_INTERVAL_MS      150
#else
  #define  OSCILLATOR_MODE_DEFAULT    OSCILLATOR_MODE_ANCHORED
  #define  PLOTTER_MODE_DEFAULT       PLOTTER_MODE_LINES

  #ifdef EMU
    #define  ANIMATION_INTERVAL_MS    120
  #else
    #define  ANIMATION_INTERVAL_MS    40
  #endif
#endif

// Animation related: adds wrist movement reaction inertia to dampen accelerometer jerkiness.
#define ACCEL_SAMPLER_CAPACITY    8


#ifdef PBL_COLOR
  #define  COLOR_MODE_DEFAULT       COLOR_MODE_DIST
#else
  #define  COLOR_MODE_DEFAULT       COLOR_MODE_MONO
#endif

#define VISIBILITY_MAX_ITERATIONS   4
#define TERMINATOR_MAX_ITERATIONS   4