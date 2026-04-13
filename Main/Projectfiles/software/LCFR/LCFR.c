/* Standard includes. */
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "system.h"
#include "io.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"
#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* The parameters passed to the reg test tasks.  This is just done to check
 the parameter passing mechanism is working correctly. */


#define LOW_FREQ_THRESHOLD 49.0;
#define HIGH_ROC_THRESHOLD 1.0;
#define SAMPLING_FREQ_HZ 16000.0

//For frequency plot
#define FREQPLT_ORI_X 101		//x axis pixel position at the plot origin
#define FREQPLT_GRID_SIZE_X 5	//pixel separation in the x axis between two data points
#define FREQPLT_ORI_Y 199.0		//y axis pixel position at the plot origin
#define FREQPLT_FREQ_RES 20.0	//number of pixels per Hz (y axis scale)

#define ROCPLT_ORI_X 101
#define ROCPLT_GRID_SIZE_X 5
#define ROCPLT_ORI_Y 259.0
#define ROCPLT_ROC_RES 0.5		//number of pixels per Hz/s (y axis scale)


#define T_FreqAndRoc_PRIORITY			4
#define T_VgaDisplay_PRIORITY			6
#define T_StabilityMonitor_PRIORITY		4
#define T_SwitchPolling_PRIORITY		5
#define T_LoadCtrl_PRIORITY				3
#define T_RecordTimeDisplay_PRIORITY	6
#define T_SwitchMode_PRIORITY			3
#define T_UpdateThreshold_PRIORITY		3


static void T_FreqAndRoc(void *pvParameters);
static void T_VgaDisplay(void *pvParameters);
static void T_SwitchPolling(void *pvParameters);
static void T_LoadCtrl(void *pvParameters);
static void T_StabilityMonitor(void *pvParameters);
static void T_RecordTimeDisplay (void *pvParameters);
static void T_SwitchMode (void *pvParameters);
static void T_UpdateThreshold (void *pvParameters);

static void ISR_Init(void);


// structure declaration
typedef struct
{
    unsigned int adcCount;
} AdcCountMessage;

typedef struct
{
    double frequencyHz;
    double rocHzPerSec;
} FreqRocMessage;

//could make ints and then we just be changing the tenths, safer and less work than critical sections
typedef struct
{
    double thresholdFreqHz;
    double thresholdRocHzPerSec;
} ThresholdMessage;

typedef	struct
{
	uint8_t producer_id; // 0- from T_SwitchPolling ; 1 = from T_StabilityMonitor
	uint8_t switch_state;
	uint8_t stability_state; // 0 - stable ; 1 - unstable
} LoadCtrlMessage;

typedef struct
{
    unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
}Line;

typedef struct
{
    char ascii;
}KeyMessage;

typedef struct
{
    uint8_t stability;
}StabilityMessage;

// system variables
static QueueHandle_t Q_adcCount;
static QueueHandle_t Q_newFreqToVGA;
static QueueHandle_t Q_newFreqToMonitor;
static QueueHandle_t Q_newLoadCtrl;
static QueueHandle_t Q_keyPress;
static QueueHandle_t Q_stabilityToVGA;

enum mode {
	MAINTENANCE,
	LOAD_MANAGING
};

enum mode current_system_mode = LOAD_MANAGING;

static ThresholdMessage threshold;

void FreqAnalyserISR(void *context)
{
    AdcCountMessage msg;

    (void)context;

    msg.adcCount = IORD(FREQUENCY_ANALYSER_BASE, 0);
    xQueueSendToBackFromISR(Q_adcCount, &msg, NULL);
}

// push button to change modes
void button_interrupts_function(void* context, alt_u32 id)
{
    // 1. Cast context (pointer to our buttonValue)
    int* temp = (int*) context;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    AdcCountMessage msg;

    // 2. Read capture and Mimic Analyser
    // We store the edge capture value into the context variable
    *temp = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);

    // Mimic the Analyser: 320 count = 50Hz
    msg.adcCount = 320;
    //printf(".2%f", msg);

    //enum to swap modes
    if(current_system_mode == MAINTENANCE){
    	current_system_mode = LOAD_MANAGING;
    } else{
    	current_system_mode = MAINTENANCE;
    }


    // 3. Send to Queue (Use FromISR version)
    //xQueueSendToBackFromISR(Q_adcCount, &msg, &xHigherPriorityTaskWoken);

    // 4. Clear the edge capture register so the interrupt doesn't re-fire
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

    // 5. Context Switch: Tells FreeRTOS to check if a task is now ready
    //portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void KeyBoardISR(void* ps2_device, alt_u32 id){
    KeyMessage msg;
    char ascii;
    int status = 0;
    unsigned char key = 0;
    KB_CODE_TYPE decode_mode;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    
    
    status = decode_scancode (context, &decode_mode , &key , &ascii) ;

    if(status != 0){
        return;
    }
    if(decode_mode != KB_CODE_TYPE){
        return;
    }
    
    msg.ascii = ascii;

    xQueueSendToBackFromISR(Q_keyPress, &msg, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // forces priority on the threshold updater


}
/*
 * Create the demo tasks then start the scheduler.
 */
int main(void)
{
    threshold.thresholdFreqHz = 49.0;
    threshold.thresholdRocHzPerSec = 5.0;

	ISR_Init();
    /* --- Queue Initialization --- */
    Q_adcCount = xQueueCreate(5, sizeof(AdcCountMessage));
    Q_newFreqToVGA = xQueueCreate(5, sizeof(FreqRocMessage));
    Q_newFreqToMonitor = xQueueCreate(5, sizeof(FreqRocMessage));
    Q_newLoadCtrl = xQueueCreate(5, sizeof(LoadCtrlMessage));
    Q_keyPress = xQueueCreate(5, sizeof(KeyMessage));
    Q_stabilityToVGA = xQueueCreate(5, sizeof(StabilityMessage));

    /* --- Task Creation --- */
    xTaskCreate(T_FreqAndRoc, "Logic", configMINIMAL_STACK_SIZE, NULL, T_FreqAndRoc_PRIORITY, NULL);
    xTaskCreate(T_VgaDisplay, "Print", configMINIMAL_STACK_SIZE, NULL, T_VgaDisplay_PRIORITY, NULL);
    xTaskCreate(T_SwitchPolling, "Logic", configMINIMAL_STACK_SIZE, NULL, T_SwitchPolling_PRIORITY, NULL);
    xTaskCreate(T_StabilityMonitor, "Logic", configMINIMAL_STACK_SIZE, NULL,T_StabilityMonitor_PRIORITY, NULL);
    xTaskCreate(T_SwitchMode, "Logic", configMINIMAL_STACK_SIZE, NULL,T_SwitchMode_PRIORITY, NULL);
    xTaskCreate(T_LoadCtrl, "Logic", configMINIMAL_STACK_SIZE, NULL,T_LoadCtrl_PRIORITY, NULL);
    xTaskCreate(T_UpdateThreshold, "Logic", configMINIMAL_STACK_SIZE, NULL,T_UpdateThreshold_PRIORITY, NULL );

    vTaskStartScheduler();

    for (;;);
}

static void ISR_Init(void)
{
    static int buttonValue = 0;
    /* --- Hardware Interrupt Setup --- */
    // Clear and Mask
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);

    //keyboard inits
    alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

    // check if device valid
    if(ps2_device == NULL){
        printf("can't find PS/2 device\n");
        return;
    }

    //clears and registers device
    alt_up_ps2_clear_fifo (ps2_device) ;
    alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);
    // register the PS/2 interrupt
    IOWR_8DIRECT(PS2_BASE,4,1);
   
    alt_irq_register(PUSH_BUTTON_IRQ, (void*)&buttonValue, button_interrupts_function);
}

static void T_FreqAndRoc(void *pvParameters)
{

    AdcCountMessage inMsg;
    FreqRocMessage outMsg;
    double prevFreq = 0.0;
    bool first = true;

    (void)pvParameters;

    for (;;)
    {
        if (xQueueReceive(Q_adcCount, &inMsg, portMAX_DELAY) == pdPASS)
        {
            if (inMsg.adcCount == 0)
            {
                continue;
            }
            //printf("    task1 activated");

            outMsg.frequencyHz = SAMPLING_FREQ_HZ / (double)inMsg.adcCount;

            if (first)
            {
                outMsg.rocHzPerSec = 0.0;
                first = false;
            }
            else
            {
                double deltaT = (double)inMsg.adcCount / SAMPLING_FREQ_HZ;
                outMsg.rocHzPerSec = (outMsg.frequencyHz - prevFreq) / deltaT;
            }

            prevFreq = outMsg.frequencyHz;

            xQueueSendToBack(Q_newFreqToVGA, &outMsg, portMAX_DELAY);
            xQueueSendToBack(Q_newFreqToMonitor, &outMsg, portMAX_DELAY);
        }
    }

}

// needs to use threshold queue and then take that to display
static void T_VgaDisplay(void *pvParameters)
{
    /* Use the correct struct type for the message */
    FreqRocMessage receivedMsg; //frequencyHz, rocHzPerSec - both in queue
    StabilityMessage stabilityMsg; // 0 stable, 1 unstable

    uint8_t currentStabilityState = 0;

    //copy thresholds
    double freqThreshold;
    double rocThreshold;

    char freqText[48];
    char rocText[48];
    char statusText[32];

    (void)pvParameters; // Prevent compiler warning for unused parameter

    //initialize VGA controllers
	alt_up_pixel_buffer_dma_dev *pixel_buf;
	pixel_buf = alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
	if(pixel_buf == NULL){
		printf("can't find pixel buffer device\n");
	}

	alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0);

	alt_up_char_buffer_dev *char_buf;
	char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
	if(char_buf == NULL){
		printf("can't find char buffer device\n");
	}

	alt_up_char_buffer_clear(char_buf);

	//Set up plot axes
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 50, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 220, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);

    // frequency header
    alt_up_char_buffer_string(char_buf, "Frequency(Hz)", 4, 4);
    // frequency plot axis
    alt_up_char_buffer_string(char_buf, "52.0", 10, 7);
	alt_up_char_buffer_string(char_buf, "50.0", 10, 12);
	alt_up_char_buffer_string(char_buf, "48.0", 10, 17);
	alt_up_char_buffer_string(char_buf, "46.0", 10, 22);

    //roc header
    alt_up_char_buffer_string(char_buf, "df/dt(Hz/s)", 4, 26);
    // roc axis unnecessary if underneath the frequency - just needs to showcase the changes

    //init buffers
    double freq[100] = {0};
    double ROCfreq[100] = {0};
	int i = 0;
	Line line_freq, line_roc;

    for (;;)
    {

        /* Listen to the Q_newFreqToVGA queue */
        if (xQueueReceive(Q_newFreqToVGA, &receivedMsg, portMAX_DELAY) == pdPASS) // frequency & ROC data has been received
        {
            // fills buffers with data
            freq[i] = receivedMsg.frequencyHz;
            ROCfreq[i] = receivedMsg.rocHzPerSec;
            i = (i + 1) % 100;
        }

        //drains all latest stability changes
        while(xQueueReceive(Q_stabilityToVGA, &stabilityMsg, 0) == pdPASS){
            currentStabilityState = stabilityMsg.stability;
        }



        // safely copies thresholds
        taskENTER_CRITICAL();
        freqThreshold = threshold.thresholdFreqHz;
        rocThreshold = threshold.thresholdRocHzPerSec;
        taskEXIT_CRITICAL();

        //clear old data
        alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 0, 639, 199, 0, 0);
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 201, 639, 299, 0, 0);

        // clear old text area by overwriting with spaces
        alt_up_char_buffer_string(char_buf, "                                        ", 4, 34);
        alt_up_char_buffer_string(char_buf, "                                        ", 4, 36);
        alt_up_char_buffer_string(char_buf, "                                        ", 4, 38);

        // formats latest text
        snprintf(freqText, sizeof(freqText), "Frequency Threshold: %.1f Hz", freqThreshold);
        snprintf(rocText, sizeof(rocText), "Rate of Change Threshold: %.1f Hz/s", rocThreshold);

        if (currentStabilityState == 0) {
            snprintf(statusText, sizeof(statusText), "Stability: Stable");
        } else {
            snprintf(statusText, sizeof(statusText), "Stability: Unstable");
        }

        // draws text
        alt_up_char_buffer_string(char_buf, freqText, 4, 34);
        alt_up_char_buffer_string(char_buf, rocText, 4, 36);
        alt_up_char_buffer_string(char_buf, statusText, 4, 38);

        for(int j = 0; j < 99; j++){
            //Frequency plot
				line_freq.x1 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * j;
				line_freq.y1 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j)%100] - freqThreshold)); //plotting relative to 49.0 HZ

				line_freq.x2 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * (j + 1);
				line_freq.y2 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j+1)%100] - freqThreshold));

				//Frequency RoC plot
                //Shows distinct changes from the frequency, no relative value - same as figure 2
				line_roc.x1 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * j;
				line_roc.y1 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * ROCfreq[(i+j)%100]);

				line_roc.x2 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * (j + 1);
				line_roc.y2 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * ROCfreq[(i+j+1)%100]);

				//Draw lines
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_freq.x1, line_freq.y1, line_freq.x2, line_freq.y2, 0x3ff << 0, 0);
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_roc.x1, line_roc.y1, line_roc.x2, line_roc.y2, 0x3ff << 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));

    }
}

static void T_SwitchPolling(void *pvParameters)
{
	  unsigned int uiSwitchValue = 0;
	  LoadCtrlMessage outMsg;
	  for (;;)
	  {
		   uiSwitchValue = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
		   outMsg.producer_id = 0;
		   outMsg.switch_state = (uint8_t)uiSwitchValue;
		   xQueueSendToBack(Q_newLoadCtrl, &outMsg, portMAX_DELAY);
		   vTaskDelay(pdMS_TO_TICKS(100));
	  }
}


// needs to be altered to use the threshold frequency message from keyboard
static void T_StabilityMonitor(void *pvParameters){
	FreqRocMessage receivedMsg;
	LoadCtrlMessage outMsg;
    StabilityMessage vgaMsg;

    double freqThreshold, RocThreshold;

    taskENTER_CRITICAL();
    freqThreshold = threshold.thresholdFreqHz;
    RocThreshold = threshold.thresholdRocHzPerSec;
    taskEXIT_CRITICAL();


	  for (;;)
	  {
		if (xQueueReceive(Q_newFreqToMonitor, &receivedMsg, portMAX_DELAY) == pdPASS)
		{
			outMsg.producer_id = 1;
			outMsg.stability_state = 0;
			uint8_t low_freq_flag = receivedMsg.frequencyHz < freqThreshold;
			uint8_t high_roc_flag = fabs(receivedMsg.rocHzPerSec) > RocThreshold;
			outMsg.stability_state = low_freq_flag | high_roc_flag;
            vgaMsg.stability = outMsg.stability_state;

			xQueueSendToBack(Q_newLoadCtrl, &outMsg, portMAX_DELAY);
            xQueueSendToBack(Q_stabilityToVGA, &vgaMsg, portMAX_DELAY);
		}
	  }
}

// lowkey, is this even necessary? Cause the ISR is swapping modes itself, this task doesn't really do anything 
// but tell LoadCtrl that the modes switched. But then we read from the global enum anyways? Which we may needa mutex lock lol
static void T_SwitchMode(void *pvParameters)
{
	(void)pvParameters;
	if(current_system_mode == MAINTENANCE){
		//need to block the relay. stop it from any shedding
	}
	else{
		//proceed normally


	}

}
static void T_LoadCtrl(void *pvParameters)
{
    LoadCtrlMessage receivedMsg;
    static uint8_t requestedLoadMask = 0; // user leds
    static uint8_t shedLoadMask = 0; // relay leds
    static uint8_t actualLoadMask = 0; // what will be used to display the leds

    static uint8_t networkUnstable = 0;
    static uint8_t prevNetworkUnstable = 0; //init stability, and change as updates throughout code run

    static uint8_t loadManaging = 0;


    (void)pvParameters; // Prevent compiler warning for unused parameter

    for (;;)
    {
        /* Listen to the Q_newFreqToVGA queue */
        if (xQueueReceive(Q_newLoadCtrl, &receivedMsg, portMAX_DELAY) == pdPASS)
        {
        	if (receivedMsg.producer_id == 0) //switch generated
        	{
                uint8_t newSwitchMask = receivedMsg.switch_state & 0x1F;
                if(current_system_mode == MAINTENANCE){
                    // force relay to do nothing, only listens to users input
                    requestedLoadMask = newSwitchMask;
                    shedLoadMask = 0;
                    loadManaging = 0; //flag for relay on/off
                }
                else {
                    // user able to turn off loads, cannot turn on a load if relay has shed
                    uint8_t userTurnedOff = requestedLoadMask & (~newSwitchMask);
                    uint8_t allowedTurnedOff = newSwitchMask & (~shedLoadMask);

                    requestedLoadMask = userTurnedOff | allowedTurnedOff;
                }
        		//IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, receivedMsg.switch_state);
        	}
        	else if (receivedMsg.producer_id == 1) // stability monitor generated
        	{   
                networkUnstable = receivedMsg.stability_state; // essentially flagging whether stable or not

                if(current_system_mode != MAINTENANCE){
                    if(networkUnstable != prevNetworkUnstable){
                        //change in stability of system, implement counter start 500ms count

                    }

                }

        	}
        }
    }

}

static void T_UpdateThreshold(void *pvParameters){
    KeyMessage msg;

    (void)pvParameters;

    for(;;){
        if(xQueueReceive(Q_keyPress, &msg, portMAX_DELAY) == pdPass){
            switch(msg.ascii){
                case 'w':
                case 'W':
                taskENTER_CRITICAL();
                threshold.thresholdFreqHz += 0.1;
                taskEXIT_CRITICAL();
                printf("Threshold Frequency is : %.2f" , threshold.thresholdFreqHz);

                case 's':
                case 'S':
                taskENTER_CRITICAL();
                threshold.thresholdFreqHz -= 0.1;
                taskEXIT_CRITICAL();
                printf("Threshold Frequency is : %.2f" , threshold.thresholdFreqHz);

                case 'a':
                case 'A':
                taskENTER_CRITICAL();
                threshold.thresholdRocHzPerSec -= 0.1;
                taskEXIT_CRITICAL();
                printf("Threshold RoC is : %.2f" , threshold.thresholdRocHzPerSec);

                case 'd':
                case 'D':
                taskENTER_CRITICAL();
                threshold.thresholdRocHzPerSec += 0.1;
                taskEXIT_CRITICAL();
                printf("Threshold RoC is : %.2f" , threshold.thresholdRocHzPerSec);

                default:
                break;
                //cases for the keyboard presses
            }

            if(threshold.thresholdFreqHz < 45.0){
                threshold.thresholdFreqHz = 45.0;
            }
            if(threshold.thresholdFreqHz > 50.0){
                threshold.thresholdFreqHz = 50.0;
            }

            if(threshold.thresholdRocHzPerSec < 0.1){
                threshold.thresholdRocHzPerSec = 0.1;
            }
            if(threshold.thresholdRocHzPerSec > 50.0){
                threshold.thresholdRocHzPerSec = 50.0;
            }
        }
    }

}