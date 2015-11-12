/*
The MIT License (MIT)

Copyright (c) 2015 silverx

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <inttypes.h>
#include <math.h>

#include "pid.h"
#include "config.h"
#include "util.h"
#include "drv_pwm.h"
#include "control.h"
#include "defines.h"
#include "drv_time.h"

#include "sixaxis.h"

extern float rx[4];
extern float gyro[3];
extern int failsafe;
extern float pidoutput[3];

extern char auxchange[AUXNUMBER];
extern char aux[AUXNUMBER];

extern float looptime;
extern float attitude[3];

int onground = 1;
float pwmsum;
float thrsum;

float error[PIDNUMBER];
float motormap( float input);
int lastchange;
int pulse;
float yawangle;
float angleerror[3];

extern float apid(int x );
extern void imu_calc( void);
extern void savecal( void);

void motorcontrol(void);
int gestures(void);
void pid_precalc( void);

void control( void)
{

	// hi rates
	float ratemulti;
	float ratemultiyaw;
	float maxangle;
	float anglerate;
	
	if ( aux[RATES] ) 
	{
		ratemulti = HIRATEMULTI;
		ratemultiyaw = HIRATEMULTIYAW;
		maxangle = MAX_ANGLE_HI;
		anglerate = LEVEL_MAX_RATE_HI;
	}
	else 
	{
		ratemulti = 1.0f;
		ratemultiyaw = 1.0f;
		maxangle = MAX_ANGLE_LO;
		anglerate = LEVEL_MAX_RATE_LO;
	}

	
	yawangle = yawangle + gyro[2]*looptime;

	if ( auxchange[HEADLESSMODE] )
	{
		yawangle = 0;
	}
	
	if ( aux[HEADLESSMODE]&&!aux[LEVELMODE] ) 
	{
		float temp = rx[0];
		rx[0] = rx[0] * cosf( yawangle) - rx[1] * sinf(yawangle );
		rx[1] = rx[1] * cosf( yawangle) + temp * sinf(yawangle ) ;
	}
	
// check for acc calibration
if (gestures()==1 )
{
	gyro_cal(); // for flashing lights
	acc_cal();
  savecal();
	// reset loop time 
	extern unsigned lastlooptime;
	lastlooptime = gettime();
}

imu_calc();

pid_precalc();

	if ( aux[LEVELMODE] ) 
	{// level mode

	angleerror[0] = rx[0] * maxangle - attitude[0];
	angleerror[1] = rx[1] * maxangle - attitude[1];

	error[0] = apid(0) * anglerate * DEGTORAD  - gyro[0];
	error[1] = apid(1) * anglerate * DEGTORAD  - gyro[1];	 

	}
else
{ // rate mode
	error[0] = rx[0] * MAX_RATE * DEGTORAD * ratemulti - gyro[0];
	error[1] = rx[1] * MAX_RATE * DEGTORAD * ratemulti - gyro[1];
	
	// reduce angle Iterm towards zero
	extern float aierror[3];
	for ( int i = 0 ; i <= 3 ; i++) aierror[i] *= 0.8f;
}	

error[2] = rx[2] * MAX_RATEYAW * DEGTORAD * ratemultiyaw - gyro[2];

	pid(0);
	pid(1);
	pid(2);

 motorcontrol();
	
}


void motorcontrol(void)
{	
// map throttle so under 10% it is zero	
float	throttle = mapf(rx[3], 0 , 1 , -0.1 , 1 );
if ( throttle < 0   ) throttle = 0;

// turn motors off if throttle is off and pitch / roll sticks are centered
	if ( failsafe || (throttle < 0.001f && (!ENABLESTIX||  (fabs(rx[0]) < 0.5f && fabs(rx[1]) < 0.5f ) ) ) ) 

	{ // motors off
		onground = 1;
		pwmsum = 0;
		thrsum = 0;
		for ( int i = 0 ; i <= 3 ; i++)
		{
			pwm_set( i , 0 );
		}	
	}
	else
	{
		onground = 0;
		float mix[4];	
		
//		pidoutput[2] += motorchange;
		
		mix[MOTOR_FR] = throttle - pidoutput[0] - pidoutput[1] + pidoutput[2];		// FR
		mix[MOTOR_FL] = throttle + pidoutput[0] - pidoutput[1] - pidoutput[2];		// FL	
		mix[MOTOR_BR] = throttle - pidoutput[0] + pidoutput[1] - pidoutput[2];		// BR
		mix[MOTOR_BL] = throttle + pidoutput[0] + pidoutput[1] + pidoutput[2];		// BL	
			
		
		for ( int i = 0 ; i <= 3 ; i++)
		{
		float test = motormap( mix[i] );
		#ifndef NOMOTORS
		pwm_set( i , ( test )  );
		#endif
		}	

		for ( int i = 0 ; i <= 3 ; i++)
		{
			if ( mix[i] < 0 ) mix[i] = 0;
			if ( mix[i] > 1 ) mix[i] = 1;
			thrsum+= mix[i];
		}	
		thrsum = thrsum / 4;
		
	}// end motors on
	
}
	
/*
float motormap_old( float input)
{ 
	// this is a thrust to pwm function
	//  float 0 to 1 input and output
	// reverse of a power to thrust graph for 8.5 mm coreless motors + hubsan prop
	// should be ok for other motors without reduction gears.
	// a*x^2 + b*x + c
	// a = 0.75 , b = 0.061 , c = 0.185

if (input > 1.0) input = 1.0;
if (input < 0) input = 0;
	
if ( input < 0.25 ) return input;

input = input*input*0.75  + input*(0.0637);
input += 0.185;

return input;   
}
*/

float motormap( float input)
{ 
	// this is a thrust to pwm function
	//  float 0 to 1 input and output
	// output can go negative slightly
	// measured eachine motors and prop, stock battery
	// a*x^2 + b*x + c
	// a = 0.262 , b = 0.771 , c = -0.0258

if (input > 1) input = 1;
if (input < 0) input = 0;

input = input*input*0.262f  + input*(0.771f);
input += -0.0258f;

return input;   
}

static unsigned gesturetime;

int gestures()
{
	if ( aux[LEVELMODE] && rx[3] < 0.1f )
	{
		if ( rx[2] < -0.9f && fabs(rx[1]) < 0.2f && fabs(rx[1]) < 0.2f   )
		{
			
			if ( gesturetime == 0 ) 
					gesturetime = gettime();
			else
			{
				// if waited more then 5sec
				if ( gettime() - gesturetime > 5e6) return 1;
			}
			
		}
		else
		{
			gesturetime = 0;
		}
		
	}else
	{
			gesturetime = 0;
	}
	
	
	return 0;
}




