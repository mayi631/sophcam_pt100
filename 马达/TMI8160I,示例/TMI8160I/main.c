#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "tmi8160i_helper.h"
#include "i2c_controller.h"

static void Usage(void)
{
    printf("Usage: ptz [option]\n");
    printf("    -h help.\n");
    printf("    -i get tmi8160i ID.\n");
    printf("    -a set tmi8160i init and auto mode.\n");
    printf("    -d set tmi8160i init and direct out mode.\n");
    printf("    -b set tmi8160i init and DC-Brush mode.\n");
    printf("    -s set tmi8160i init and 1-2 phase mode.\n");
    printf("    -r set tmi8160i init and 2-2 phase mode.\n");
    printf("    -l set tmi8160i init and IRCUT DC Motor.\n");
    printf("    -p set tmi8160i init and AbsolutePosition mode.\n");
    printf("    -o set tmi8160i init and RelativePosition mode.\n");
}

int main(int argc, char **argv)
{
    printf(" ======================      Hello tmi8160i!      ==============================\n");
    int fd = i2c_init(NULL);
    if(fd < 0){
        printf("Filed to init I2CDevice.\n");
        return -1;
    }
    setI2CHandle(fd);
    setSlaveAddress(fd);

    int	c;
    while ((c = getopt(argc, argv, "hiadbsrlpo")) != EOF) {
		switch (c) {
        case 'i':
        {
            uint16_t id = getID();
            printf("ID:0x%04X\n",id);
        }break;
		case 'a':
        {
            initAutoControl(CH12,MSTEP_DIVIDE_64,CLOCK_PRE_DIVIDE_64,CLOCK_DIVIDE_1,SPEED_PRE_DIVIDE_64,SPEED_DIVIDE_1,0x0040);
            setChannelAction(CH12,FOREWARD);
        }break;
		case 'd':
        {
            initDirectControl(CH1);
            setChannelAction(CH1,FOREWARD);
        }break;
		case 'b':
        {
            initBldcControl(CH1,SPEED_PRE_DIVIDE_64,SPEED_DIVIDE_1,0x8000);
            setChannelAction(CH1,FOREWARD);
        }break;
		case 's':
        {
            init1_2PhaseControl(CH12,CLOCK_PRE_DIVIDE_1,CLOCK_DIVIDE_1,SPEED_PRE_DIVIDE_32,SPEED_DIVIDE_1,0X02FF,0,0x06ff);
            setChannelAction(CH12,FOREWARD);
        }break;
		case 'r':
        {
            init2_2PhaseControl(CH12,CLOCK_PRE_DIVIDE_1,CLOCK_DIVIDE_1,SPEED_PRE_DIVIDE_32,SPEED_DIVIDE_1,0X04FF,0x0080,0x06ff);
            setChannelAction(CH12,FOREWARD);
        }break;
		case 'l':
        {
            initIRCUTAction(FOREWARD);
        }break;
        case 'p':
        {
            initAbsolutePositionControl(CH12,MSTEP_DIVIDE_128,CLOCK_PRE_DIVIDE_128,CLOCK_DIVIDE_1,SPEED_PRE_DIVIDE_32,SPEED_DIVIDE_1,0x0080,0,0x00ff);
            setChannelAction(CH12,FOREWARD);
        }break;
        case 'o':
        {
            initRelativePositionControl(CH12,MSTEP_DIVIDE_128,CLOCK_PRE_DIVIDE_128,CLOCK_DIVIDE_1,SPEED_PRE_DIVIDE_32,SPEED_DIVIDE_1,0x0080,0,0x0032);
            setChannelAction(CH12,FOREWARD);
        }break;
		case 'h':
			Usage();
			break;
		}
	}

    i2c_close(fd);
    printf(" ======================           End             ==============================\n");
    return 0;
}

