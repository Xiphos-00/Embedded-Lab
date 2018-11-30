#include "gpiolib_addr.h"
#include "gpiolib_reg.h"
#include "gpiolib_addr.h"
#include "gpiolib_pinmd.h"
#include "gpiolib_pinfuncs.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

int setPin(GPIO_Handle gpio, int pin, int mode){
    //Pin was out of bounds
    if(pin < PINMIN || pin > PINMAX){
        printf("pin %i was out of range (should be [0,53])\n",pin);
        return -1;
    }

    //The gpfsel register we want to write to is calculated
    int selReg = pin / GPFSELPINS;

    //The location of the last bit in the 3-bit pin field is calculated
    int lastBit = ((pin - (GPFSELPINS*(pin/GPFSELPINS)))*GPFSELMODE_SIZE);

    //Input and Output modes are 1 bit changes
    if(mode == INPUT){
        //Read the register value
        uint32_t regValue = gpiolib_read_reg(gpio, GPFSEL(selReg));

        //Check if we actually need to change something (if the field is not 000)
        if((regValue >> lastBit & 0b111) != 1){
            //The field is non-zero, so we must do something, otherwise we don't
            //We must & the register with 0001111111[...]
            int mask = 0b000 << (lastBit + 2);
            mask |= (int)(pow(2,lastBit)-1); //now we have 00011111[...]
            regValue &= mask;
        }
    }
    else if(mode == OUTPUT){
        //Read the register value
        uint32_t regValue = gpiolib_read_reg(gpio, GPFSEL(selReg));
        
        //output what is going on
        fprintf(stderr, "GPFSEL(%i) value before setting pin %i: %i\n", selReg,pin, regValue);

        /////////////////////////////////
        //int mask = 0b001 << (lastBit + 2);
        //mask |= (int)(pow(2,lastBit)-1); //now we have 001[111111...]
        //
        //set this bit to 1 (001 is the field)
        //regValue &= mask;
        ////////////////////////////

        int mask = 1 << lastBit;
        regValue |= mask;

        //output what is going on
        fprintf(stderr, "GPFSEL(%i) value after setting pin %i: %i, shifted 1 by %i\n", selReg, pin, regValue, lastBit);
        
        //Write back the value
        gpiolib_write_reg(gpio, GPFSEL(selReg), regValue);
    }
    //Leave room for future alternate pinmodes
    else{
        fprintf(stderr, "Invalid mode given for pin %i\n", pin);
        return -1;
    }

    return 0;
}
int writePin(GPIO_Handle gpio, int pin, int data){   
    //The pin was out of bounds
    if(pin < PINMIN || pin > PINMAX){
        fprintf(stderr, "pin %i was out of range (should be [0,53])\n", pin);
        return -1;
    }

    //The GPSET/GPCLR register is calculated
    int reg = pin / GPSETCLR_SIZE;

    //The bit position is calculated
    int bitPos = pin - (GPSETCLR_SIZE*(pin / GPSETCLR_SIZE));

    //If we are writing a 0, write to the clr register
    if(data == LO){
        fprintf(stderr, "wrote 1 to GPCLR(%i) bit position %i\n", reg, bitPos);
        gpiolib_write_reg(gpio, GPCLR(reg), 1 << bitPos);
    }
    //If we are writing a 1, write to the set register
    else if(data == HI){
        fprintf(stderr, "wrote 1 to GPSET(%i) bit position %i\n", reg, bitPos);
        gpiolib_write_reg(gpio, GPSET(reg), 1 << bitPos);
    }
    //Something went wrong
    else{
        fprintf(stderr, "Invalid data was written to pin %i\n", pin);
        return -1;
    }
    return 0;
}
int readPin(GPIO_Handle gpio, int pin){
    //The pin was out of bounds
    if(pin < PINMIN || pin > PINMAX){
        fprintf(stderr, "pin %i was out of range (should be [0,53])\n", pin);
        return -1;
    }

    //The GPLEV register is calculated
    int reg = (pin / GPLEV_SIZE);

    //The bit position is calculated
    int bitPos = pin - (GPLEV_SIZE*(pin / GPLEV_SIZE));

    //Read the register we calculated
	uint32_t sel_reg = gpiolib_read_reg(gpio, GPLEV(reg));
    
    //Select the bit we calculated
    int value = (sel_reg & (1 << bitPos));
    
    
    //Return the value of that bit
    if(value){
        return 1;
    }
    return 0;
}
GPIO_Handle initialiseGPIO(void){
    GPIO_Handle gpio;
	gpio = gpiolib_init_gpio();
    return gpio;
}
int displayBits(int value){
    int x = value;
    while (x > 0){
        fprintf(stderr, "%i", x%2);
        x/= 2;
    }
    fprintf(stderr, "\n");
    return 0;
}