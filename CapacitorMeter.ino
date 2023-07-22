//Capacitor meter using 555 timer plus charge timer for larger capacitors
//R.J.Tidey 27 Sep 2020
//Note text co-ordinates are by pixel in x direction and by 8 pixels in Y direction
#include <Arduino.h>
#include <ssd1306BB.h>

#define SETCLK_8MHz
// non zero values will update OSCVAL to get better clock accuracy
#define OSCCAL_VAL 150
//if this is defined then uses asm for adc interrupt routine
//reduces push/pop for normal path
//isr time goes down from ~10uS to 4uS
#define ADCISR_ASM

#define TIMING_ADC		0		// PB5	
#define SSD1306_SDA		0		// PB0 SDA 
#define FASTCHARGE_PIN	1		// PB1
#define SSD1306_SCL		3		// PB3 SCL
#define DISCHARGE_PIN	4		// PB4

#define SSDI2C_DELAY	4		// sets i2c speed
#define SSD1306_SA		0X3C	// Slave address

#define BLANK "                "
#define BASE_CAP (820 + 25)

// 100000000/(R*0.69315) time constant to 50%
#define CHARGE_RCLOW		6573 
#define CHARGE_RCHIGH		310255
 
#define BUTTON_LONG			1000

#define STATE_STARTUP		0
#define STATE_BEGIN			1
#define STATE_MEASURE555	2
#define STATE_DISCHARGE1	3
#define STATE_MEASURECHARGE	4
#define STATE_DISCHARGE2	5
#define STATE_CAL			6
#define STATE_END			7

#define START_DELAY			1000000ul

#define MEASURECOUNT_CAL	7
#define MEASUREMODE_555		0
#define MEASUREMODE_CHLOW	1
#define MEASUREMODE_CHHIGH	2
//time (ms) to allow capacitor to charge to detect mode needed
#define MEASUREMODE_DETECT	40

// timeout for 555 period greater than this (100mS)
#define PULSE_TIMEOUT		100000ul
//switch fast mode off if charge time less than this 
#define MEASURE_INTERVAL555		1000000ul

#define ADCSRA_INIT 0x24  // DISABLED | ADATE | 16 prescale 
#define ADCSRA_STARTFREE 0xe4
#define ADCSRA_STARTFREEINT 0xec
#define ADCSRA_STARTSINGLE 0xc4

char valString[12];
unsigned long measureTime;
unsigned long startTime;
unsigned long measureInterval[3] = {1000000ul,5000000ul,25000000ul};
unsigned long measureActual;;
int measureCount = 0;
uint8_t state = STATE_STARTUP;
uint8_t dischargeState;
unsigned long countT0;
unsigned long basecountT0;
uint16_t multiplier2;
uint16_t overflowT0;
volatile uint8_t tcnt0, tcnt1;
unsigned long capValue;
//measuring mode
uint8_t measureMode = MEASUREMODE_CHLOW;
uint8_t chargeFast = 0;
volatile uint8_t chargeThreshold = 128;
uint16_t buttonCount = 0;
uint8_t clockCheck = 0;

uint8_t testState = 0;

// Use ADC to detect capacitor charging crossing a threshold
ISR(ADC_vect) {
#ifdef ADCISR_ASM
	asm(
	"push r24 \n"
	"push r25 \n"
	"in r25, 0x05 \n"
	"lds r24, (chargeThreshold) \n"
	"cp r25, r24 \n"
	"brcs 1f \n"
	"push r18 \n"
	"push r19 \n"
	"push r20 \n"
	"push r21 \n"
	"push r22 \n"
	"push r23 \n"
	"push r26 \n"
	"push r27 \n"
	"push r30 \n"
	"push r31 \n"
	"rcall micros \n"
	"sts 0x00B2, r22 \n"
	"sts 0x00B3, r23 \n"
	"sts 0x00B4, r24 \n"
	"sts 0x00B5, r25 \n"
	"ldi r24, 0x24 \n"
	"out 0x06, r24 \n"
	"pop r31 \n"
	"pop r30 \n"
	"pop r27 \n"
	"pop r26 \n"
	"pop r23 \n"
	"pop r22 \n"
	"pop r21 \n"
	"pop r20 \n"
	"pop r19 \n"
	"pop r18 \n"
	"1: \n"
	"pop r25 \n"
	"pop r24 \n"
	: 
	: 
	:);
#else
	if(ADCH >= chargeThreshold) {
		measureActual = micros();
		ADCSRA = ADCSRA_INIT; //stop ADC
	}
#endif
}

ISR(TIM0_OVF_vect) {
	overflowT0++;
}

void capValue555() {
	//capValue in units of 0.1pF
	countT0 = (countT0 * (measureActual / 10) ) / (measureInterval[0] / 10);
	capValue = ((basecountT0 * BASE_CAP * 10ul)  / countT0) * multiplier2 - BASE_CAP * 10ul;
}

void capValueCharge() {
	//capValue in units of 1nF
	if(measureMode == MEASUREMODE_CHLOW)
		capValue = measureActual * CHARGE_RCLOW;
	else
		capValue = measureActual * CHARGE_RCHIGH;
	capValue = (capValue / 100000) * multiplier2;
}

void strCapValue() {
	uint8_t len, dpos, dpl, i;
	char prefix;
	
	ultoa(capValue, valString, 10);
	len = strlen(valString);
	if(measureMode == MEASUREMODE_555) {
		if(len > 4) {
			prefix = 'n';
			dpos = len - 4;
			dpl = 3;
		} else {
			prefix = 'p';
			dpos = len - 1;
			dpl = 1;
		}
	} else {
		prefix = 'u';
		if(len < 7) {
			dpos = len - 3;
			dpl = 3;
		} else {
			dpl = len+1;
		}
		
	}
	if(dpl <= len) { 	//make space for dec point
		for(i=len; i > dpos; i--) {
			valString[i] = valString[i-1];
		}
		valString[dpos] = '.';
		i = dpos + dpl + 1;
	} else {
		i = len;
	}
	valString[i++] = ' ';
	valString[i++] = prefix;
	valString[i++] = 'F';
	valString[i] = 0;
}

void displayInit() {
	SSD1306.ssd1306_init(SSD1306_SDA, SSD1306_SCL, SSD1306_SA, SSDI2C_DELAY);
	delay(200);
	SSD1306.ssd1306_fillscreen(0);
	SSD1306.ssd1306_string(0,0," Capacitor");
	SSD1306.ssd1306_string(0,2,"   Meter");
	SSD1306.ssd1306_string(0,6,BLANK);
}

void displayValues(int state) {
	if(state == STATE_CAL) {
		SSD1306.ssd1306_string(0,4, "Calib");
		itoa(measureCount, valString, 10);
		SSD1306.ssd1306_string(96,4, valString);
	} else {
		SSD1306.ssd1306_string(0,4,BLANK);
		if(measureMode == MEASUREMODE_555) {
			if(basecountT0 && (countT0) < basecountT0) {
				strCapValue();
			} else {
				valString[0] = '?';
				valString[1] = 0;
			}
		} else {
			strCapValue();
		}
		SSD1306.ssd1306_string(0,4,valString);
	}
}

void startMeasure() {
	unsigned long microsTemp;
	if(measureMode == MEASUREMODE_555) {
		// frequency measure from 555
		TCCR0A = 0; //Timer0_SetDefaults
		OCR0A = 0;
		OCR0B = 0;
		TCNT0 = 0;
		overflowT0 = 0;
		TIMSK |= (1<<TOIE0); //Timer0_EnableOverflowInterrupt
		TCCR0B = 7; //Timer0_T0_Rising
		microsTemp = micros() + PULSE_TIMEOUT;
		//detect real edge on signal for accuracy on lower frequencies
		do {
			tcnt0 = TCNT0;
		} while(tcnt0 == 0 && (micros() < microsTemp));
		if(tcnt0) {
			measureTime = micros();
			state = STATE_MEASURE555;
		} else {
			state = STATE_BEGIN;
		}
	} else {
		// start up ADC measurements to check discharge
		ADCSRA = ADCSRA_STARTFREE;
		//remove fast charge and enable discharge
		digitalWrite(FASTCHARGE_PIN, 1);
		digitalWrite(DISCHARGE_PIN, 1);
		state = STATE_DISCHARGE1;
	}
}

void endMeasure() {
	if(measureMode == MEASUREMODE_555) {
		tcnt1 = TCNT0;
		measureActual = micros() + PULSE_TIMEOUT;
		//detect real edge on signal for accuracy on lower frequencies
		do {
		} while(tcnt1 == TCNT0 && (micros() < measureActual));
		if(tcnt1 != TCNT0) {
			measureActual = micros() - measureTime;
			TCCR0B = 0; //Timer0_Stopped
			TIMSK &= ~(1<<TOIE0); //Timer0_DisableOverflowInterrupt
			countT0 = (256ul * overflowT0 + tcnt1 - tcnt0);
			//Scale the count to prevent overflows in calculation
			multiplier2 = 1;
			while(countT0 < 8000) {
				countT0 <<= 1;
				multiplier2 <<= 1;
			}
			capValue555();
		}
	} else {
		TIMSK &= ~(1<<TOIE0); //Timer0_DisableOverflowInterrupt
		digitalWrite(FASTCHARGE_PIN,1); //fast charge off
		digitalWrite(DISCHARGE_PIN,1); //discharge on
		measureActual = micros() - measureTime;
		//SSD1306.ssd1306_string(0,6,BLANK);		
		//ultoa(measureActual,valString,10);
		//SSD1306.ssd1306_string(0,6,valString);		
		if(ADCSRA == ADCSRA_INIT){
			//Scale the time to prevent overflows in calculation
			multiplier2 = 1;
			while(measureActual > 8000) {
				measureActual >>= 1;
				multiplier2 <<= 1;
			}
			capValueCharge();
			dischargeState = STATE_END;
			ADCSRA = ADCSRA_STARTFREE;
		} else {
			dischargeState = STATE_BEGIN;
			ADCSRA = ADCSRA_STARTFREE;
		}
		state = STATE_DISCHARGE2;
	}
}

void calibrate() {
	if(measureCount == 0) {
		basecountT0 = 0;
	}
	if(measureCount >= (MEASURECOUNT_CAL - 4)) {
		basecountT0 = basecountT0 + countT0;
	}
	if(measureCount == (MEASURECOUNT_CAL - 1)) {
		basecountT0 >>= 2;
		if(clockCheck) {
			ultoa(basecountT0, valString,10);
			SSD1306.ssd1306_string(0,6,valString);
			itoa(OSCCAL,valString,10);
			SSD1306.ssd1306_string(84,6,valString);
		} else {
			SSD1306.ssd1306_string(0,6,BLANK);
		}
	}
	measureCount++;
}

uint8_t getMeasureMode() {
	uint8_t mm;
	if(measureCount < MEASURECOUNT_CAL) {
		mm = MEASUREMODE_555;
	} else {
		digitalWrite(FASTCHARGE_PIN, 1); //fast charge off
		digitalWrite(DISCHARGE_PIN, 0); // start charging
		// wait to allow a bit of capacitor charging
		delay(MEASUREMODE_DETECT);
		ADCSRA = ADCSRA_STARTSINGLE;
		delay(1);
		if(ADCH > 250) {
			// no Cap  detected on charge timing pin use 555
			mm = MEASUREMODE_555;
		} else if(ADCH > 10) {
			// medium capacitor use low rate charging
			mm = MEASUREMODE_CHLOW;
		} else {
			// large capacitor use high rate charging
			mm = MEASUREMODE_CHHIGH;
		}
	}
	digitalWrite(DISCHARGE_PIN, 1); // discharge on
	delay(100);
	ADCSRA = ADCSRA_INIT;
	return mm;
}

void stateMachine() {
	switch(state) {
		case STATE_STARTUP:	if((micros() - startTime) >= START_DELAY) {
								SSD1306.ssd1306_init(SSD1306_SDA, SSD1306_SCL, SSD1306_SA, SSDI2C_DELAY);
								delay(500);
								displayInit();
								state = STATE_BEGIN;
							}
							break;
		case STATE_BEGIN:	
							measureMode = getMeasureMode();
							startMeasure();
							break;
		case STATE_MEASURE555:	
							if(micros() - measureTime >= measureInterval[0]) {
								endMeasure();
								if(measureCount < MEASURECOUNT_CAL) {
									calibrate();
									state = STATE_CAL;
								} else {
									state = STATE_END;
								}
							}
							break;
		case STATE_DISCHARGE1 :	
								if(ADCH == 0) {
									ADCSRA = ADCSRA_STARTFREEINT;
									digitalWrite(FASTCHARGE_PIN, measureMode == MEASUREMODE_CHHIGH ? 0 : 1);
									measureTime = micros();
									// release discharge
									digitalWrite(DISCHARGE_PIN, 0);
									state = STATE_MEASURECHARGE;
								}
							break;
		case STATE_MEASURECHARGE : 
							if((ADCSRA == ADCSRA_INIT) || (micros() - measureTime >= measureInterval[measureMode])) {
								endMeasure();
							}
							break;
		case STATE_DISCHARGE2 :	
							if(ADCH == 0) {
								ADCSRA = ADCSRA_INIT;
								state = dischargeState;
							}
							break;
		case STATE_CAL:
		case STATE_END:		
							// enforce minimum interval between measurements
							if(micros() - measureTime >= measureInterval[0]) {
								displayValues(state);
								state = STATE_BEGIN;
							}
							break;
	}
}

void checkButton() {
	if(digitalRead(SSD1306_SCL)) {
		if(buttonCount > BUTTON_LONG) {
			// initiate calibrate
			state = STATE_BEGIN;
			measureCount = 0;
			clockCheck = ~clockCheck;
		}
		buttonCount = 0;
	} else {
		buttonCount++;
	}
}

void setup() {
#ifdef SETCLK_8MHz
	cli(); // Disable interrupts
	CLKPR = (1<<CLKPCE); // Prescaler enable
	CLKPR = 1; // Clock division factor 2 8MHz
	TCCR1 = (TCCR1 & 0xF0) | 6; // timer1 prescale 32 to keep 4uS ticks
	sei(); // Enable interrupts	int i;
#endif
	if(OSCCAL_VAL > 0) OSCCAL = OSCCAL_VAL;
	digitalWrite(FASTCHARGE_PIN,1); //fast charge off
	pinMode(FASTCHARGE_PIN, OUTPUT);
	digitalWrite(DISCHARGE_PIN,1); //discharge on
	pinMode(DISCHARGE_PIN, OUTPUT);
	ADCSRA = ADCSRA_INIT;
	ADCSRB = 0x00; // FreeRun
	ADMUX = 0x20; // ADLAR ADC0 Vcc Reference
	startTime = micros();
}

void loop() {
	stateMachine();
	checkButton();
	delay(1);
}
