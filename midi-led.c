#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>


#define ARDUINO_MODE 0

#if !ARDUINO_MODE
#include <sys/time.h>
#include <unistd.h>
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

#define MAX_LEDS 16
#define MAX_CHANNELS 16

#define MAX_LED_WRITE 255

#define EXP_VALUE(v) ((double)( 5*exp((double)v/15)-1 ))

#define BR_VALUE(v) (((v+1)*2)-2)

typedef uint8_t byte;
/*typedef int8_t bool;
#define true 1
#define false 1*/

enum MidiFunction {
    FuncADSRAttack  = 0,
    FuncADSRDecay   = 1,
    FuncADSRSustain = 2,
    FuncADSRRelease = 3,
    FuncADSRJitterRate = 4,
    FuncADSRJitterIntensity = 5,
    FuncADSRBrightnessFloor = 6,
};

enum EnvelopeState {
    EnvStateADSRAttack,
    EnvStateADSRDecay,
    EnvStateADSRSustain,
    EnvStateADSRRelease,
    EnvStateADSRDone
};

struct LFO
{
    byte rate;
    byte shape;
};

struct ADSREnvelope
{
    byte brightnessFloor;
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
    byte brightnessRoof;
    byte brightness;
    struct ADSREnvelope *adsr;
    int adsrBr;
    struct LFO *lfo;
    enum EnvelopeState adsrState;
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

void HandleNoteOn( byte channel, byte pitch, byte velocity )
{
    // pitch is 0 based.
    if ( (pitch+1) > MAX_LEDS ) {
        return;
    }
    memset( &gLed[pitch], 0, sizeof( gLed[pitch] ) );
    gLed[pitch].enabled = true;
    gLed[pitch].brightnessRoof = BR_VALUE(velocity);
    gLed[pitch].adsrState = EnvStateADSRAttack;
    gLed[pitch].adsr = &gChannel[channel].adsr;
    gLed[pitch].lfo = &gChannel[channel].lfo;
    gLed[pitch].noteOnTimer = millis();
}

void HandleNoteOff( byte channel, byte pitch, byte velocity )
{
    // pitch is 0 based.
    if ( (pitch+1) > MAX_LEDS ) {
        return;
    }

    gLed[pitch].adsrState = EnvStateADSRRelease;
    gLed[pitch].noteOffTimer = millis();
    gLed[pitch].nextActionMs = 1;
    printf("\nRelease State\n" );
}

void HandleControlChange( byte channel, byte number, byte value )
{
    struct Channel *chPtr;

    chPtr = &gChannel[channel];
    printf("Value: %d - ", value );
    switch( number ) {
        case FuncADSRAttack:
            chPtr->adsr.attackMs = EXP_VALUE(value);
            printf("Attack: %.0f ms\n", chPtr->adsr.attackMs );
            break;
        case FuncADSRDecay:
            chPtr->adsr.decayMs = EXP_VALUE(value);
            printf("Decay: %.0f ms\n", chPtr->adsr.decayMs );
            break;
        case FuncADSRSustain:
            chPtr->adsr.sustainBr = BR_VALUE(value);
            printf("Sustain: %d br\n", chPtr->adsr.sustainBr );
            break;
        case FuncADSRRelease:
            chPtr->adsr.releaseMs = EXP_VALUE(value);
            printf("Release: %.0f ms\n", chPtr->adsr.releaseMs );
            break;
        case FuncADSRJitterRate:
            chPtr->adsr.jitterRate = value;
            printf("Jitter Rate: %d\n", chPtr->adsr.jitterRate );
            break;
        case FuncADSRJitterIntensity:
            chPtr->adsr.jitterIntensity = value;
            printf("Jitter Intensity: %d\n", chPtr->adsr.jitterIntensity );
            break;
        case FuncADSRBrightnessFloor:
            chPtr->adsr.brightnessFloor = BR_VALUE(value);
            printf("Brightness Floor: %d br\n", chPtr->adsr.brightnessFloor );
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

    double slice = ( led->adsr->attackMs / led->brightnessRoof );
    led->adsrBr = ( msElapsed / slice );
    if ( led->adsrBr > led->brightnessRoof ) {
        led->adsrBr = led->brightnessRoof;
    }

    // Check if we are entering the Decay state
    if ( msElapsed > led->adsr->attackMs ) {
        printf("\nDecay State: %.0f - %d\n", led->adsr->decayMs, led->adsr->sustainBr );
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
    int brFactors = (led->brightnessRoof - led->adsr->sustainBr);

    double slice = ( ( led->adsr->decayMs ) / brFactors );
    led->adsrBr = led->brightnessRoof - ( msElapsed / slice );
    if ( led->adsrBr  < led->adsr->sustainBr ) {
        led->adsrBr = led->adsr->sustainBr;
    }

    // Check if we are entering the Decay state
    if ( msElapsed > led->adsr->attackMs+led->adsr->decayMs ) {
        printf("\nSustain State\n" );
        led->adsrState = EnvStateADSRSustain;
        led->nextActionMs = 0;
    } else {
        led->nextActionMs = slice;
    }

    return 0;
}

static byte ProcessLEDEnvRelease( struct LED *led )
{
    unsigned long now = millis();
    double msElapsed = (now - led->noteOffTimer)+1;

    double slice = ( led->adsr->releaseMs / ( led->adsr->sustainBr - led->adsr->brightnessFloor ) );
    led->adsrBr = led->adsr->sustainBr - ( msElapsed / slice );
    if ( led->adsrBr < led->adsr->brightnessFloor ) {
        led->adsrBr = led->adsr->brightnessFloor;
    }

    // Check if we are entering the Decay state
    if ( msElapsed > led->adsr->releaseMs ) {
        printf("\nADSR Done\n");
        gWantExit = true;
        led->adsrState = EnvStateADSRDone;
        led->nextActionMs = 0;
    } else {
        led->nextActionMs = slice;
    }

    return 0;
}

static void ProcessLED( struct LED *led )
{
    switch( led->adsrState ) {
        case EnvStateADSRAttack:
            ProcessLEDEnvAttack( led );
            break;
        case EnvStateADSRDecay:
            ProcessLEDEnvDecay( led );
            break;
        case EnvStateADSRSustain:
            HandleNoteOff( 0, 0, 0 );
            break;
        case EnvStateADSRRelease:
            ProcessLEDEnvRelease( led );
            break;
        case EnvStateADSRDone:
            led->enabled = false;
            break;
    }
    //printf("ADSR: %d\n", led->adsrBr );
}

void OutputSimulator(void)
{
    int i;
    while ( !gWantExit ) {
        for ( i = 0; i < MAX_LEDS; i++ ) {
            if ( !gLed[i].enabled ) {
                continue;
            }
            ProcessLED( &gLed[i] );
            if ( gWantExit ) {
                break;
            }
#if ARDUINO_MODE
            delayMilli(1);
#else
            usleep( 1000 );
#endif
        }
    }
}

void InitDebugMidiCmds(void)
{
    int channel = 0;

    HandleNoteOn( channel, 0, 127 );
    HandleControlChange( channel, FuncADSRAttack, 70 );
    HandleControlChange( channel, FuncADSRDecay, 90 );
    HandleControlChange( channel, FuncADSRSustain, 50 );
    HandleControlChange( channel, FuncADSRRelease, 100 );
    HandleControlChange( channel, FuncADSRJitterRate, 0 );
    HandleControlChange( channel, FuncADSRJitterIntensity, 0 );
    HandleControlChange( channel, FuncADSRBrightnessFloor, 0 );
}

int main(void)
{
    memset( &gChannel, 0, sizeof( gChannel ) );

#if !ARDUINO_MODE
    gettimeofday( &gStartTv, NULL );
#endif

#if 0
    double value;
    for ( value = 0; value < 127; value++ ) {
        printf( "%f: %f\n", value, EXP_VALUE(value) );
    }
    return 0;
#endif

    InitDebugMidiCmds();

    OutputSimulator();

    return 0;
}
