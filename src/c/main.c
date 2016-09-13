/*
   WatchApp: Ripples 3D
   File    : main.c
   Author  : Afonso Santos, Portugal
   Notes   : Dedicated to all the @PebbleDev team and to @KatharineBerry in particular
           : ... for her CloudPebble online dev environment that made this possible.

   Last revision: 13h25 September 13 2016  GMT
*/

#include <pebble.h>
#include <karambola/Q2.h>
#include <karambola/Q3.h>
#include <karambola/CamQ3.h>
#include <karambola/Sampler.h>

#ifndef PBL_COLOR
  #include <karambola/Draw2D.h>
#endif

#include "main.h"
#include "Config.h"


// UI related
static Window         *s_window ;
static Layer          *s_window_layer ;
static Layer          *s_world_layer ;
static ActionBarLayer *s_action_bar_layer ;

// Screen related.
static GSize screen_availableSize ;
static Q     cameraCoord2screenCoord_k, cameraCoord2screenCoord_bX, cameraCoord2screenCoord_bY ;


// World related
static Q world_xMin, world_xMax, world_yMin, world_yMax, world_zMin, world_zMax ;

const Q  grid_scale     = Q_from_float(GRID_SCALE) ;
const Q  grid_halfScale = Q_from_float(GRID_SCALE) >> 1 ;   //  scale / 2

static Q3      grid_mark_worldCoord                      [GRID_LINES][GRID_LINES] ;
static Q       grid_mark_distance2oscillator             [GRID_LINES][GRID_LINES] ;
static bool    grid_mark_isVisible                       [GRID_LINES][GRID_LINES] ;
static GPoint  grid_mark_screenCoord                     [GRID_LINES][GRID_LINES] ;

static Q3      grid_xBiSegment_center_worldCoord         [GRID_LINES][GRID_LINES-1] ;
static Q       grid_xBiSegment_center_distance2oscillator[GRID_LINES][GRID_LINES-1] ;
static bool    grid_xBiSegment_center_isVisible          [GRID_LINES][GRID_LINES-1] ;
static GPoint  grid_xBiSegment_center_screenCoord        [GRID_LINES][GRID_LINES-1] ;

static Q3      grid_yBiSegment_center_worldCoord         [GRID_LINES][GRID_LINES-1] ;
static Q       grid_yBiSegment_center_distance2oscillator[GRID_LINES][GRID_LINES-1] ;
static bool    grid_yBiSegment_center_isVisible          [GRID_LINES][GRID_LINES-1] ;
static GPoint  grid_yBiSegment_center_screenCoord        [GRID_LINES][GRID_LINES-1] ;

static int32_t oscillator_anglePhase ;
static Q2      oscillator_position ;
static Q2      oscillator_speed ;          // For OSCILLATOR_MODE_BOUNCING
static Q2      oscillator_acceleration ;   // For OSCILLATOR_MODE_BOUNCING

static int        s_world_updateCount       = 0 ;
static AppTimer  *s_world_updateTimer_ptr   = NULL ;


/***  ---------------  COLOR MODE  ---------------  ***/

typedef enum { COLOR_MODE_UNDEFINED
             , COLOR_MODE_MONO
             , COLOR_MODE_SIGNAL
             , COLOR_MODE_DIST
             }
ColorMode ;


static ColorMode  s_colorMode = COLOR_MODE_UNDEFINED ;


void
colorMode_set
( ColorMode pColorMode )
{
  if (s_colorMode == pColorMode)
    return ;

  s_colorMode = pColorMode ;
}


void
colorMode_change
( )
{
  // Cycle trough the color modes.
  switch (s_colorMode)
  {
    case COLOR_MODE_MONO:
      colorMode_set( COLOR_MODE_SIGNAL ) ;
    break ;

    case COLOR_MODE_SIGNAL:
      colorMode_set( COLOR_MODE_DIST ) ;
    break ;

    case COLOR_MODE_DIST:
      colorMode_set( COLOR_MODE_MONO ) ;
    break ;

    default:
      colorMode_set( COLOR_MODE_DEFAULT ) ;
    break ;
  } ;
}


void
colorMode_change_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{ colorMode_change( ) ; }


/***  ---------------  PLOTTER MODE  ---------------  ***/

typedef enum { PLOTTER_MODE_UNDEFINED
             , PLOTTER_MODE_DOTS
             , PLOTTER_MODE_LINES
             , PLOTTER_MODE_GRID
             }
PlotterMode ;

static PlotterMode    s_plotterMode = PLOTTER_MODE_UNDEFINED ;


void
plotterMode_set
( PlotterMode pPlotterMode )
{
  if (s_plotterMode == pPlotterMode)
    return ;

  s_plotterMode = pPlotterMode ;
}


void
plotterMode_change
( )
{
  // Cycle trough the plotter modes.
  switch (s_plotterMode)
  {
    case PLOTTER_MODE_DOTS:
      plotterMode_set( PLOTTER_MODE_LINES ) ;
    break ;

    case PLOTTER_MODE_LINES:
      plotterMode_set( PLOTTER_MODE_GRID ) ;
    break ;

    case PLOTTER_MODE_GRID:
      plotterMode_set( PLOTTER_MODE_DOTS ) ;
    break ;

    default:
      plotterMode_set( PLOTTER_MODE_DEFAULT ) ;
    break ;
  } ;
}


void
plotterMode_change_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{ plotterMode_change( ) ; }


/***  ---------------  Camera related  ---------------  ***/

static CamQ3   s_cam ;
static Q3      s_cam_viewPoint ;
static Q       s_cam_zoom        = PBL_IF_RECT_ELSE(Q_from_float(+1.25f), Q_from_float(+1.15f)) ;
static int32_t s_cam_rotZangle   = 0 ;
static int32_t s_cam_rotZangleStep ;


void
cam_config
( const Q3      *pViewPoint
, const int32_t  pRotZangle
)
{
  Q3 scaledVP ;

  Q3_scaTo( &scaledVP
          , Q_from_float(CAM3D_DISTANCEFROMORIGIN)
          , (pViewPoint->x != Q_0  ||  pViewPoint->y != Q_0)          // Viewpoint not on Z axis ?
            ? pViewPoint                                              // Use original view point.
            : &(Q3){ .x = Q_1>>4, .y = Q_1>>4, .z = pViewPoint->z }
          ) ;

  Q3 rotatedVP ;
  Q3_rotZ( &rotatedVP, &scaledVP, pRotZangle ) ;

  // setup 3D camera
  CamQ3_lookAtOriginUpwards( &s_cam, &rotatedVP, s_cam_zoom, CAM_PROJECTION_PERSPECTIVE ) ;
}


/***  ---------------  OSCILLATOR MODE  ---------------  ***/

typedef enum { OSCILLATOR_MODE_UNDEFINED
             , OSCILLATOR_MODE_ANCHORED
             , OSCILLATOR_MODE_FLOATING
             , OSCILLATOR_MODE_BOUNCING
             }
OscilatorMode ;

static OscilatorMode    s_oscillatorMode = OSCILLATOR_MODE_UNDEFINED ;

void oscillator_distance_update( const Q2 *pRefPtr ) ;
Q2*  position_setFromSensors( Q2 *positionPtr ) ;
Q2*  acceleration_setFromSensors( Q2 *accelerationPtr ) ;


void
oscillatorMode_set
( OscilatorMode pOscilatorMode )
{
  if (s_oscillatorMode == pOscilatorMode)
    return ;

  switch (s_oscillatorMode = pOscilatorMode)
  {
    case OSCILLATOR_MODE_FLOATING:
      cam_config( Q3_set( &s_cam_viewPoint, Q_from_float( +0.1f ), Q_from_float( -1.0f ), Q_from_float( +0.7f ) ), s_cam_rotZangle = 0 ) ;
      position_setFromSensors( &oscillator_position ) ;
    break ;

    case OSCILLATOR_MODE_BOUNCING:
      cam_config( Q3_set( &s_cam_viewPoint, Q_from_float( +0.1f ), Q_from_float( -1.0f ), Q_from_float( +0.7f ) ), s_cam_rotZangle = 0 ) ;
      Q2_assign( &oscillator_position, &Q2_origin ) ;   //  Initial position is center of grid.

      #ifdef GIF
        Q2_set( &oscillator_speed, 3072, -1536 ) ;
      #else
        Q2_assign( &oscillator_speed, &Q2_origin ) ;   //  No initial speed.
      #endif
    break ;

    case OSCILLATOR_MODE_ANCHORED:
    default:
      s_cam_rotZangle = 0 ;
      Q2_assign( &oscillator_position, &Q2_origin ) ;   //  oscillator_position := Q2_origin
    break ;
  } ;

  oscillator_distance_update( &oscillator_position ) ;
}


void
oscillatorMode_change
( )
{
  // Cycle trough the oscillator modes.
  switch (s_oscillatorMode)
  {
    case OSCILLATOR_MODE_ANCHORED:
      oscillatorMode_set( OSCILLATOR_MODE_FLOATING ) ;
    break ;

    case OSCILLATOR_MODE_FLOATING:
      oscillatorMode_set( OSCILLATOR_MODE_BOUNCING ) ;
    break ;

    case OSCILLATOR_MODE_BOUNCING:
      oscillatorMode_set( OSCILLATOR_MODE_ANCHORED ) ;
    break ;

    default:
      oscillatorMode_set( OSCILLATOR_MODE_DEFAULT ) ;
    break ;
  } ;
}


void
oscillatorMode_change_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{ oscillatorMode_change( ) ; }


Q
world_function
( const Q pX, const Q pY )
{
  const Q       x     = oscillator_position.x - pX ;
  const Q       y     = oscillator_position.y - pY ;
  const Q       dist  = Q_sqrt( Q_mul( x, x ) + Q_mul( y, y ) ) ;
  const int32_t angle = ((dist >> 1) + oscillator_anglePhase) & 0xFFFF ;   //  (dist / 2 + anglePhase) % TRIG_MAX_RATIO

  return cos_lookup( angle ) ;                                             //  z = f( x, y )
}


bool
world_pointIsVisible
( const Q3 *point    // IN : a function point inside the world min/max box.
, const Q3 *viewer   // IN : usualy the camera's view point (can be outside the world min/max box).
)
{
  Q  k ;
  Q3 point2viewer ;

  Q3_sub( &point2viewer, viewer, point ) ;

  //  1) Clip the view line to the nearest min/max box wall.
  if (viewer->x > world_xMax)
    k = Q_div( world_xMax - point->x, point2viewer.x ) ;
  else if (viewer->x < world_xMin)
    k = Q_div( world_xMin - point->x, point2viewer.x ) ;
  else
    k = Q_1 ;

  Q kMin = k ;
    
  if (viewer->y > world_yMax)
    k = Q_div( world_yMax - point->y, point2viewer.y ) ;
  else if (viewer->y < world_yMin)
    k = Q_div( world_yMin - point->y, point2viewer.y ) ;
  else
    k = Q_1 ;

  if (k < kMin)
    kMin = k ;
    
  if (viewer->z > world_zMax)
    k = Q_div( world_zMax - point->z, point2viewer.z ) ;
  else if (viewer->z < world_zMin)
    k = Q_div( world_zMin - point->z, point2viewer.z ) ;
  else
    k = Q_1 ;

  if (k < kMin)
    kMin = k ;

  if (kMin < Q_1)
    Q3_sca( &point2viewer, k, &point2viewer ) ;    //  Do the clipping to the nearest min/max box wall.


  bool hasPositives = false ;
  bool hasNegatives = false ;

  //  2) Test the clipped line segment with increasingly smaller steps.

  Q3 smallStep ;
  Q  smallStepK ;

  for ( smallStepK = Q_1     , Q3_assign( &smallStep, &point2viewer )                    //  Start with the biggest possible small step, all the way to the nearest point in the min/max box (k=1).
      ; smallStepK >= Q_1>>VISIBILITY_MAX_ITERATIONS                                     //  Newton (split in half) steps. TODO: refine exit criteria.
      ; smallStepK >>= 1     , smallStep.x >>= 1, smallStep.y >>= 1, smallStep.z >>= 1   //  Divide the step length in half.
      )
  {
    Q3 probe ;
    Q3_add( &probe, point, &smallStep ) ;

    Q3 bigStep ;
    Q  bigStepK ;

    for ( k = smallStepK,  bigStepK = smallStepK << 1,  bigStep.x = smallStep.x << 1,  bigStep.y = smallStep.y << 1,  bigStep.z = smallStep.z << 1
        ; k <= Q_1
        ; k += bigStepK ,  Q3_add( &probe, &probe, &bigStep )
        )
    {
      Q probeAltitude = probe.z - world_function( probe.x, probe.y ) ;
    
      if (probeAltitude > Q_0)
      {
        if (hasNegatives)
          return false ;    // Not visible since it has both positive and negative probe altitudes (function altitude has zeros).
  
        hasPositives = true ;
      }
      else if (probeAltitude < Q_0)
      {
        if (hasPositives)
          return false ;    // Not visible since it has both positive and negative probe altitudes (function altitude has zeros).
  
        hasNegatives = true ;
      }
    }
  }

  return true ;
}


#ifdef GIF
  void world_update_timer_handler( void *data ) ;
  
  void
  gifStepper_advance_click_handler
  ( ClickRecognizerRef recognizer
  , void              *context
  )
  {
    if (s_world_updateTimer_ptr == NULL)
      s_world_updateTimer_ptr = app_timer_register( 0, world_update_timer_handler, NULL ) ;   // Schedule a world update.
  }
#else
  Sampler   *accelSampler_x = NULL ;    // To be allocated at accelSamplers_initialize( ).
  Sampler   *accelSampler_y = NULL ;    // To be allocated at accelSamplers_initialize( ).
  Sampler   *accelSampler_z = NULL ;    // To be allocated at accelSamplers_initialize( ).
  
  
  void
  accelSamplers_initialize
  ( )
  {
    accelSampler_x = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;
    accelSampler_y = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;
    accelSampler_z = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;
  
    for (int i = 0  ;  i < ACCEL_SAMPLER_CAPACITY  ;  ++i)
    {
      Sampler_push( accelSampler_x,  -81 ) ;   // STEADY viewPoint attractor.
      Sampler_push( accelSampler_y, -816 ) ;   // STEADY viewPoint attractor.
      Sampler_push( accelSampler_z, -571 ) ;   // STEADY viewPoint attractor.
    }
  }
  
  
  void
  accelSamplers_finalize
  ( )
  {
    free( Sampler_free( accelSampler_x ) ) ; accelSampler_x = NULL ;
    free( Sampler_free( accelSampler_y ) ) ; accelSampler_y = NULL ;
    free( Sampler_free( accelSampler_z ) ) ; accelSampler_z = NULL ;
  }
  
  
  // Acellerometer handlers.
  void
  accel_data_service_handler
  ( AccelData *data
  , uint32_t   num_samples
  )
  { }
#endif


/***  ---------------  oscillator---------  ***/

void
oscillator_distance_update
( const Q2 *pRefPtr )
{
  Q y2[GRID_LINES] ;

  for (int j = 0  ;  j < GRID_LINES  ;  j++)
  {
    const Q y = pRefPtr->y - grid_mark_worldCoord[0][j].y ;
    y2[j] = Q_mul(y, y) ;
  }

  for (int i = 0  ;  i < GRID_LINES  ;  i++)
  {
    const Q x   = pRefPtr->x - grid_mark_worldCoord[i][0].x ;
    const Q x2i = Q_mul(x, x) ;

    for (int j = 0  ;  j < GRID_LINES  ;  j++)
      grid_mark_distance2oscillator[i][j] = Q_sqrt( x2i + y2[j] ) ;
  }
}


void
function_grid_initialize
( )
{
  const Q gridStep = Q_div( grid_scale, Q_from_int(GRID_LINES - 1) ) ;

  world_xMin = world_yMin = -grid_halfScale ;
  world_xMax = world_yMax = +grid_halfScale ;
  world_zMin = -Q_1 ;
  world_zMax = +Q_1 ;

  int i ;
  Q   x ;

  for ( i = 0          , x = world_xMin
      ; i < GRID_LINES
      ; i++            , x += gridStep
      )
  {
    int j ;
    Q   y ;

    for ( j = 0          , y = world_yMin
        ; j < GRID_LINES
        ; j++            , y += gridStep
        )
    {
      grid_mark_worldCoord[i][j].x = x ;
      grid_mark_worldCoord[i][j].y = y ;
    }
  }
}


/***  ---------------  Color related  ---------------  ***/

static GColor     s_stroke_color ;
static GColor     s_background_color ;
static bool       s_isInverted ;


#ifdef PBL_COLOR
  static GColor  s_colorMap[8] ;
  
  void
  color_initialize
  ( )
  {
    s_stroke_color     = GColorWhite ;
    s_background_color = GColorBlack ;
    s_isInverted       = false ;
    
    s_colorMap[7] = GColorWhite ;
    s_colorMap[6] = GColorMelon ;
    s_colorMap[5] = GColorMagenta ;
    s_colorMap[4] = GColorRed ;
    s_colorMap[3] = GColorCyan ;
    s_colorMap[2] = GColorYellow ;
    s_colorMap[1] = GColorGreen ;
    s_colorMap[0] = GColorVividCerulean ;
  }
  
  
  void
  set_stroke_color
  ( GContext *gCtx
  , const int i
  , const int j
  )
  {
    switch (s_colorMode)
    {
      case COLOR_MODE_SIGNAL:
        graphics_context_set_stroke_color( gCtx, grid_mark_worldCoord[i][j].z > Q_0  ?  GColorMelon  :  GColorVividCerulean ) ;
      break ;
  
      case COLOR_MODE_DIST:
        graphics_context_set_stroke_color( gCtx, s_colorMap[((grid_mark_distance2oscillator[i][j]) >> 15) & 0b111] ) ;             //  (2 * distance) % 8
      break ;
  
      case COLOR_MODE_MONO:
      default:
        graphics_context_set_stroke_color( gCtx, s_stroke_color ) ;
      break ;
    } ;
  }
  
  
  static bool   s_antialiasing = ANTIALIASING_DEFAULT ;
  
  void
  antialiasing_change_click_handler
  ( ClickRecognizerRef recognizer
  , void              *context
  )
  { s_antialiasing = !s_antialiasing ; }
#else
  void
  color_initialize
  ( )
  {
    s_stroke_color     = GColorBlack ;
    s_background_color = GColorWhite ;
    s_isInverted       = true ;
  }
  
  
  ink_t
  get_stroke_ink
  ( const int i
  , const int j
  )
  {
    switch (s_colorMode)
    {
      case COLOR_MODE_MONO:
        return INK100 ;
      break ;
      
      case COLOR_MODE_SIGNAL:
        return grid_mark_worldCoord[i][j].z > Q_0  ?  INK100  :  INK33 ;
      break ;
  
      case COLOR_MODE_DIST:
        switch (((grid_mark_distance2oscillator[i][j]) >> 15) & 0b1 )   //  (2 * distance) % 2
        {
          case 1:
            return INK33 ;
  
          case 0:
          default:
            return INK100 ;
          break ;
        }
      break ;
  
      default:
        return INK100 ;
      break ;
    } ;
  }
  
  
  void
  invert_change
  ( )
  {
    s_isInverted = !s_isInverted ;
  
    if (s_isInverted)
    {
      s_stroke_color     = GColorBlack ;
      s_background_color = GColorWhite ;
    }
    else
    {
      s_stroke_color     = GColorWhite ;
      s_background_color = GColorBlack ;
    }
  
    window_set_background_color( s_window, s_background_color ) ;
    action_bar_layer_set_background_color( s_action_bar_layer, s_background_color) ;
  }
  
  
  void
  invert_change_click_handler
  ( ClickRecognizerRef recognizer
  , void              *context
  )
  { invert_change( ) ; }
#endif


void
world_initialize
( )
{
  function_grid_initialize( ) ;
  color_initialize( ) ;

  colorMode_set( COLOR_MODE_DEFAULT ) ;
  plotterMode_set( PLOTTER_MODE_DEFAULT ) ;
  oscillatorMode_set( OSCILLATOR_MODE_DEFAULT ) ;

#ifndef GIF
  accelSamplers_initialize( ) ;
#endif

  // Initialize cam rotation vars.
  s_cam_rotZangleStep = TRIG_MAX_ANGLE >> 9 ;   //  2 * PI / 512
}


// UPDATE WORLD OBJECTS PROPERTIES

void
function_update_worldPoints
( )
{
  for (int i = 0  ;  i < GRID_LINES  ;  ++i)
    for (int j = 0  ;  j < GRID_LINES  ;  ++j)
    {
      const int32_t angle = ((grid_mark_distance2oscillator[i][j] >> 1) + oscillator_anglePhase) & 0xFFFF ;   //  (grid_mark_distance2oscillator[i][j] / 2 + anglePhase) % TRIG_MAX_RATIO
      grid_mark_worldCoord[i][j].z = cos_lookup( angle ) ;                                           //  z = f( x, y )
    }
}


void
function_update_worldPoints_isVisible
( )
{
  for (int i = 0  ;  i < GRID_LINES  ;  ++i)
    for (int j = 0  ;  j < GRID_LINES  ;  ++j)
      grid_mark_isVisible[i][j] = world_pointIsVisible( &grid_mark_worldCoord[i][j], &s_cam.viewPoint ) ;
}


Q2*
position_setFromSensors
( Q2 *positionPtr )
{
  AccelData ad ;
  
  if (accel_service_peek( &ad ) < 0)         // Accel service not available.
    Q2_assign( positionPtr, &Q2_origin ) ;
  else
  {
    positionPtr->x = Q_mul( grid_halfScale, ad.x << 6 ) ;
    positionPtr->y = Q_mul( grid_halfScale, ad.y << 6 ) ;
  }
  
  return positionPtr ;
}


Q2*
acceleration_setFromSensors
( Q2 *accelerationPtr )
{
  AccelData ad ;

  if (accel_service_peek( &ad ) < 0)         // Accel service not available.
    Q2_assign( accelerationPtr, &Q2_origin ) ;
  else
  {
    accelerationPtr->x = ad.x >> OSCILLATOR_INERTIA_LEVEL ;
    accelerationPtr->y = ad.y >> OSCILLATOR_INERTIA_LEVEL ;
  }

  return accelerationPtr ;
}


// UPDATE CAMERA

void
camera_update
( )
{
  //  If oscillator position is moving around then the camera has allready been configured elsewhere.
  if (s_oscillatorMode != OSCILLATOR_MODE_ANCHORED)
    return ;

  #ifdef GIF
    // Fixed point view for GIF generation.
    Q3_set( &s_cam_viewPoint, Q_from_float( -0.1f ), Q_from_float( +1.0f ), Q_from_float( +0.7f ) ) ;
  #else
    // Non GIF => Interactive: use acelerometer to affect camera's view point position.
    AccelData ad ;
  
    if (accel_service_peek( &ad ) < 0)         // Accel service not available.
    {
      Sampler_push( accelSampler_x,  -81 ) ;   // STEADY viewPoint attractor. 
      Sampler_push( accelSampler_y, -816 ) ;   // STEADY viewPoint attractor.
      Sampler_push( accelSampler_z, -571 ) ;   // STEADY viewPoint attractor.
    }
    else
    {
      #ifdef EMU
        if (ad.x == 0  &&  ad.y == 0  &&  ad.z == -1000)   // Under EMU with SENSORS off this is the default output.
        {
          Sampler_push( accelSampler_x,  -81 ) ;
          Sampler_push( accelSampler_y, -816 ) ;
          Sampler_push( accelSampler_z, -571 ) ;
        }
        else                                               // If running under EMU the SENSOR feed must be ON.
        {
          Sampler_push( accelSampler_x, ad.x ) ;
          Sampler_push( accelSampler_y, ad.y ) ;
          Sampler_push( accelSampler_z, ad.z ) ;
        }
      #else
        Sampler_push( accelSampler_x, ad.x ) ;
        Sampler_push( accelSampler_y, ad.y ) ;
        Sampler_push( accelSampler_z, ad.z ) ;
      #endif
    }

    const float kAvg = 0.001f / accelSampler_x->samplesNum ;
    const float avgX = (float)(kAvg * accelSampler_x->samplesAcum ) ;
    const float avgY =-(float)(kAvg * accelSampler_y->samplesAcum ) ;
    const float avgZ =-(float)(kAvg * accelSampler_z->samplesAcum ) ;
  
    Q3_set( &s_cam_viewPoint, Q_from_float( avgX ), Q_from_float( avgY ), Q_from_float( avgZ ) ) ;
  #endif

  s_cam_rotZangle += s_cam_rotZangleStep ;
  s_cam_rotZangle &= 0xFFFF ;        // Keep angle normalized.

  cam_config( &s_cam_viewPoint, s_cam_rotZangle ) ;
}


void
oscillator_update
( )
{
  oscillator_anglePhase = TRIG_MAX_ANGLE - ((s_world_updateCount << 8) & 0xFFFF) ;   //  2*PI - (256 * s_world_updateCount) % TRIG_MAX_RATIO

  switch (s_oscillatorMode)
  {
    case OSCILLATOR_MODE_ANCHORED:
      //  No need to call oscillator_distance_update( ) because oscillator is not moving.
    break ;

    case OSCILLATOR_MODE_FLOATING:
      oscillator_distance_update( position_setFromSensors( &oscillator_position ) ) ;
    break ;

    case OSCILLATOR_MODE_BOUNCING:
      //  1) set oscillator acceleration from sensor readings
      acceleration_setFromSensors( &oscillator_acceleration ) ;

      //  2) update oscillator speed with oscillator acceleration
      // TODO: prevent acceleration to act when position is "on the ropes".
      Q2_add( &oscillator_speed, &oscillator_speed, &oscillator_acceleration ) ;   //  oscillator_speed += oscillator_acceleration

      //  3) affect oscillator position given current oscillator speed
      Q2_add( &oscillator_position, &oscillator_position, &oscillator_speed ) ;   //  oscillator_position += oscillator_speed

      //  4) detect boundary colisions
      //     clip position to stay inside grid boundaries.
      //     invert speed direction on colision for bounce effect

      if (oscillator_position.x < -grid_halfScale)
      {
        oscillator_position.x = -grid_halfScale ;
        oscillator_speed.x    = -oscillator_speed.x ;
      }
      else if (oscillator_position.x > grid_halfScale)
      {
        oscillator_position.x = grid_halfScale ;
        oscillator_speed.x    = -oscillator_speed.x ;
      }

      if (oscillator_position.y < -grid_halfScale)
      {
        oscillator_position.y = -grid_halfScale ;
        oscillator_speed.y    = -oscillator_speed.y ;
      }
      else if (oscillator_position.y > grid_halfScale)
      {
        oscillator_position.y = grid_halfScale ;
        oscillator_speed.y    = -oscillator_speed.y ;
      }

      oscillator_distance_update( &oscillator_position ) ;

      // 6) introduce some drag to dampen oscillator speed
      #ifndef GIF
        Q2 drag ;
        Q2_sub( &oscillator_speed, &oscillator_speed, Q2_sca( &drag, Q_1 >> OSCILLATOR_LUBRICATION_LEVEL, &oscillator_speed ) ) ;
      #endif
    break ;

    default:
    break ;
  }
}


void
world_update
( )
{
  ++s_world_updateCount ;   //   "Master clock" for everything.

  oscillator_update( ) ;
  function_update_worldPoints( ) ;
  camera_update( ) ;
  function_update_worldPoints_isVisible( ) ;

  // this will queue a defered call to the world_draw( ) method.
  layer_mark_dirty( s_world_layer ) ;
}


void
function_draw_DOTS
( GContext *gCtx )
{
  for (int i = 0  ;  i < GRID_LINES  ;  ++i)
    for (int j = 0  ;  j < GRID_LINES  ;  ++j)
      if (grid_mark_isVisible[i][j])
      {
        #ifdef PBL_COLOR
          set_stroke_color( gCtx, i, j ) ;
        #endif

        graphics_draw_pixel( gCtx, grid_mark_screenCoord[i][j] ) ;
      }
}


void
function_draw_lineSegment
( GContext *gCtx
, const int i0
, const int j0
, const int i1
, const int j1
)
{
  bool p0_isVisible = grid_mark_isVisible[i0][j0] ;
  bool p1_isVisible = grid_mark_isVisible[i1][j1] ;

  if (!p0_isVisible && !p1_isVisible)    //  None of the points is visible ?
    return ;

  int iColor, jColor ;

  GPoint s0, s1 ;
  
  if (p0_isVisible && p1_isVisible)      //  Both points visible ?
  {
    s0 = grid_mark_screenCoord[i0][j0] ;
    s1 = grid_mark_screenCoord[i1][j1] ;
    iColor = i0  ; jColor = j0 ;
  }
  else                                   //  Only one of the points is visible.
  {
    Q3  visible, invisible ;

    if (p0_isVisible)
    {
      iColor = i0 ;
      jColor = j0 ;
      s0 = grid_mark_screenCoord[i0][j0] ;
      Q3_assign( &visible  , &grid_mark_worldCoord[i0][j0] ) ;
      Q3_assign( &invisible, &grid_mark_worldCoord[i1][j1] ) ;
    }
    else
    {
      iColor = i1 ;
      jColor = j1 ;
      s0 = grid_mark_screenCoord[i1][j1] ;
      Q3_assign( &visible  , &grid_mark_worldCoord[i1][j1] ) ;
      Q3_assign( &invisible, &grid_mark_worldCoord[i0][j0] ) ;
    }

    for (int i = 0  ;  i < TERMINATOR_MAX_ITERATIONS  ;  ++i)    // TODO: replace with a while with screen point distance exit heuristic.
    {
      Q3  half ;

      half.x = (visible.x + invisible.x) >> 1 ;
      half.y = (visible.y + invisible.y) >> 1 ;
      half.z = world_function( half.x, half.y ) ;
  
      if (world_pointIsVisible( &half, &s_cam.viewPoint ))
        Q3_assign( &visible  , &half ) ;
      else
        Q3_assign( &invisible, &half ) ;
    }
  
    // Calculate camera film plane 2D coordinates of 3D world points.
    Q2 vCamera ;
    CamQ3_view( &vCamera, &s_cam, &visible ) ;

    // Convert camera coordinates to screen/device coordinates.
    Q x = Q_mul( cameraCoord2screenCoord_k, vCamera.x )  +  cameraCoord2screenCoord_bX ;
    Q y = Q_mul( cameraCoord2screenCoord_k, vCamera.y )  +  cameraCoord2screenCoord_bY ;
    s1.x = Q_to_int(x) ;
    s1.y = Q_to_int(y) ;
  }

#ifdef PBL_COLOR
  set_stroke_color( gCtx, iColor, jColor ) ;
  graphics_draw_line( gCtx, s0, s1 ) ;
#else
  Draw2D_line_pattern( gCtx, s0.x, s0.y, s1.x, s1.y, get_stroke_ink( iColor, jColor ) ) ;
#endif
}


void
function_draw_xLINES
( GContext *gCtx )
{
  // x parallel lines.
  for (int j = 0  ;  j < GRID_LINES  ;  ++j)
    for (int i = 1  ;  i < GRID_LINES  ;  ++i)
      function_draw_lineSegment( gCtx, i, j, i-1, j ) ;
}


void
function_draw_yLINES
( GContext *gCtx )
{
  // y parallel lines.
  for (int i = 0  ;  i < GRID_LINES  ;  ++i)
    for (int j = 1  ;  j < GRID_LINES  ;  ++j)
      function_draw_lineSegment( gCtx, i, j, i, j-1 ) ;
}


void
world_draw
( Layer    *me
, GContext *gCtx
)
{
  LOGD( "world_draw:: s_world_updateCount = %d", s_world_updateCount ) ;

#ifdef PBL_COLOR
  graphics_context_set_antialiased( gCtx, s_antialiasing ) ;
#else
  graphics_context_set_stroke_color( gCtx, s_stroke_color ) ;
#endif

  // Calculate screen points.
  for (int i = 0  ;  i < GRID_LINES  ;  ++i)
    for (int j = 0  ;  j < GRID_LINES  ;  ++j)
    {
      // Calculate camera film plane 2D coordinates of 3D world points.
      Q2 vCamera ;
      CamQ3_view( &vCamera, &s_cam, &grid_mark_worldCoord[i][j] ) ;
  
      // Convert camera coordinates to screen/device coordinates.
      Q x = Q_mul( cameraCoord2screenCoord_k, vCamera.x )  +  cameraCoord2screenCoord_bX ;
      Q y = Q_mul( cameraCoord2screenCoord_k, vCamera.y )  +  cameraCoord2screenCoord_bY ;
      grid_mark_screenCoord[i][j].x = Q_to_int(x) ;
      grid_mark_screenCoord[i][j].y = Q_to_int(y) ;
    }

  // Draw the calculated screen points.
  switch (s_plotterMode)
  {
    case PLOTTER_MODE_DOTS:
      function_draw_DOTS( gCtx ) ;
    break ;

    case PLOTTER_MODE_GRID:
      function_draw_yLINES( gCtx ) ;

    case PLOTTER_MODE_LINES:
      function_draw_xLINES( gCtx ) ;
    break ;

    default:
    break ;
  }
}


void
world_finalize
( )
{
#ifndef GIF
  accelSamplers_finalize( ) ;
#endif
}


void
world_update_timer_handler
( void *data )
{
  s_world_updateTimer_ptr = NULL ;
  world_update( ) ;

#ifdef GIF
  if (s_world_updateCount < GIF_STOP_COUNT)
#endif
  s_world_updateTimer_ptr = app_timer_register( ANIMATION_INTERVAL_MS, world_update_timer_handler, data ) ;
}


void
world_start
( )
{
#ifndef GIF
  // Gravity aware.
 	accel_data_service_subscribe( 0, accel_data_service_handler ) ;
#endif

  // Start animation.
  world_update_timer_handler( NULL ) ;
}


void
world_stop
( )
{
  // Stop animation.
  if (s_world_updateTimer_ptr != NULL)
  {
    app_timer_cancel( s_world_updateTimer_ptr ) ;
    s_world_updateTimer_ptr = NULL ;
  }

#ifndef GIF
  // Gravity unaware.
  accel_data_service_unsubscribe( ) ;
#endif
}


void
unobstructed_area_change_handler
( AnimationProgress progress
, void             *context
)
{ screen_availableSize = layer_get_unobstructed_bounds( s_window_layer ).size ; }


void
click_config_provider
( void *context )
{
  window_single_click_subscribe( BUTTON_ID_UP
                               , (ClickHandler) colorMode_change_click_handler
                               ) ;

  window_single_click_subscribe( BUTTON_ID_SELECT
                               , (ClickHandler) plotterMode_change_click_handler
                               ) ;

#ifndef GIF
  window_single_click_subscribe( BUTTON_ID_DOWN
                               , (ClickHandler) oscillatorMode_change_click_handler
                               ) ;
#else
  window_single_click_subscribe( BUTTON_ID_DOWN
                               , (ClickHandler) gifStepper_advance_click_handler
                               ) ;
#endif

#ifdef PBL_COLOR
  window_long_click_subscribe( BUTTON_ID_DOWN
                             , 0
                             , (ClickHandler) antialiasing_change_click_handler
                             , NULL
                             ) ;
#else
  window_long_click_subscribe( BUTTON_ID_DOWN
                             , 0
                             , (ClickHandler) invert_change_click_handler
                             , NULL
                             ) ;
#endif
}



void
window_load
( Window *window )
{
  // Create and configure the layers
  s_window_layer        = window_get_root_layer( window ) ;
  screen_availableSize  = layer_get_unobstructed_bounds( s_window_layer ).size ;
  
  cameraCoord2screenCoord_k = (screen_availableSize.w > screen_availableSize.h)
             ? Q_from_int(screen_availableSize.h)
             : Q_from_int(screen_availableSize.w)
             ;

  cameraCoord2screenCoord_bX = Q_from_int(screen_availableSize.w) >> 1 ;   // bX = screen_availableSize.w / 2
  cameraCoord2screenCoord_bY = Q_from_int(screen_availableSize.h) >> 1 ;   // bY = screen_availableSize.h / 2


  s_action_bar_layer = action_bar_layer_create( ) ;
  action_bar_layer_set_background_color( s_action_bar_layer, s_background_color) ;
  action_bar_layer_set_click_config_provider( s_action_bar_layer, click_config_provider ) ;

  s_world_layer = layer_create( layer_get_frame( s_window_layer ) ) ;
  layer_set_update_proc( s_world_layer, world_draw ) ;

  // Add the layers to the main window layer.
  action_bar_layer_add_to_window( s_action_bar_layer, s_window ) ;
  layer_add_child( s_window_layer, s_world_layer ) ;

  // Obstrution handling.
  UnobstructedAreaHandlers unobstructed_area_handlers = { .change = unobstructed_area_change_handler } ;
  unobstructed_area_service_subscribe( unobstructed_area_handlers, NULL ) ;

  world_start( ) ;
}


void
window_unload
( Window *window )
{
  world_stop( ) ;

  // Unsubscribe services.
  unobstructed_area_service_unsubscribe( ) ;

  // Destroy layers.
  layer_destroy( s_world_layer ) ;
}


void
app_initialize
( void )
{
  world_initialize( ) ;

  s_window = window_create( ) ;
  window_set_background_color( s_window, s_background_color ) ;
 
  window_set_window_handlers( s_window
                            , (WindowHandlers)
                              { .load   = window_load
                              , .unload = window_unload
                              }
                            ) ;

  window_stack_push( s_window, false ) ;
}


void
app_finalize
( void )
{
  window_stack_remove( s_window, false ) ;
  window_destroy( s_window ) ;
  world_finalize( ) ;
}


int
main
( void )
{
  app_initialize( ) ;
  app_event_loop( ) ;
  app_finalize( ) ;
}