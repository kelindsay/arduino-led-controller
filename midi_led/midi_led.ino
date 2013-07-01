
#define ARDUINO_MODE 1
#define PRINT_WAVEFORM 0

#if ARDUINO_MODE
#include <Arduino.h>
#include <MIDI.h>
#define PRINT(...) (0);
#endif

#if !ARDUINO_MODE

#define PRINT(...) (printf(__VA_ARGS__));

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <sys/time.h>
#include <unistd.h>

typedef uint8_t byte;

static struct timeval gStartTv;

static unsigned long millis( void )
{
  struct timeval curTv;

  gettimeofday( &curTv, NULL );
  double secondsDiff = ( curTv.tv_sec - gStartTv.tv_sec );
  double elapsedUs = ( ( 1000000 * secondsDiff ) - gStartTv.tv_usec ) + curTv.tv_usec;

  return elapsedUs / 1000;
}
#endif

// Definitions
#define MAX_LEDS 16
#define MAX_CHANNELS 16
#define MAX_LED_WRITE 255

// Macros
#define EXP_VALUE(v) ((double)( 5*exp((double)v/15)-1 ))
#define LFO_RATE_VALUE(v) ((double)( 5*exp((double)v/18)-1 ))
#define BR_VALUE(v) (((v+1)*2)-2)

enum MidiFunction {
  FuncADSRAttack  = 14,
  FuncADSRDecay   = 15,
  FuncADSRSustain = 16,
  FuncADSRRelease = 17,
  FuncADSRJitterRate = 18,
  FuncADSRJitterIntensity = 19,
  FuncADSRBrightnessFloor = 20,
  FuncLFORate = 21,
  FuncLFOShape = 22,
};

enum EnvelopeState {
  EnvStateADSRAttack,
  EnvStateADSRDecay,
  EnvStateADSRSustain,
  EnvStateADSRRelease,
  EnvStateADSRDone
};

enum LFOShape {
    LFOShapeTriangle
};

struct LFO
{
    bool enabled;
    double rate;
    byte shape;
};

struct ADSREnvelope
{
  bool enabled;
  byte brFloor;
  double attackMs;
  double decayMs;
  byte sustainBr; // Brightness
  double releaseMs;
  byte jitterRate;
  byte jitterIntensity;
};

struct LED
{
  bool enabled;
  unsigned long noteOnTimer;
  unsigned long noteOffTimer;
  byte brRoof;
  byte brightness;
  struct ADSREnvelope *adsr;
  enum EnvelopeState adsrState;
  int adsrBr;
  struct LFO *lfo;
  int lfoBr;
  int nextActionMs;
};

struct Channel {
  struct LED led[MAX_LEDS];
  struct ADSREnvelope adsr;
  struct LFO lfo;
};

static struct LED gLed[MAX_LEDS];
static struct Channel gChannel[MAX_CHANNELS];
static bool gWantExit = false;
byte gMapPitchToPin[MAX_LEDS]; // Value of 0 means that the PITCH isn't mapped to a PIN
static byte gLastBrightness[MAX_LEDS];

void SetupMappingTable(void)
{
  // Init everything to 0
  memset( &gMapPitchToPin, 0, sizeof( gMapPitchToPin ) );

  //              Pitch     Pin
  gMapPitchToPin[  0 ]	= 12;	//
  gMapPitchToPin[  1 ]	= 11;	//
  gMapPitchToPin[  2 ]	= 44;	//
  gMapPitchToPin[  3 ]	= 42;	//
  gMapPitchToPin[  5 ]	= 38;	//
  gMapPitchToPin[  6 ]	= 39;	//
  gMapPitchToPin[  7 ]	= 40;	//
  gMapPitchToPin[  9 ]	= 45;	//
  gMapPitchToPin[ 10 ]	= 46;	//
  gMapPitchToPin[ 11 ]	= 47;	//
  gMapPitchToPin[ 12 ]	= 48;	//
  gMapPitchToPin[ 13 ]	= 49;	//
  gMapPitchToPin[ 14 ]	= 50;	//
  gMapPitchToPin[ 15 ]	= 51;	// 
}

static void blinkLed( int count, int delayMs )
{
  int i;
  for ( i = 0; i < count; i++ ) {
   analogWrite( 12, 255 );
   delay(delayMs);
   analogWrite( 12, 0 );
   delay(delayMs);
  }
}

void HandleStartStop(void)
{
  int i;
  
#if ARDUINO_MODE
  for ( i = 0; i < MAX_LEDS; i++ ) {
    analogWrite(gMapPitchToPin[i], 0 );
  }
#endif
  memset( &gLed, 0, sizeof( gLed ) );
  for( i = 0; i < MAX_LEDS; i++ ) {
      gLed[i].adsrBr = MAX_LED_WRITE;
      gLed[i].brRoof = MAX_LED_WRITE;
  }
}

void HandleNoteOn( byte channel, byte pitch, byte velocity )
{
  // pitch is 0 based.
  if ( (pitch+1) > MAX_LEDS ) {
    return;
  }
  gLed[pitch].enabled = true;
  gLed[pitch].brRoof = BR_VALUE(velocity);
  PRINT("\nAttack State\n");
  gLed[pitch].adsrState = EnvStateADSRAttack;
  gLed[pitch].adsr = &gChannel[channel].adsr;
  gLed[pitch].lfo = &gChannel[channel].lfo;
  gLed[pitch].noteOnTimer = millis();
}

void HandleNoteOff( byte channel, byte pitch, byte velocity )
{
  //blinkLed(1, 200 );
  // pitch is 0 based.
  if ( (pitch+1) > MAX_LEDS ) {
    return;
  }

  if ( gLed[pitch].adsr and gLed[pitch].adsr->enabled ) {
    gLed[pitch].adsrState = EnvStateADSRRelease;
    gLed[pitch].noteOffTimer = millis();
    gLed[pitch].nextActionMs = 1;
  } else {
    gLed[pitch].enabled = 0;
  }
  PRINT("\nRelease State\n" );
}

void HandleControlChange( byte channel, byte number, byte value )
{
  struct Channel *chPtr;

  //  if ( number == 21 ) { // number >= 14 && number <= 18 ) { //number > 0 && number < 60 ) {
  //      blinkLed( 1, 200 );
  //  }

  chPtr = &gChannel[channel];
  PRINT("Value: %d - ", value );
  switch( number ) {
  case FuncADSRAttack:
    chPtr->adsr.enabled = true;
    chPtr->adsr.attackMs = EXP_VALUE(value);
    PRINT("Attack: %.0f ms\n", chPtr->adsr.attackMs );
    break;
  case FuncADSRDecay:
    chPtr->adsr.decayMs = EXP_VALUE(value);
    PRINT("Decay: %.0f ms\n", chPtr->adsr.decayMs );
    break;
  case FuncADSRSustain:
    chPtr->adsr.sustainBr = BR_VALUE(value);
    PRINT("Sustain: %d br\n", chPtr->adsr.sustainBr );
    break;
  case FuncADSRRelease:
    chPtr->adsr.releaseMs = EXP_VALUE(value);
    PRINT("Release: %.0f ms\n", chPtr->adsr.releaseMs );
    break;
  case FuncADSRJitterRate:
    chPtr->adsr.jitterRate = value;
    PRINT("Jitter Rate: %d\n", chPtr->adsr.jitterRate );
    break;
  case FuncADSRJitterIntensity:
    chPtr->adsr.jitterIntensity = value;
    PRINT("Jitter Intensity: %d\n", chPtr->adsr.jitterIntensity );
    break;
  case FuncADSRBrightnessFloor:
    chPtr->adsr.brFloor = BR_VALUE(value);
    PRINT("Brightness Floor: %d br\n", chPtr->adsr.brFloor );
    break;
  case FuncLFORate:
    chPtr->lfo.enabled = true;
    chPtr->lfo.rate = LFO_RATE_VALUE(value);
    PRINT("LFO Rate: %.0f ms\n", chPtr->lfo.rate );
    break;
  case FuncLFOShape:
    chPtr->lfo.shape = value;
    PRINT("LFO Shape: %d\n", chPtr->lfo.shape );
    break;
  default:
    // error
    break;
  }
}
static byte ProcessLEDEnvAttack( struct LED *led )
{
  unsigned long now = millis();
  double msElapsed = (now - led->noteOnTimer)+1;
  double brRange = ( led->brRoof - led->adsr->brFloor );

#if !ARDUINO_MODE
  static int once = false;
  if ( !once ) {
     once = true;
     PRINT("T: %lu ms\n", millis() - led->noteOnTimer );
 }
#endif

  double slice = ( led->adsr->attackMs / brRange );
  led->adsrBr = led->adsr->brFloor + ( msElapsed / slice );
  if ( led->adsrBr > led->brRoof ) {
      led->adsrBr = led->brRoof;
  }

    //PRINT("ADSR: %d\n", led->adsrBr );
  // Check if we are entering the Decay state
  if ( msElapsed > led->adsr->attackMs ) {
    PRINT("\nDecay State: %.0f - %d\n", led->adsr->decayMs, led->adsr->sustainBr );
    led->adsrState = EnvStateADSRDecay;
    led->nextActionMs = 0;
  } else {
    led->nextActionMs = slice;
  }

  return 0;
}

static byte ProcessLEDEnvDecay( struct LED *led )
{
  unsigned long now = millis();
  double msElapsed = (now - (led->noteOnTimer+led->adsr->attackMs));
  int brFactors = (led->brRoof - led->adsr->sustainBr);

#if !ARDUINO_MODE
  static int once = false;
  if ( !once ) {
     once = true;
     PRINT("T: %lu ms\n", millis() - led->noteOnTimer );
 }
#endif

  double slice = ( ( led->adsr->decayMs ) / brFactors );
  led->adsrBr = led->brRoof - ( msElapsed / slice );
  if ( led->adsrBr  < led->adsr->sustainBr ) {
    led->adsrBr = led->adsr->sustainBr;
  }

  // Check if we are entering the Decay state
  if ( msElapsed > led->adsr->attackMs+led->adsr->decayMs ) {
    led->adsrState = EnvStateADSRSustain;
    led->nextActionMs = 0;
  }
  else {
    led->nextActionMs = slice;
  }

  return 0;
}

static byte ProcessLEDEnvRelease( struct LED *led )
{
  unsigned long now = millis();
  double msElapsed = (now - led->noteOffTimer)+1;
  double brRange = led->adsr->sustainBr - led->adsr->brFloor;

#if !ARDUINO_MODE
  static int once = false;
  if ( !once ) {
     once = true;
     PRINT("T: %lu ms\n", millis() - led->noteOnTimer );
 }
#endif

  double slice = ( led->adsr->releaseMs / brRange );
  led->adsrBr = led->adsr->sustainBr - ( msElapsed / slice );
  if ( led->adsrBr < led->adsr->brFloor ) {
    led->adsrBr = led->adsr->brFloor;
  }

//    PRINT("Release: adsrBr = %d, brFloor = %d\n", led->adsrBr, led->adsr->brFloor );
  // Check if we are entering the Decay state
  if ( msElapsed > led->adsr->releaseMs ||
       led->adsrBr < led->adsr->brFloor ) {
    PRINT("\nADSR Done\n");
    PRINT("T: %lu ms\n", millis() - led->noteOnTimer );
    gWantExit = true;
    led->adsrState = EnvStateADSRDone;
    led->nextActionMs = 0;
  }
  else {
    led->nextActionMs = slice;
  }

  return 0;
}

static void ProcessADSR( struct LED *led )
{
  if ( led->adsr->enabled == false ) {
      return;
  }
  switch( led->adsrState ) {
  case EnvStateADSRAttack:
    ProcessLEDEnvAttack( led );
    break;
  case EnvStateADSRDecay:
    ProcessLEDEnvDecay( led );
    break;
  case EnvStateADSRSustain:
    PRINT("\nSustain State\n" );
    PRINT("T: %lu ms\n", millis() - led->noteOnTimer );
    HandleNoteOff( 0, 0, 0 );
    break;
  case EnvStateADSRRelease:
    ProcessLEDEnvRelease( led );
    break;
  case EnvStateADSRDone:
    led->enabled = false;
    HandleNoteOn( 0, 0, 127 );
    break;
  }

  if ( led->adsrBr < led->adsr->brFloor ) {
      led->adsrBr = led->adsr->brFloor;
  }
  led->brightness = led->adsrBr;
  //PRINT("ADSR: %d\n", led->adsrBr );
}

static void ProcessLFO( struct LED *led )
{
    if ( led->lfo->enabled == false ) {
        return;
    }
    unsigned long now = millis();
    double msElapsed = (now - led->noteOnTimer)+1;
    double brRange = led->adsrBr - led->adsr->brFloor;

    if ( brRange == 0 ) {
        led->lfoBr = 0;
        //PRINT( "LFO: %d  ADSR: %d   BR Range: %.0f\n", led->lfoBr, led->adsrBr, brRange );
        led->brightness = 0;
        return;
    }
    int mod = msElapsed / led->lfo->rate;
    double lfoElapsed = msElapsed - ( mod * led->lfo->rate );
    double halfRate = led->lfo->rate / 2;

    //PRINT("LFO Elapsed: %.0f  Half Rate: %.0f\n", lfoElapsed, halfRate );
    if ( lfoElapsed > halfRate ) {
        led->lfoBr = ( ( halfRate - ( lfoElapsed - halfRate ) ) / ( led->lfo->rate / brRange ) * 2 );
    } else {
        led->lfoBr = ( lfoElapsed / ( led->lfo->rate / brRange ) * 2 );
    }

    led->lfoBr += led->adsr->brFloor;

    if ( led->lfoBr > led->adsrBr ) {
        led->lfoBr = led->adsrBr;
    } else if ( led->lfoBr < led->adsr->brFloor ) {
        led->lfoBr = led->adsr->brFloor;
    }
    //PRINT( "LFO Elapsed: %d %f %f\n", mod, led->lfo->rate, lfoElapsed );
    //PRINT( "%f: LFO: %d  ADSR: %d   BR Range: %.0f\n", lfoElapsed, led->lfoBr, led->adsrBr, brRange );
    
    led->brightness = led->lfoBr;
}

void InitDebugMidiCmds(void)
{
  int channel = 0;

  HandleStartStop();

  // ADSR
  HandleControlChange( channel, FuncADSRAttack, 70 );
  HandleControlChange( channel, FuncADSRDecay, 90 );
  HandleControlChange( channel, FuncADSRSustain, 50 );
  HandleControlChange( channel, FuncADSRRelease, 100 );
  HandleControlChange( channel, FuncADSRJitterRate, 0 );
  HandleControlChange( channel, FuncADSRJitterIntensity, 0 );
  HandleControlChange( channel, FuncADSRBrightnessFloor, 10 );

  // LFO
  HandleControlChange( channel, FuncLFORate, 80 );

  HandleNoteOn( channel, 0, 127 );
}

#if !ARDUINO_MODE
void OutputSimulator(void)
{
  int i;
  while ( !gWantExit ) {
    for ( i = 0; i < MAX_LEDS; i++ ) {
      if ( !gLed[i].enabled ) {
        continue;
      }
      ProcessADSR( &gLed[i] );
      ProcessLFO( &gLed[i] );
# if PRINT_WAVEFORM
        int j;
        for ( j = 0; j <  gLed[i].brightness/2; j++ ) {
            PRINT(" ");
        }
        PRINT("*\n");
# endif
      if ( gWantExit ) {
        break;
      }
      usleep( 1000 );
    }
  }
}


int main(void)
{
  memset( &gChannel, 0, sizeof( gChannel ) );

  gettimeofday( &gStartTv, NULL );

# if 0
    double msElased = 2500;
    int mod = msElased / 1000;
    PRINT("Mod: %d\n", mod );
    PRINT("Val: %f\n", msElased - ( mod * 1000  ) );
    return 0;
# endif
#if 0
  double value;
  for ( value = 0; value < 127; value++ ) {
      //PRINT( "%f: %f\n", value, EXP_VALUE(value) );
      PRINT( "%f: %f\n", value, LFO_RATE_VALUE(value) );
  }
    return 0;
#endif

  InitDebugMidiCmds();
  OutputSimulator();

  return 0;
}
#endif

#if ARDUINO_MODE
void setup( void )
{
  memset( &gChannel, 0, sizeof( gChannel ) );
  memset( &gLed, 0, sizeof( gLed ) );
  SetupMappingTable();

  MIDI.begin( MIDI_CHANNEL_OMNI );
  MIDI.setHandleNoteOn ( HandleNoteOn       );
  MIDI.setHandleNoteOff( HandleNoteOff      );
  MIDI.setHandleStop   ( HandleStartStop    );
  MIDI.setHandleStart  ( HandleStartStop    );
  MIDI.setHandleControlChange( HandleControlChange );
  //InitDebugMidiCmds();
  
  HandleStartStop();
}

void loop( void )
{
  
  int i;
#if 0

for ( i = 0; i < 255; i++ ) {
analogWrite( 12, i );   
delay( 5 );
}
return;
#endif
#if 0
if ( gLed[0].enabled ) {
    blinkLed( 10, 100 );
} else {
  analogWrite( gMapPitchToPin[0], 0 );
}
return;
#endif  
  //int i;

  MIDI.read();

  for ( i = 0; i < MAX_LEDS; i++ ) {
    if ( !gLed[i].enabled ) {
      continue;
    }
    ProcessADSR( &gLed[i] );
    ProcessLFO( &gLed[i] );
    // Only change the brightness if it has changed
    if ( gLastBrightness[i] != gLed[i].brightness ) {
        analogWrite( gMapPitchToPin[i],  gLed[i].brightness );
    }
    gLastBrightness[i] = gLed[i].brightness;
  }
  delayMicroseconds(1001);
}

#endif

