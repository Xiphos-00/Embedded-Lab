enum PinMode {INPUT, OUTPUT} pinMode;
enum Data {LO, HI} data_e;

#define PINMAX 53
#define PINMIN 0
#define GPFSELPINS 10
#define GPFSELMODE_SIZE 3
#define GPSETCLR_SIZE 32
#define GPLEV_SIZE 32

int setPin(GPIO_Handle gpio, int pin, int mode);
int writePin(GPIO_Handle gpio, int pin, int data);
int readPin(GPIO_Handle gpio, int pin);
GPIO_Handle initialiseGPIO(void);
int displayBits(int x);