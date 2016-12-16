#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <getopt.h>
#include <math.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"
#include "version.h"

#include "ws2811.h"


#define ARRAY_SIZE(stuff)                        (sizeof(stuff) / sizeof(stuff[0]))

// defaults for cmdline options
#define TARGET_FREQ                              WS2811_TARGET_FREQ
#define GPIO_PIN1                                12
#define GPIO_PIN2                                12
#define DMA                                      5
#define DEBUG					 1


// #define STRIP_TYPE				                 WS2811_STRIP_RGB		// WS2812/SK6812RGB integrated chip+leds
// #define STRIP_TYPE				             WS2811_STRIP_GBR		// WS2812/SK6812RGB integrated chip+leds
// #define STRIP_TYPE				             SK6812_STRIP_RGBW		// SK6812RGBW (NOT SK6812RGB)

// #define STRIP_TYPE                               WS2811_STRIP_GRB
#define STRIP_TYPE                               WS2811_STRIP_BRG


#define UDP_PORT 				 6454
#define DMX_BUFFER_SIZE				 530

#define BRIGHT                                   100
#define LED_PER_U				 170
#define UNIVERSE                                 5
#define LED_COUNT                                (LED_PER_U * UNIVERSE)
int width = LED_PER_U;
int height = UNIVERSE;
int debug = DEBUG;
int led_count = LED_COUNT;
int fps = 60;
int clear_on_exit = 1;

void dmx2rgb(ws2811_led_t* dest, char *src, size_t sn);

ws2811_t ledstring =
{
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel =
    {
        [0] =
        {
            .gpionum = GPIO_PIN1,
            .count = LED_COUNT,
            .invert = 0,
            .brightness = BRIGHT,
            .strip_type = STRIP_TYPE,
        },
        [1] =
        {
            .gpionum = 0,
            .count = 0,
            .invert = 0,
            .brightness = 0,
        },
    },
};

ws2811_led_t *matrix;

static uint8_t running = 1;
/*
int dotspos[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
*/
ws2811_led_t dotcolors[] =
{
    0x00200000,  // red
    0x00201000,  // orange
    0x00202000,  // yellow
    0x00002000,  // green
    0x00002020,  // lightblue
    0x00000020,  // blue
    0x00100010,  // purple
    0x00200010,  // pink
};

void matrix_debug(void)
{
	static int idx = 0, lidx = 0;
	int i;
/*
	for(i = 0; i < UNIVERSE; i++) {
		for(j = 0; j < LED_PER_U; j++) {
			matrix[i * LED_PER_U + j] = dotcolors[(i + idx)%8];
		}
	}
	if(++idx >= 8) idx = 0;
*/
	for(i = 0; i < UNIVERSE; i++) {
		matrix[i * LED_PER_U + lidx] = 0;
		matrix[i * LED_PER_U + idx] = 16777215;
	}
	lidx = idx;
	if(++idx >= LED_PER_U - 1) { idx = 0; }	

}

void matrix_render(void)
{
    int x, y;

    for (x = 0; x < width; x++)
    {
        for (y = 0; y < height; y++)
        {
            ledstring.channel[0].leds[(y * width) + x] = matrix[y * width + x];
        }
    }
}

void matrix_clear(void)
{
    int x, y;

    for (y = 0; y < (height ); y++)
    {
        for (x = 0; x < width; x++)
        {
            matrix[y * width + x] = 0;
        }
    }
}


static void ctrl_c_handler(int signum) {  running = 0;  }

static void setup_handlers(void)
{
    struct sigaction sa =
    {
        .sa_handler = ctrl_c_handler,
    };

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}


void parseargs(int argc, char **argv, ws2811_t *ws2811)
{
	int index;
	int c;

	static struct option longopts[] =
	{
		{"help", no_argument, 0, 'h'},
		{"dma", required_argument, 0, 'd'},
		{"gpio", required_argument, 0, 'g'},
		{"invert", no_argument, 0, 'i'},
		{"clear", no_argument, 0, 'c'},
		{"strip", required_argument, 0, 's'},
		{"height", required_argument, 0, 'y'},
		{"width", required_argument, 0, 'x'},
		{"version", no_argument, 0, 'v'},
		{"debug", required_argument, 0, 'e'},
		{"fps", required_argument, 0, 'f'},
		{0, 0, 0, 0}
	};

	while (1)
	{

		index = 0;
		c = getopt_long(argc, argv, "cd:g:his:vx:y:e:f:", longopts, &index);

		if (c == -1)
			break;

		switch (c)
		{
		case 0:
			/* handle flag options (array's 3rd field non-0) */
			break;

		case 'h':
			// fprintf(stderr, "%s version %s\n", argv[0], VERSION);
			fprintf(stderr, "Usage: %s \n"
				"-h (--help)    - this information\n"
				"-s (--strip)   - strip type - rgb, grb, gbr, rgbw\n"
				"-x (--width)   - matrix width (default 8)\n"
				"-y (--height)  - matrix height (default 8)\n"
				"-d (--dma)     - dma channel to use (default 5)\n"
				"-g (--gpio)    - GPIO to use must be one of 10,18,40,52\n"
				"                 If omitted, default is 18\n"
				"-i (--invert)  - invert pin output (pulse LOW)\n"
				"-c (--clear)   - clear matrix on exit.\n"
				"-v (--version) - version information\n"
				, argv[0]);
			exit(-1);

		case 'D':
			break;

		case 'g':
			if (optarg) {
				int gpio = atoi(optarg);
/*
	https://www.raspberrypi.org/forums/viewtopic.php?f=91&t=105044
	PWM0, which can be set to use GPIOs 12, 18, 40, and 52. 
	Only 12 (pin 32) and 18 (pin 12) are available on the B+/2B
	PWM1 which can be set to use GPIOs 13, 19, 41, 45 and 53. 
	Only 13 is available on the B+/2B, on pin 35
*/
				switch (gpio) {
					case 10:
					case 18:
					case 40:
					case 52:
						ws2811->channel[0].gpionum = gpio;
						break;
					default:
						printf ("gpio %d doesnt support PWM0\n",gpio);
						exit (-1);
				}
			}
			break;

		case 'i':
			ws2811->channel[0].invert=1;
			break;

		case 'c':
			clear_on_exit=1;
			break;

		case 'd':
			if (optarg) {
				int dma = atoi(optarg);
				if (dma < 14) {
					ws2811->dmanum = dma;
				} else {
					printf ("invalid dma %d\n", dma);
					exit (-1);
				}
			}
			break;

		case 'y':
			if (optarg) {
				height = atoi(optarg);
				if (height > 0) {
					ws2811->channel[0].count = height * width;
				} else {
					printf ("invalid height %d\n", height);
					exit (-1);
				}
			}
			break;

		case 'x':
			if (optarg) {
				width = atoi(optarg);
				if (width > 0) {
					ws2811->channel[0].count = height * width;
				} else {
					printf ("invalid width %d\n", width);
					exit (-1);
				}
			}
			break;

		case 's':
			if (optarg) {
				if (!strncasecmp("rgb", optarg, 4)) {
					ws2811->channel[0].strip_type = WS2811_STRIP_RGB;
				}
				else if (!strncasecmp("rbg", optarg, 4)) {
					ws2811->channel[0].strip_type = WS2811_STRIP_RBG;
				}
				else if (!strncasecmp("grb", optarg, 4)) {
					ws2811->channel[0].strip_type = WS2811_STRIP_GRB;
				}
				else if (!strncasecmp("gbr", optarg, 4)) {
					ws2811->channel[0].strip_type = WS2811_STRIP_GBR;
				}
				else if (!strncasecmp("brg", optarg, 4)) {
					ws2811->channel[0].strip_type = WS2811_STRIP_BRG;
				}
				else if (!strncasecmp("bgr", optarg, 4)) {
					ws2811->channel[0].strip_type = WS2811_STRIP_BGR;
				}
				else if (!strncasecmp("rgbw", optarg, 4)) {
					ws2811->channel[0].strip_type = SK6812_STRIP_RGBW;
				}
				else if (!strncasecmp("grbw", optarg, 4)) {
					ws2811->channel[0].strip_type = SK6812_STRIP_GRBW;
				}
				else {
					printf ("invalid strip %s\n", optarg);
					exit (-1);
				}
			}
			break;
		case 'e':
			if(optarg) {
				debug = atoi(optarg);
			}
			break;
		case 'f':
			if(optarg) fps = atoi(optarg);
			break;

		case 'v':
			// fprintf(stderr, "%s version %s\n", argv[0], VERSION);
			exit(-1);

		case '?':
			/* getopt_long already reported error? */
			exit(-1);

		default:
			exit(-1);
		}
	}
}


int main(int argc, char *argv[])
{
    int ret = 0, ofst = 0, i = 0;
	struct sockaddr_in myaddr;                      /* our address */
    struct sockaddr_in remaddr;                     /* remote address */
    socklen_t addrlen = sizeof(remaddr);            /* length of addresses */
    int recvlen;                                    /* # bytes received */
    int fd;                                         /* our socket */
    char udp_buf[DMX_BUFFER_SIZE];                  /* receive buffer */
	char rgb_buf[LED_COUNT * 3];         /* rgb buffer */
    parseargs(argc, argv, &ledstring);

    matrix = malloc(sizeof(ws2811_led_t) * width * height);

    setup_handlers();

    if (ws2811_init(&ledstring))
    {
		fprintf(stderr, "ws281x init failed\n");
        return -1;
    }
   
    /* create a UDP socket */

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("cannot create socket\n");
        return -2;
    }

    /* bind the socket to any valid IP address and a specific port */

    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(UDP_PORT);

    if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
		perror("bind failed");
        return -3;
    }


    while (running)
    {
		int dmx_repeat_count[5] = {0,0,0,0,0};
		int received_count = 0;
		if(!debug) {
			// printf("[%d]", i);
			while (received_count < UNIVERSE) {
				recvlen = recvfrom(fd, udp_buf, DMX_BUFFER_SIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
				if (recvlen <= 14) { continue; }
				ofst = udp_buf[14];
				// printf("%d, ", ofst);
				if (ofst >= UNIVERSE) { continue; }
				// printf("%d, ", ofst);
				// if (dmx_repeat_count[ofst] > 0) { continue; }
				received_count++;
				dmx_repeat_count[ofst]++;
				memcpy(rgb_buf + ofst * LED_PER_U * 3, udp_buf + 18, LED_PER_U * 3 * sizeof(char));
				
				// dmx2rgb(ledstring.channel[0].leds + ofst * LED_PER_U, rgb_buf, LED_COUNT * 3);
				// fprintf(stderr, "%d\n", ofst);
			}
			if(++i > 1000) i = 0;
			// printf("\n");
			dmx2rgb(matrix, rgb_buf, LED_COUNT * 3);

		} else {
			matrix_debug();	
		}
	    matrix_render();

	    if (ws2811_render(&ledstring))
	    {
	        ret = -1;
	        break;
	    }
		
        // 60 frames /sec
        usleep(1000000 / (1 + fps));
    }

    if (clear_on_exit) {
		matrix_clear();
		matrix_render();
		ws2811_render(&ledstring);
    }

    ws2811_fini(&ledstring);
	close(fd);
    printf ("\n");
    return ret;
}

void dmx2rgb(ws2811_led_t* dest, char *src, size_t sn) {
	int i;
	int dn = sn / 3;
	for (i = 0; i < dn; i++) {
		dest[i] = ((ws2811_led_t)src[i * 3] << 16) + ((ws2811_led_t)src[i * 3 + 1] << 8) + (ws2811_led_t)src[i * 3 + 2];
	}
}





