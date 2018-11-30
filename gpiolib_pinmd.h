#ifndef GPIO_PINMD_H
#define GPIO_PINMD_H

#define GPINP(gpio, _x) *(gpio+ ((_x)/10)) &= ~(7<<(((_x)%10)*3))
#define GPOUT(gpio, _x) *(gpio + ((_x)/10)) |= (1<<(((_x)%10)*3))
#define GPREAD(gpio, _x) *(gpio + 13) &= (1<<(_x))
#define GPWRITE(gpio, _x, _d)  *(gpio + 7) = (_d << (_x))

#endif /* GPIO_PINMD_H */