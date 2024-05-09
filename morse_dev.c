#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/ktime.h>

/* LIMITS AND EXPECTATIONS */
/*
	1. always use echo -n "something" > /dev/morse_dev (because we want to avoid sending new line feed) 	
	2. make only one space between words and no space at the end of word when sending data to driver (i.e. avoid doing this: AB  CD or this: AB CD ). Example of good usage: AB CD
	3. first echo data to driver (i.e. write data to it) and then perform cat (i.e. reading from it)
	4. always pass capital letters (logic for conversion isn't implemented in driver)
	5. In ERROR mode, we will have additional dot and space (i.e. "* ") before each letter
	6. After each change of configuration, encoded data which is currently written in buffer will be output on diode once again. All configurations, except switching between NORMAL and ERROR modes will be visible immediately on led output. Error insertion is done on-fly while encoding, so writing of new portion of data will be needed in order to notice error insertion on diode
	7. be patient after led shuts off. It doesn't mean that encoded word is ended. There are 3 spaces after last character, during which diode is off, but it is still considered as showing of encoded word
*/

/* CONSTANTS AND TYPES */
#define ENCODED_CHAR_MAX_LENGTH 	     20 /* the worst case is that we have all zeros to encode (because it is all composed of dashes, which lasts longest). 0 -> 5 * (3+1) */
#define MAX_NUM_OF_CHARS_TO_BE_ENCODED 	     50	/* max num of chars that user app can pass */
#define COUNT 				      1	/* num of minor numbers */

#define PHY_ADDR_SPC_PERIPH_START    0x3F200000	/* starting address of peripherals in ARM physical address space */
#define PHY_ADDR_SPC_LEN 	     0x000000B4 /* size of address space */
#define GPFSEL3_OFFSET  	     0x0000000C	/* offset from starting address of function select register which controls first LED (GPIO 35) (i.e. GPIO pins 39-30 range) */
#define GPFSEL4_OFFSET  	     0x00000010	/* offset from starting address of function select register which controls first LED (GPIO 47) (i.e. GPIO pins 49-40 range) */
#define GPSET1_OFFSET   	     0x00000020 /* offset from starting address of set output register */
#define GPCLR1_OFFSET   	     0x0000002C /* offset from starting address of clear output register */
#define CLEAR_FUNCTION_GPIO_35 	     0xFFFC7FFF	/* perform bitwise AND operation between this value and function select register in order to clear function of GPIO PIN 35 */
#define CLEAR_FUNCTION_GPIO_47 	     0xFF1FFFFF	/* perform bitwise AND operation between this value and function select register in order to clear function of GPIO PIN 47 */
#define CONF_OUTPUT_GPIO_35 	     0x00008000	/* perform bitwise OR operation between this value and function select register in order to set GPIO PIN 35 to output mode */
#define CONF_OUTPUT_GPIO_47 	     0x00200000	/* perform bitwise OR operation between this value and function select register in order to set GPIO PIN 47 to output mode */
#define GPIO_35			     0x00000008 /* perform bitwise OR operation between this value and set/clear register in order to set/clear GPIO PIN 35 */
#define GPIO_47			     0x00008000 /* perform bitwise OR operation between this value and set/clear register in order to set/clear GPIO PIN 47 */

typedef enum {
	LED_LEFT,
	LED_RIGHT
} led_selector;

typedef enum {
	NORMAL,
	ERROR
} work_mode;

typedef enum {
	SINGLE = 1,
	DASH = 3
} threshold;

/* HW RELATED DATA */

/* device */
static struct cdev test_cdev;
dev_t dev;

/* LEDs */
void __iomem* virtualized_io_start_addr = NULL;
void __iomem* virtualized_GPFSEL3_addr = NULL;
void __iomem* virtualized_GPFSEL4_addr = NULL;
void __iomem* virtualized_GPSET1_addr = NULL;
void __iomem* virtualized_GPCLR1_addr = NULL;
led_selector selected_led = LED_LEFT;
threshold active_threshold = SINGLE;
int unit_counter = 0;
int char_to_be_shown = 0;
int blinking = 0;

/* timer */
int time_unit_ms = 2000;			/* default time unit is 2000 ms */
struct hrtimer blink_timer;			/* timer handle */
ktime_t kt;					/* timeout definition */

/* ALGORITHM RELATED DATA AND TMP */
char rawData[MAX_NUM_OF_CHARS_TO_BE_ENCODED];
char encodedData[MAX_NUM_OF_CHARS_TO_BE_ENCODED * ENCODED_CHAR_MAX_LENGTH];
int encodedDataLength = 0;
work_mode current_work_mode = NORMAL;
const char* charToMorseTable[] = {
    "* -",	 /* A */
    "- * * *",	 /* B */
    "- * - *",	 /* C */
    "- * *",	 /* D */
    "*",	 /* E */
    "* * - *",	 /* F */
    "- - *",	 /* G */
    "* * * *",	 /* H */
    "* *",	 /* I */
    "* - - -",	 /* J */
    "- * -",	 /* K */
    "* - * *",	 /* L */
    "- -",	 /* M */
    "- *",	 /* N */
    "- - -",	 /* O */
    "* - - *",	 /* P */
    "- - * -",	 /* Q */
    "* - *",	 /* R */
    "* * *",	 /* S */
    "-",	 /* T */
    "* * -",	 /* U */
    "* * * -",	 /* V */
    "* - -",	 /* W */
    "- * * -",	 /* X */
    "- * - -",	 /* Y */
    "- - * *",	 /* Z */
    "- - - - -", /* 0 */
    "* - - - -", /* 1 */
    "* * - - -", /* 2 */
    "* * * - -", /* 3 */
    "* * * * -", /* 4 */
    "* * * * *", /* 5 */
    "- * * * *", /* 6 */
    "- - * * *", /* 7 */
    "- - - * *", /* 8 */
    "- - - - *"	 /* 9 */ 
};

/* DEVICE FUNCTIONS PROTOTYPES */
static ssize_t morse_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static ssize_t morse_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);
static long morse_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void turnOnLeftLED(void);
void turnOffLeftLED(void);
void turnOnRightLED(void);
void turnOffRightLED(void);

/* Timer callback function called each time the timer expires */
static enum hrtimer_restart blink_timer_callback(struct hrtimer *param)
{
	//pr_info("encodedDataLength: %d, char_to_be_shown: %d\n", encodedDataLength, char_to_be_shown);
	if (encodedDataLength > 0 && char_to_be_shown < encodedDataLength){
		blinking = 1;
		unit_counter++;
		if (unit_counter == active_threshold){
			/* time to read encoded element and drive diode */
			unit_counter = 0;
			if (encodedData[char_to_be_shown] == '*'){
				active_threshold = SINGLE;
				if (selected_led == LED_LEFT){
					turnOnLeftLED();
				} else{
					turnOnRightLED();
				}
			} else{
				if (encodedData[char_to_be_shown] == '-'){
					active_threshold = DASH;
					if (selected_led == LED_LEFT){
						turnOnLeftLED();
					} else{
						turnOnRightLED();
					}
				} else{
					if (encodedData[char_to_be_shown] == ' '){
						active_threshold = SINGLE;
						if (selected_led == LED_LEFT){
							turnOffLeftLED();
						} else{
							turnOffRightLED();
						}
					} else{
						/* should not happen */
					}
				}
			}
			char_to_be_shown++;
		} else{
			/* still showing current encoded element on LED */
		}
		
	} else{
		/* do not blink */
		blinking = 0;
	}
      
	hrtimer_forward(&blink_timer, ktime_get(), kt);

    	return HRTIMER_RESTART;
}

/* Linking device functions with file operations */ 
static const struct file_operations test_fops = {
	.owner = THIS_MODULE,
	.read = morse_read,
	.write = morse_write,
	.unlocked_ioctl = morse_ioctl
};

static int __init morse_init(void) {

	int tmp;
	
	pr_info("Hello from Morse module\n");
	
	/* dynamically allocate major and minor */
	if (alloc_chrdev_region(&dev, 0, COUNT, "morse_dev")) {
		pr_err("Failed to allocate device number\n");
		goto alloc_error;
	}
	
	/* init char device */  
	cdev_init(&test_cdev, &test_fops);
	
	/* add char device to the system */ 
	if (cdev_add(&test_cdev, dev, COUNT)) {
		pr_err("Char driver registration failed\n");
		goto add_error;
	}
	
	/* LEDs related inits */	
	virtualized_io_start_addr = ioremap(PHY_ADDR_SPC_PERIPH_START, PHY_ADDR_SPC_LEN);
	if (virtualized_io_start_addr == NULL){
		pr_err("Faield to virtualize IO\n");
		goto add_error;
	}
	virtualized_GPFSEL3_addr = virtualized_io_start_addr + GPFSEL3_OFFSET;
	virtualized_GPFSEL4_addr = virtualized_io_start_addr + GPFSEL4_OFFSET;
	virtualized_GPSET1_addr = virtualized_io_start_addr + GPSET1_OFFSET;
	virtualized_GPCLR1_addr = virtualized_io_start_addr + GPCLR1_OFFSET;
	
	/* setting LED GPIOs as output */
	tmp = ioread32(virtualized_GPFSEL3_addr);
	tmp &= CLEAR_FUNCTION_GPIO_35;
	tmp |= CONF_OUTPUT_GPIO_35;
	iowrite32(tmp, virtualized_GPFSEL3_addr);
	
	tmp = ioread32(virtualized_GPFSEL4_addr);
	tmp &= CLEAR_FUNCTION_GPIO_47;
	tmp |= CONF_OUTPUT_GPIO_47;
	iowrite32(tmp, virtualized_GPFSEL4_addr);
	
	/* make LEDs off initially */
	blinking = 0;
	turnOffLeftLED();
	turnOffRightLED();
	
	/* Initialize high resolution timer. */
    	hrtimer_init(&blink_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kt = ktime_set(0 /*TIMER_SEC*/, time_unit_ms * 1000000 /*TIMER_NANO_SEC*/); /* timer interval defined as (TIMER_SEC + TIMER_NANO_SEC) */ 
	blink_timer.function = &blink_timer_callback;
	hrtimer_start(&blink_timer, kt, HRTIMER_MODE_REL);
		
	return 0;
	
add_error:
	unregister_chrdev_region(dev, COUNT);	
	
alloc_error:
	return -1;
}

static void __exit morse_exit(void) {

	int tmp;

	pr_info("Goodbye from Morse module\n");
	
	hrtimer_cancel(&blink_timer);
	
	turnOffLeftLED();
	turnOffRightLED();
	
	tmp = ioread32(virtualized_GPFSEL3_addr);
	tmp &= CLEAR_FUNCTION_GPIO_35;			/* 000 value will make it input again */
	iowrite32(tmp, virtualized_GPFSEL3_addr);
	
	tmp = ioread32(virtualized_GPFSEL4_addr);
	tmp &= CLEAR_FUNCTION_GPIO_47;			/* 000 value will make it input again */
	iowrite32(tmp, virtualized_GPFSEL4_addr);
	
	cdev_del(&test_cdev);
	unregister_chrdev_region(dev, COUNT);
	
	if (virtualized_io_start_addr != NULL){
		iounmap(virtualized_io_start_addr);
	}
}

static ssize_t morse_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int remainingToRead = encodedDataLength - *ppos;  // remaining data to be read 
	int to_transfer = count;

	/* if user app requests more than we can provide */
	if (to_transfer > remainingToRead) {
		/* we will do our best and provide everything we have */
		to_transfer = remainingToRead;
	} else{
		/* to_transfer already equals count (i.e. desired number of bytes) */
	}
	
	if (copy_to_user(buf, encodedData, to_transfer) == 0) {
		/* cat will be kept invoked until it returns zero, so avoid printing zero characters to log in last iteration */
		if (to_transfer != 0){
			//pr_info("Sending data to user app...\n");
			//pr_info("Sent %d characters to app side\n", to_transfer);
		}		
		*ppos += to_transfer;
		return to_transfer;
	}
	
	pr_info("Should not be printed!\n");

	return -1; // NOTE: better to use specific error code from include/uapi/asm-generic/errno-base.h
}

static ssize_t morse_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{	
	if (blinking == 1){
		/* we are still blinking, reject new data */
		//pr_info("Still blinking, please wait...\n");
		
		return -1;
	} else{
		/* we finished with blinking of current encoded data, we can receive new portion */
		//pr_info("Receiving data from user app...\n");
	
		int remaining_free = MAX_NUM_OF_CHARS_TO_BE_ENCODED - *ppos; // remaining free size in rawData 
		int to_transfer = count;
		int i = 0;
		
		/* clear old data before starting new encoding iteration */
		if (*ppos == 0){
			//pr_info("Cleared local buffer from previous iteration\n");
			memset(rawData, 0, MAX_NUM_OF_CHARS_TO_BE_ENCODED);
			memset(encodedData, 0, MAX_NUM_OF_CHARS_TO_BE_ENCODED*ENCODED_CHAR_MAX_LENGTH);
			encodedDataLength = 0;
			char_to_be_shown = 0;
			unit_counter = 0;
		}
		
		/* protection from case when application wants to write more than driver's module can accept in buffer */
		if (remaining_free != 0){
			if (to_transfer > remaining_free) {
				/* our best is to fill the rest of free spaces, some data won't be written */
				to_transfer = remaining_free;
			} else{
				/* to_transfer already equals count (i.e. desired number of bytes) */
			}
		} else{
			to_transfer = 0;
		}
		
		//pr_info("Transfering %d characters from app side\n", to_transfer);
		
		if (copy_from_user(rawData + *ppos, buf, to_transfer) == 0) {		
			//pr_info("Starting encoding...\n");
			for (i = 0; i < to_transfer; i++){
				//pr_info("Received %d character\n", rawData[*ppos + i]);
				if (rawData[*ppos + i] >= 65){
					/* we have letter */
					if (current_work_mode == ERROR){
						encodedData[encodedDataLength] = '*';
						encodedDataLength++;
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;
					}
					memcpy(encodedData + encodedDataLength, charToMorseTable[rawData[*ppos + i] - 65], strlen(charToMorseTable[rawData[*ppos + i] - 65]));
					encodedDataLength += strlen(charToMorseTable[rawData[*ppos + i] - 65]);
					
					/* adding character separators */
					encodedData[encodedDataLength] = ' ';
					encodedDataLength++;
					encodedData[encodedDataLength] = ' ';
					encodedDataLength++;
					encodedData[encodedDataLength] = ' ';
					encodedDataLength++;
				} else{
					if (rawData[*ppos + i] >= 48){
						/* we have digit */		
						//pr_info("Encoding digit\n");		
						//pr_info("Encoded val: %s\n", charToMorseTable[rawData[*ppos + i] - 22]);	
						memcpy(encodedData + encodedDataLength, charToMorseTable[rawData[*ppos + i] - 22], strlen(charToMorseTable[rawData[*ppos + i] - 22]));
						encodedDataLength += strlen(charToMorseTable[rawData[*ppos + i] - 22]);
						
						/* adding character separators */
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;
					} else{
						/* we have word separator */
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;	
						encodedData[encodedDataLength] = ' ';
						encodedDataLength++;				
					}
				}
			}
			return to_transfer;
		}		
		
		pr_info("Transfering failed\n");
		return -1; // NOTE: better to use specific error code from include/uapi/asm-generic/errno-base.h
	}
}

static long morse_ioctl(struct file *file, unsigned int cmd, unsigned long arg){

	//pr_info("ioctl call detected. CMD: %d, ARG: %d\n", cmd, arg);
	
	/* configuring driver, reinitialize control variables and shut down LEDs */
	char_to_be_shown = 0;
	unit_counter = 0;
	turnOffLeftLED();
	turnOffRightLED();
	/* we are not reseting blink control variable, because if led was blinking before configuration, it should blink also after configuration, but from beginning of encoded data */
	
	if (cmd == 0){
		if (arg == 0){
			current_work_mode = NORMAL;						
		} else{
			if (arg == 1){
				current_work_mode = ERROR;
			} else{
				/* should not happen */
			}			
		}
	} else{
		if (cmd == 1){			
			if (arg == 0){
				selected_led = LED_LEFT;
			} else{
				selected_led = LED_RIGHT;
			}
		} else{
			if (cmd == 3){
				/* we are choosing time unit amount */
				time_unit_ms = arg;			
				
				/* reinit timer */
				hrtimer_cancel(&blink_timer);
				
				hrtimer_init(&blink_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
				kt = ktime_set(0 /*TIMER_SEC*/, time_unit_ms * 1000000 /*TIMER_NANO_SEC*/); /* timer interval defined as (TIMER_SEC + TIMER_NANO_SEC) */ 
				blink_timer.function = &blink_timer_callback;
				hrtimer_start(&blink_timer, kt, HRTIMER_MODE_REL);
			} else{
				/* should not happen */
			}
		}
	}
	
	return 0;	
}

void turnOnLeftLED(void){

	iowrite32(GPIO_35, virtualized_GPSET1_addr);	
}

void turnOffLeftLED(void){

	iowrite32(GPIO_35, virtualized_GPCLR1_addr);
}

void turnOnRightLED(void){

	iowrite32(GPIO_47, virtualized_GPSET1_addr);
}

void turnOffRightLED(void){

	iowrite32(GPIO_47, virtualized_GPCLR1_addr);
}


module_init(morse_init);
module_exit(morse_exit);

MODULE_DESCRIPTION("Morse module");
MODULE_AUTHOR("Darian Pudic");
MODULE_LICENSE("GPL");

