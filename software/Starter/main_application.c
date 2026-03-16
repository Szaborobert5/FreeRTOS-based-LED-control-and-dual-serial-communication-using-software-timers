// STANDARD INCLUDES
#include <stdio.h>
#include <string.h>

// KERNEL INCLUDES
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

// HARDWARE SIMULATOR UTILITY FUNCTIONS  
#include "HW_access.h"

//Task priorities

#define PRIORITY_CRITICAL    ( tskIDLE_PRIORITY + (UBaseType_t)4 )
#define PRIORITY_HIGH        ( tskIDLE_PRIORITY + (UBaseType_t)3 )
#define PRIORITY_NORMAL      ( tskIDLE_PRIORITY + (UBaseType_t)2 )
#define PRIORITY_LOW         ( tskIDLE_PRIORITY + (UBaseType_t)1 )

//Inicilizacija kanala
#define COM_CH_0 (0)
#define COM_CH_1 (1)

//Globalne promenljive 

static  uint8_t rezimRada = (uint8_t)0;
static  uint32_t trenutnaBrzina = (uint32_t)0;
static uint32_t leftBack = (uint32_t)0;
static uint32_t rightBack = (uint32_t)0;
static uint32_t rightFront = (uint32_t)0;
static uint32_t leftFront = (uint32_t)0;
static uint32_t srednjaBrzina = (uint32_t)0;

static uint8_t flagForWindowRiseFall = (uint8_t)0;

static  uint32_t minBrzina = (uint32_t)65535;
static  uint32_t maxBrzina = (uint32_t)0;
static  uint8_t flagForDisplayMinMax = (uint8_t)0;

//Definovi
#define MAX_CHARACTERS (30)
#define UPPER_LED_MASK   ((uint8_t)0xF0)


//Tajmeri
static TimerHandle_t timer;

//Taskovi
static void prijemPorukaOdCOM0(void* pvParameters);
static void prijemPorukaOdCOM1(void* pvParameters);
static void obradaPodataka(void* pvParameters);
static void message_COM_1(void* pvParameters);
static void ledTask(void* pvParameters);
static void okTask(void* pvParameters);
static void windowRiseFall(void* pvParameters);
static void display(void* pvParameters);

//Timer funkcija
static void tajmerFunkcija(TimerHandle_t tmH);

//Quevi
static QueueHandle_t queue_for_message;
static QueueHandle_t led_queue;


//Semafori
static SemaphoreHandle_t rxc_semaphore_COM_0;
static SemaphoreHandle_t rxc_semaphore_COM_1;
static SemaphoreHandle_t semafor_for_COM_CH_1;
static SemaphoreHandle_t sempahoreLED;
static SemaphoreHandle_t semaphoreForOk;
static SemaphoreHandle_t semaphoreMutex_COM_CH1;
static SemaphoreHandle_t semaphoreForWindows;
static SemaphoreHandle_t semaphoreForDispaly;

//Interaptovi

static uint32_t interaptRXC_0(void) {
	BaseType_t xHigherPTW = pdFALSE;

	if (get_RXC_status(0) != 0) {
		if (xSemaphoreGiveFromISR(rxc_semaphore_COM_0, &xHigherPTW) != pdPASS) {
			printf("Error from rxc_semaphore_COM_0\n");
		}
	}

	if (get_RXC_status(1) != 0) {
		if (xSemaphoreGiveFromISR(rxc_semaphore_COM_1, &xHigherPTW) != pdPASS) {
			printf("Error from rxc_semaphore_COM_1\n");
		}
	}

	portYIELD_FROM_ISR((uint32_t)xHigherPTW);

}

static uint32_t led_interrupt(void) {
	BaseType_t xHigherPTW = pdFALSE;
	if (xSemaphoreGiveFromISR(sempahoreLED, &xHigherPTW) != pdPASS) {
		printf("Error in giving ISR semaphore");
	}
	portYIELD_FROM_ISR((uint32_t)xHigherPTW);
}

void main_program(void) {
	//Inilizacija periferija

	if (init_7seg_comm() != 0) {
		printf("Display ERROR!\n");
	}

	if (init_LED_comm() != 0) {
		printf("Error occured in LEDs initialization");
	}

	//Inicalizacija COM kanala

	if (init_serial_uplink(COM_CH_0) != 0) {
		printf("Error init_serial_uplink(COM_CH_0)\n");
	}
	if (init_serial_downlink(COM_CH_0) != 0) {
		printf("Error init_serial_downlink(COM_CH_0)\n");
	}

	if (init_serial_uplink(COM_CH_1) != 0) {
		printf("Error init_serial_uplink(COM_CH_1)\n");
	}

	if (init_serial_downlink(COM_CH_1) != 0) {
		printf("Error init_serial_downlink(COM_CH_1)\n");
	}

	//Interaptovi
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, interaptRXC_0);
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, led_interrupt);

	//Semafori

	rxc_semaphore_COM_0 = xSemaphoreCreateBinary();
	if (rxc_semaphore_COM_0 == NULL) {
		printf("Error in creating rxc_semaphore_COM_0\n");
	}

	rxc_semaphore_COM_1 = xSemaphoreCreateBinary();
	if (rxc_semaphore_COM_1 == NULL) {
		printf("Error in creating rxc_semaphore_COM_1\n");
	}

	semafor_for_COM_CH_1 = xSemaphoreCreateBinary();
	if (semafor_for_COM_CH_1 == NULL) {
		printf("Error in creating semafor_for_COM_CH_1");
	}

	sempahoreLED = xSemaphoreCreateBinary();
	if (sempahoreLED == NULL) {
		printf("Error in creating semaphoreLED\n");
	}

	semaphoreForOk = xSemaphoreCreateBinary();
	if (semaphoreForOk == NULL) {
		printf("Error creating semaphoreForOk\n");
	}

	semaphoreMutex_COM_CH1 = xSemaphoreCreateMutex();
	if (semaphoreMutex_COM_CH1 == NULL) {
		printf("Error in creating semaphoreMutex_COM_CH1\n");
	}

	semaphoreForWindows = xSemaphoreCreateBinary();
	if (semaphoreForWindows == NULL) {
		printf("Error in creating semaphoreForWindows\n");
	}

	semaphoreForDispaly = xSemaphoreCreateBinary();
	if (semaphoreForDispaly == NULL) {
		printf("Error in creating semaphoreForDispaly\n");
	}

	//Quevi

	queue_for_message = xQueueCreate((uint8_t)10, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(char));
	if (queue_for_message == NULL) {
		printf("Error QUEUE1_CREATE\n");
	}

	led_queue = xQueueCreate(1, sizeof(uint8_t));
	if (led_queue == NULL) {
		printf("Error QUEUE1_CREATE\n");
	}

	//Tajmeri

	timer = xTimerCreate(NULL, pdMS_TO_TICKS(200), pdTRUE, NULL, tajmerFunkcija);

	if (timer == NULL) {
		printf("Error in creating timer\n");
	}

	if (xTimerStart(timer, 0) != pdPASS) {
		printf("Error in staring timer\n");
	}

	//Taskovi

	BaseType_t task;

	task = xTaskCreate(prijemPorukaOdCOM0, "Prijem poruka", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_CRITICAL, NULL);
	if (task != pdPASS) {
		printf("Error in creating prijemPorukaOdCOM0!\n");
	}

	task = xTaskCreate(prijemPorukaOdCOM1, "Prijem poruka", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_CRITICAL, NULL);
	if (task != pdPASS) {
		printf("Error in creating prijemPorukaOdCOM1\n");
	}

	task = xTaskCreate(obradaPodataka, "Obrada podataka", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_HIGH, NULL);
	if (task != pdPASS) {
		printf("Error in creating obradaPodataka task!\n");
	}

	task = xTaskCreate(message_COM_1, "Poruka o senzorima i stanje", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_NORMAL, NULL);
	if (task != pdPASS) {
		printf("Error in creating message_COM_1 task!\n");
	}

	task = xTaskCreate(ledTask, "Led diodes", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_HIGH, NULL);
	if (task != pdPASS) {
		printf("Error in creating led task!\n");
	}

	task = xTaskCreate(okTask, "Send OK", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_NORMAL, NULL);
	if (task != pdPASS) {
		printf("Erorr in creating okTask\n");
	}

	task = xTaskCreate(windowRiseFall, "Rise fall window", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_NORMAL, NULL);
	if (task != pdPASS) {
		printf("Error in creating windowRiseFall\n");
	}

	task = xTaskCreate(display, "display", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)PRIORITY_LOW, NULL);
	if (task != pdPASS) {
		printf("Error in creating display task\n");
	}

		vTaskStartScheduler();
	}

//Realizacija funkcija!

static void tajmerFunkcija(TimerHandle_t tmH) {
	static uint8_t counter_for_message_COM_CH_1;
	static uint8_t counter_for_display;

	if (send_serial_character((uint8_t)COM_CH_0, (uint8_t)'T') != 0) {
		printf("Error sending trigger\n");
	}

	counter_for_message_COM_CH_1++;
	if (counter_for_message_COM_CH_1 == (uint8_t)25) {
		if (xSemaphoreGive(semafor_for_COM_CH_1) != pdTRUE) {
			printf("Error giving semaphore COM_CH_1\n");
		}
		counter_for_message_COM_CH_1 = (uint8_t)0;
	}

	counter_for_display++;
	if (counter_for_display == (uint8_t)5) {
		if (xSemaphoreGive(semaphoreForDispaly) != pdTRUE) {
			printf("Error giving semaphoreForDisplay\n");
		}
		counter_for_display = (uint8_t)0;
	}
}

static void prijemPorukaOdCOM0(void* pvParameters) {
	static char tmpString[MAX_CHARACTERS];
	static uint8_t position = 0;
	uint8_t cc = (uint8_t)0;

	for (;;) {
		if (xSemaphoreTake(rxc_semaphore_COM_0, portMAX_DELAY) != pdTRUE) {
			printf("Error taking rxc_semaphore_COM_0\n");
		}

		if (get_serial_character(COM_CH_0, &cc) != 0) {
			printf("Error get_serial_character(COM_CH_0, &cc)\n");
		}

		if (cc == (uint8_t)13) {
			tmpString[position] = '\0';
			position = 0;
			if (xQueueSend(queue_for_message, tmpString, 0) != pdTRUE) {
				printf("Error xQueueSend(queue_for_message, tmpString, 0)\n");
			}
		}
		else if (position < ((uint8_t)MAX_CHARACTERS)) {
			tmpString[position++] = (char)cc;
		}
		else {
			//Zbog MISRA direktive 15.7
		}
	}
}

static void prijemPorukaOdCOM1(void* pvParameters) {
	static char tmpString[MAX_CHARACTERS];
	static uint8_t position = 0;
	uint8_t cc = (uint8_t)0;

	for (;;) {
		if (xSemaphoreTake(rxc_semaphore_COM_1, portMAX_DELAY) != pdTRUE) {
			printf("Error xSemaphoreTake(rxc_semaphore_COM_1, portMAX_DELAY)\n");
		}

		if (get_serial_character(COM_CH_1, &cc) != 0) {
			printf("Error get_serial_character(COM_CH_1, &cc)\n");
		}

		if (cc == (uint8_t)13) {
			tmpString[position] = '\0';
			position = 0;
			if (xQueueSend(queue_for_message, tmpString, 0) != pdTRUE) {
				printf("Error xQueueSend(queue_for_message, tmpString, 0)\n");
			}
		}
		else if (position < ((uint8_t)MAX_CHARACTERS)) {
			tmpString[position++] = (char)cc;
		}
		else {
			//Zbog MISRE 15.7
		}
	}
}

static void obradaPodataka(void* pvParameters) {
	static char string[MAX_CHARACTERS];
	static uint32_t maksimalnaBrzina = (uint32_t)100;
	static uint32_t windowLevel = (uint32_t)0;
	static uint8_t nivoAktivan = (uint8_t)0;

	static uint8_t oldDiodes = (uint8_t)0;
	static uint8_t newDiodes = (uint8_t)0;
	static uint8_t prevDiodes = (uint8_t)0;

	static uint32_t speedBuffer[10] = { 0 };
	static uint32_t speedSum = (uint32_t)0;
	static uint32_t speedIndex = (uint32_t)0;
	static uint32_t speedCount = (uint32_t)0;

	uint8_t d = 0x01;

	for (;;) {
		if (xQueueReceive(queue_for_message, string, portMAX_DELAY) != pdTRUE) {
			printf("Error QUEUE1_RECEIVE\n");
		}

		if (strncmp(string, "AUTOMATSKI", 10) == 0) {
			printf("Rezim je automatski!\n");
			rezimRada = (uint8_t)0;
			if (xSemaphoreGive(semaphoreForOk) != pdTRUE) {
				printf("Error\n");
			}
			nivoAktivan = (uint8_t)0;
			continue;
		}

		//Brzina command
		if (rezimRada == (uint8_t)0)
		{
			if (memcmp(string, "BRZINA", (size_t)6) == 0)
			{
				uint32_t temp;
				
				temp = (((uint32_t)(uint8_t)string[6] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[7] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[8] - (uint32_t)(uint8_t)'0'); //Ovako mora zbog MISRE 10.8
				maksimalnaBrzina = (uint32_t)temp;

				printf("Maksimalna brzina: %u\n", maksimalnaBrzina);
				continue;
			}
		}

		//Nivo Command
		if (rezimRada == (uint8_t)0)
		{
			if (memcmp(string, "NIVO", (size_t)4) == 0)
			{
				uint32_t temp;

				temp = (((uint32_t)(uint8_t)string[4] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[5] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[6] - (uint32_t)(uint8_t)'0'); //Ovako mora zbog MISRE 10.8
				if (temp <= (uint32_t)100)
				{
					windowLevel = (uint32_t)temp;
					nivoAktivan = (uint8_t)1;
					printf("Nivo svih prozora je %u\n", windowLevel);
				}
				else
				{
					printf("Nevalidna vrednost za NIVO: %lu\n", temp);
				}
				continue;
			}
		}


		if (nivoAktivan == (uint8_t)1)
		{
			if (strlen(string) >= (size_t)15)
			{
				if ((string[0] >= '0') && (string[0] <= '9'))
				{
					uint32_t tempSpeed;

					/* Postavi nivo svih prozora */
					rightBack = windowLevel;
					rightFront = windowLevel;
					leftFront = windowLevel;
					leftBack = windowLevel;

					tempSpeed = (((uint32_t)(uint8_t)string[12] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[13] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[14] - (uint32_t)(uint8_t)'0'); //Ovako mora zbog MISRE 10.8
					trenutnaBrzina = (uint32_t)tempSpeed;

					speedSum -= (uint32_t)speedBuffer[speedIndex];
					speedBuffer[speedIndex] = (uint32_t)trenutnaBrzina;
					speedSum += (uint32_t)trenutnaBrzina;

					speedIndex++;

					if (speedIndex >= (uint32_t)10)
					{
						speedIndex = (uint32_t)0;
					}

					if (speedCount < (uint32_t)10)
					{
						speedCount++;
					}

					if (speedCount == (uint32_t)10)
					{
						srednjaBrzina = (uint32_t)(speedSum / 10UL);
					}
				}
			}
		}


		if (rezimRada == (uint32_t)0)
		{
			if (trenutnaBrzina > maksimalnaBrzina)
			{
				uint32_t tempSpeed;

				rightBack = (uint32_t)100;
				leftBack = (uint32_t)100;
				leftFront = (uint32_t)100;
				rightFront = (uint32_t)100;

				tempSpeed = (((uint32_t)(uint8_t)string[12] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[13] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[14] - (uint32_t)(uint8_t)'0'); //Ovako mora zbog MISRE 10.8
				trenutnaBrzina = (uint32_t)tempSpeed;

				nivoAktivan = (uint8_t)0;

				printf("PREKORACENA BRZINA! Prozori -> 100\n");
				continue;
			}
		}

		if (strncmp(string, "MANUELNO", 8) == 0) {
			printf("Rezim je manualan!\n");
			rezimRada = (uint8_t)1;
			if (xSemaphoreGive(semaphoreForOk) != pdTRUE) {
				printf("Error\n");
			}
			nivoAktivan = (uint8_t)0;
			continue;
		}

		//MISRA 13.5 nije ispoštovan. Kada sam pokušavao da rešim problem kod mi nije posle radio.
		if (nivoAktivan == (uint8_t)0 && strlen(string) >= (size_t)15 && string[0] >= '0' && string[0] <= '9') {
			uint32_t newRightBack = (((uint32_t)(uint8_t)string[0] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[1] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[2] - (uint32_t)(uint8_t)'0');
			uint32_t newLeftBack = (((uint32_t)(uint8_t)string[3] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[4] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[5] - (uint32_t)(uint8_t)'0');
			uint32_t newLeftFront = (((uint32_t)(uint8_t)string[6] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[7] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[8] - (uint32_t)(uint8_t)'0');
			uint32_t newRightFront = (((uint32_t)(uint8_t)string[9] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[10] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[11] - (uint32_t)(uint8_t)'0');
			trenutnaBrzina = (((uint32_t)(uint8_t)string[12] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[13] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[14] - (uint32_t)(uint8_t)'0');

			if (newRightBack <= (uint32_t)100) {
				rightBack = newRightBack;
			}
			if (newLeftBack <= (uint32_t)100) {
				leftBack = newLeftBack;
			}
			if (newLeftFront <= (uint32_t)100) {
				leftFront = newLeftFront;
			}
			if (newRightFront <= (uint32_t)100) {
				rightFront = newRightFront;
			}

			//SrednjaBrzina
			speedSum -= (uint32_t)speedBuffer[speedIndex];
			speedBuffer[speedIndex] = (uint32_t)trenutnaBrzina;
			speedSum += (uint32_t)trenutnaBrzina;
			speedIndex++;

			if (speedIndex >= (uint32_t)10) {
				speedIndex = (uint32_t)0;
			}

			if (speedCount < (uint32_t)10) {
				speedCount++;
			}

			if (speedCount == (uint32_t)10) {
				srednjaBrzina = (uint32_t)(speedSum / 10UL);
			}		
		}

		if (rezimRada == (uint8_t)1) {
			if (xQueueReceive(led_queue, &newDiodes, 0) == pdTRUE)
			{
				uint8_t allUpperOn = (uint8_t)((newDiodes & UPPER_LED_MASK) == UPPER_LED_MASK);
				if (allUpperOn || newDiodes & (1U << 0) || newDiodes & (1U << 1) || newDiodes & (1U << 2) || newDiodes & (1U << 3))
				{
					nivoAktivan = (uint8_t)3;
				}
				
				if (nivoAktivan != (uint8_t)3) {
					if ((newDiodes & ((1U << 0) | (1U << 1) | (1U << 2) | (1U << 3))) != 0U) {
						nivoAktivan = 3;
					}
				}

				//Gornje led izlazni
				uint8_t outLeds = (uint8_t)0;
				if (get_LED_BAR(1, &outLeds) != 0) {
					printf("Error in get_LED_BAR(1, &outLeds)\n");
				}

				for (uint8_t i = 0; i < 4U; i++)
				{
					uint8_t inMask = (1U << i);         // donja ulazna LED
					uint8_t outMask = (1U << (7U - i));  // gornja izlazna LED

					if ((newDiodes & inMask) != 0U) {
						outLeds |= outMask;   // uključi
					}
					else {
						outLeds &= ~outMask;  // isključi
					}
				}

				if (set_LED_BAR(1, outLeds) != 0) {
					printf("Error in set_LED_BAR(1, outLeds)\n");
				}
			}
		}

		

		if (nivoAktivan == (uint8_t)3) {
			uint8_t change = oldDiodes ^ newDiodes;
			uint8_t allUpperOn = ((newDiodes & UPPER_LED_MASK) == UPPER_LED_MASK);

			if (allUpperOn != 0U) {
				if (set_LED_BAR(1, 0xC0) != 0) {
					printf("Error in (set_LED_BAR(1, 0xC0)\n");
				}
				rightBack = (uint32_t)100;
				leftBack = (uint32_t)100;
				if ((change & ((uint8_t)1U << 0U)) != (uint8_t)0U) {
					if ((newDiodes & ((uint8_t)1U << 0U)) != (uint8_t)0U) {
					}
					else {
						printf("RB LOCKED\n");
						if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
							printf("Error in (xSemaphoreGive(semaphoreForWindows)\n");
						}
						flagForWindowRiseFall = (uint8_t)9;
					}
				}

				if ((change & ((uint8_t)1U << 1U)) != (uint8_t)0U) {
					if ((newDiodes & ((uint8_t)1U << 1U)) != (uint8_t)0U) {
					}
					else {
						printf("LB LOCKED\n");
						if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
							printf("Error in (xSemaphoreGive(semaphoreForWindows) LB_LOCKED\n");
						}
						flagForWindowRiseFall = (uint8_t)10;
					}
				}

			}

			if (((change & ((uint8_t)1U << 0U)) != (uint8_t)0U) && (allUpperOn == (uint8_t)0U)) {
				if ((newDiodes & (uint8_t)(1U << 0U)) != (uint8_t)0U) {
					rightBack = (uint32_t)100;
					printf("LED 0 ON\n");
					flagForWindowRiseFall = (uint8_t)1;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForWindows) LED 0 ON\n");
					}
				}
				else {
					rightBack = (uint32_t)0;
					flagForWindowRiseFall = (uint8_t)2;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForWindows) LED 0 OFF");
					}
					printf("LED 0 OFF\n");
				}
			}

			if (((change & ((uint8_t)1U << 1U)) != (uint8_t)0U) && (allUpperOn == (uint8_t)0U)) {
				if ((newDiodes & (uint8_t)(1U << 1U)) != (uint8_t)0U) {
					leftBack = (uint32_t)100;
					printf("LED 1 ON\n");
					flagForWindowRiseFall = (uint8_t)3;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForWindows) LED 1 ON\n");
					}
				}
				else {
					leftBack = (uint32_t)0;
					printf("LED 1 OFF\n");
					flagForWindowRiseFall = (uint8_t)4;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForWindows) LED 1 OFF\n");
					}
				}
			}

			if ((change & (uint8_t)(1U << 2)) != (uint8_t)0) {
				if ((newDiodes & (uint8_t)(1U << 2)) != (uint8_t)0U) {
					leftFront = (uint32_t)100;
					printf("LED 2 ON\n");
					flagForWindowRiseFall = (uint8_t)5;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForWindows) LED 2 ON\n");
					}
				}
				else {
					leftFront = (uint32_t)0;
					printf("LED 2 OFF\n");
					flagForWindowRiseFall = (uint8_t)6;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in (xSemaphoreGive(semaphoreForWindows) LED 2 OFF\n");
					}
				}
			}

			if ((change & (uint8_t)(1U << 3)) != (uint8_t)0U) {
				if ((newDiodes & (uint8_t)(1U << 3)) != (uint8_t)0) {
					rightFront = (uint32_t)100;
					printf("LED 3 ON\n");
					flagForWindowRiseFall = (uint8_t)7;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForWindows) LED 3 ON\n");
					}
				}
				else {
					rightFront = (uint32_t)0;
					printf("LED 3 OFF\n");
					flagForWindowRiseFall = (uint8_t)8;
					if (xSemaphoreGive(semaphoreForWindows) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForWindows) LED 3 OFF");
					}
				}
			}

			trenutnaBrzina = (((uint32_t)(uint8_t)string[12] - (uint32_t)(uint8_t)'0') * 100UL) + (((uint32_t)(uint8_t)string[13] - (uint32_t)(uint8_t)'0') * 10UL) + ((uint32_t)(uint8_t)string[14] - (uint32_t)(uint8_t)'0');
			speedSum -= (uint32_t)speedBuffer[speedIndex];
			speedBuffer[speedIndex] = (uint32_t)trenutnaBrzina;
			speedSum += (uint32_t)trenutnaBrzina;
			speedIndex++;

			if (speedIndex >= (uint32_t)10U) {
				speedIndex = (uint32_t)0U;
			}

			if (speedCount < (uint32_t)10U) {
				speedCount++;
			}

			if (speedCount == (uint32_t)10U) {
				srednjaBrzina = (uint32_t)(speedSum / 10UL);
			}
		}
		oldDiodes = newDiodes;

		if (nivoAktivan == (uint8_t)0U || nivoAktivan == (uint8_t)1U || nivoAktivan == (uint8_t)3) {
			if (trenutnaBrzina < minBrzina) {
				minBrzina = trenutnaBrzina;
			}

			if (trenutnaBrzina > maxBrzina) {
				maxBrzina = trenutnaBrzina;
			}

			if (get_LED_BAR(2, &d) == 0) {
				if ((d & (uint8_t)0x20U) != (uint8_t)0U) {
					flagForDisplayMinMax = (uint8_t)1;
					if (xSemaphoreGive(semaphoreForDispaly) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForDispaly)\n");
					}
				}
				else {
					flagForDisplayMinMax = (uint8_t)0;
					if (xSemaphoreGive(semaphoreForDispaly) != pdTRUE) {
						printf("Error in xSemaphoreGive(semaphoreForDispaly)\n");
					}
				}
			}
		}
	}
}

static void ledTask(void* pvParameters) {
	uint8_t ledTmp;

	for (;;) {
		if (xSemaphoreTake(sempahoreLED, portMAX_DELAY) != pdTRUE) {
			printf("Error LED_SEM_TAKE\n");
		}

		if (get_LED_BAR(0, &ledTmp) != 0) {
			printf("Error GET_LED\n");
			continue;
		}

		if (xQueueOverwrite(led_queue, &ledTmp) != pdTRUE) {
			printf("Error QUEUE3_SEND\n");
		}
	}
}


static void message_COM_1(void* pvParameters) {

	static uint32_t string[MAX_CHARACTERS];

	static const char stringMANUELNO[] = "MANUELNO\n";
	static const char stringAUTOMATSKI[] = "AUTOMATSKI\n";

	for (;;) {
		if (xSemaphoreTake(semafor_for_COM_CH_1, portMAX_DELAY) != pdTRUE) {
			printf("Error xSemaphoreTake(semafor_for_COM_CH_1, portMAX_DELAY)\n");
		}

		if (xSemaphoreTake(semaphoreMutex_COM_CH1, portMAX_DELAY) != pdTRUE) {
			printf("Error (xSemaphoreTake(semaphoreMutex_COM_CH1, portMAX_DELAY)\n");
		}

		string[0] = (uint32_t)(rightBack / 100U);
		string[1] = (uint32_t)(rightBack % 100U) / 10U;
		string[2] = (uint32_t)(rightBack % 10U);

		string[3] = (uint32_t)(leftBack / 100U);
		string[4] = (uint32_t)(leftBack % 100U) / 10U;
		string[5] = (uint32_t)(leftBack % 10U);


		string[6] = (uint32_t)(leftFront / 100U);
		string[7] = (uint32_t)(leftFront % 100U) / 10U;
		string[8] = (uint32_t)(leftFront % 10U);
		
		string[9] = (uint32_t)(rightFront / 100U);
		string[10] = (uint32_t)(rightFront % 100U) / 10U;
		string[11] = (uint32_t)(rightFront % 10U);

		string[12] = (uint32_t)(srednjaBrzina / 100U);
		string[13] = (uint32_t)((srednjaBrzina % 100U) / 10U);
		string[14] = (uint32_t)(srednjaBrzina % 10U);


		for (uint8_t i = (uint8_t)0; i < (uint8_t)15; i++) {
			if (string[i] <= (uint8_t)9) {
				string[i] = string[i] + (uint8_t)48;
			}
		}

		for (uint8_t i = (uint8_t)0; i <= (uint8_t)2; i++) {
			if (send_serial_character(COM_CH_1, (uint8_t)string[i]) != 0) {
				printf("Error sending rightBack to COM_CH1\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
		}

		if (send_serial_character(COM_CH_1, (uint8_t)' ') != 0) {
			printf("Error (send_serial_character((uint8_t)COM_CH_1, ' ')\n");
		}
		vTaskDelay(pdMS_TO_TICKS(100));

		for (uint8_t i = (uint8_t)3; i <= (uint8_t)5; i++) {
			if (send_serial_character(COM_CH_1, (uint8_t)string[i]) != 0) {
				printf("Error sending leftBack to COM_CH1\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
		}

		if (send_serial_character(COM_CH_1, (uint8_t)' ') != 0) {
			printf("Error (send_serial_character((uint8_t)COM_CH_1, ' ')\n");
		}
		vTaskDelay(pdMS_TO_TICKS(100));

		for (uint8_t i = (uint8_t)6; i <= (uint8_t)8; i++) {
			if (send_serial_character(COM_CH_1, (uint8_t)string[i]) != 0) {
				printf("Error sending leftFront to COM_CH1\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
		}

		if (send_serial_character(COM_CH_1, (uint8_t)' ') != 0) {
			printf("Error (send_serial_character((uint8_t)COM_CH_1, ' ')\n");
		}
		vTaskDelay(pdMS_TO_TICKS(100));

		for (uint8_t i = (uint8_t)9; i <= (uint8_t)11; i++) {
			if (send_serial_character(COM_CH_1, (uint8_t)string[i]) != 0) {
				printf("Error sending rightFront to COM_CH1\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
		}

		if (send_serial_character(COM_CH_1, (uint8_t)' ') != 0) {
			printf("Error (send_serial_character((uint8_t)COM_CH_1, ' ')\n");
		}
		vTaskDelay(pdMS_TO_TICKS(100));

		for (uint8_t i = (uint8_t)12; i <= (uint8_t)14; i++) {
			if (send_serial_character(COM_CH_1, (uint8_t)string[i]) != 0) {
				printf("Error sending srednjaBrzina to COM_CH1\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
		}

		if (send_serial_character(COM_CH_1, (uint8_t)' ') != 0) {
			printf("Error (send_serial_character((uint8_t)COM_CH_1, ' ')\n");
		}
		vTaskDelay(pdMS_TO_TICKS(100));

		if (rezimRada == (uint8_t)1) {
			for (uint8_t i = (uint8_t)0U; stringMANUELNO[i] != '\0'; i++) {
				if (send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringMANUELNO[i]) != 0) {
					printf("Error sending MANUELNO to COM_CH_1\n");
				}
				vTaskDelay(pdMS_TO_TICKS(100));
			}
		}

		if (rezimRada == (uint8_t)0) {
			for (uint8_t i = (uint8_t)0U; stringAUTOMATSKI[i] != '\0'; i++) {
				if (send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringAUTOMATSKI[i]) != 0) {
					printf("Error sending AUTOMATSKI to COM_CH1\n");
				}
				vTaskDelay(pdMS_TO_TICKS(100));
			}
		}

		if (xSemaphoreGive(semaphoreMutex_COM_CH1) != pdTRUE) {
			printf("Error in (xSemaphoreGive(semaphoreMutex_COM_CH1) != pdTRUE)\n");
		}

	}
}

static void okTask(void* pvParameters) {
	for (;;) {
		// Wait for mode change signal
		if (xSemaphoreTake(semaphoreForOk, portMAX_DELAY) == pdTRUE) {

			if (xSemaphoreTake(semaphoreMutex_COM_CH1, portMAX_DELAY) != pdTRUE) {
				printf("Error in xSemaphoreTake(semaphoreMutex_COM_CH1, portMAX_DELAY)\n");
			}
			// Send "OK" on COM_1
			if (send_serial_character(COM_CH_1, (uint8_t)'O') != 0) {
				printf("Error in send_serial_character((uint8_t)COM_CH_1, 'O')\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
			if (send_serial_character(COM_CH_1, (uint8_t)'K') != 0) {
				printf("Error in send_serial_character((uint8_t)COM_CH_1, 'K')\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
			if (send_serial_character(COM_CH_1, (uint8_t)'\n') != 0) {
				printf("Error in send_serial_character((uint8_t)COM_CH_1, '\n')\n");
			}
			vTaskDelay(pdMS_TO_TICKS(100));
			printf("OK sent on COM_1\n");

			if (xSemaphoreGive(semaphoreMutex_COM_CH1) != pdTRUE) {
				printf("Error in xSemaphoreGive(semaphoreMutex_COM_CH1)\n");
			}
		}
	}
}

static void windowRiseFall(void* pvParameters) {

	static const char stringRiseRB[] = "RiseRB\n";
	static const char stringFallRB[] = "FallRB\n";

	static const char stringRiseLB[] = "RiseLB\n";
	static const char stringFallLB[] = "FallLB\n";

	static const char stringRiseLF[] = "RiseLF\n";
	static const char stringFallLF[] = "FallLF\n";

	static const char stringRiseRF[] = "RiseRF\n";
	static const char stringFallRF[] = "FallRF\n";

	static const char stringRBLOCKED[] = "RB LOCKED\n";
	static const char stringLBLOCKED[] = "LB LOCKED\n";

	//uint8_t i;

	for (;;) {
		if (xSemaphoreTake(semaphoreForWindows, portMAX_DELAY) == pdTRUE) {

			if (xSemaphoreTake(semaphoreMutex_COM_CH1, portMAX_DELAY) != pdTRUE) {
				printf("Error in xSemaphoreTake(semaphoreMutex_COM_CH1, portMAX_DELAY)\n");
			}

			if (flagForWindowRiseFall == (uint8_t)1) {
				printf("Rise rightBack\n");
				for (uint8_t i = (uint8_t)0; stringRiseRB[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringRiseRB[i]) != 0) {
						printf("Error in send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringRiseRB[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}

			else if (flagForWindowRiseFall == (uint8_t)2) {
				printf("Fall rightBack\n");
				for (uint8_t i = (uint8_t)0; stringFallRB[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringFallRB[i]) != 0) {
						printf("Error in (send_serial_character(COM_CH_1, (uint8_t)stringFallRB[i]) != 0)\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}
			
			else if (flagForWindowRiseFall == (uint8_t)3) {
				printf("Rise leftBack\n");

				for (uint8_t i = (uint8_t)0; stringRiseLB[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringRiseLB[i]) != 0) {
						printf("Error in (send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringRiseLB[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}

			}

			else if (flagForWindowRiseFall == (uint8_t)4) {
				printf("Fall leftBack\n");

				for (uint8_t i = (uint8_t)0; stringFallLB[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringFallLB[i]) != 0) {
						printf("send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringFallLB[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}

			else if (flagForWindowRiseFall == (uint8_t)5) {
				printf("Rise leftFront\n");
				for (uint8_t i = (uint8_t)0; stringRiseLF[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringRiseLF[i]) != 0) {
						printf("Error in send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringRiseLF[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}

			else if (flagForWindowRiseFall == (uint8_t)6) {
				printf("Fall leftFront\n");
				for (uint8_t i = (uint8_t)0; stringFallLF[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringFallLF[i]) != 0) {
						printf("Error in send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringFallLF[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}

			else if (flagForWindowRiseFall == (uint8_t)7) {
				printf("Rise rightFront\n");
				for (uint8_t i = (uint8_t)0; stringRiseRF[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringRiseRF[i]) != 0) {
						printf("Error in send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringRiseRF[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}

			else if (flagForWindowRiseFall == (uint8_t)8) {
				printf("Fall rightFront\n");
				for (uint8_t i = (uint8_t)0; stringFallRF[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringFallRF[i]) != 0) {
						printf("Error in send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringFallRF[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}

			else if (flagForWindowRiseFall == (uint8_t)9) {
				for (uint8_t i = (uint8_t)0; stringRBLOCKED[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringRBLOCKED[i]) != 0) {
						printf("Error in send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringRBLOCKED[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}

			else if (flagForWindowRiseFall == (uint8_t)10) {
				for (uint8_t i = (uint8_t)0; stringLBLOCKED[i] != '\0'; i++) {
					if (send_serial_character(COM_CH_1, (uint8_t)stringLBLOCKED[i]) != 0) {
						printf("Error in send_serial_character((uint8_t)COM_CH_1, (uint8_t)stringLBLOCKED[i])\n");
					}
					vTaskDelay(pdMS_TO_TICKS(100));
				}
			}
			else {
				//Zbog misra direktive 15.7
			}
			if (xSemaphoreGive(semaphoreMutex_COM_CH1) != pdTRUE) {
				printf("Error in (xSemaphoreGive(semaphoreMutex_COM_CH1)\n");
			}
		}
	}
}

static void display(void* pvParameters) {
	const uint8_t character[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
	uint32_t indexSrednja1, indexSrednja2, indexSrednja3;
	uint32_t indexMinimum1, indexMinimum2, indexMinimum3;
	uint32_t indexMaximum1, indexMaximum2, indexMaximum3;

	for (;;) {
		if (xSemaphoreTake(semaphoreForDispaly, portMAX_DELAY) != pdTRUE) {
			printf("Error taking semaphoreForDisplay\n");
		}

		if ((uint8_t)select_7seg_digit(0) != (uint8_t)0) {
			printf("Error selecting display digit 0\n");
		}
		
		if ((uint8_t)set_7seg_digit(character[rezimRada]) != (uint8_t)0) {
			printf("Error displaying rezimRada\n");
		}

		if ((uint8_t)select_7seg_digit(1) != (uint8_t)0) {
			printf("Error selecting display digit 1\n");
		}

		if ((uint8_t)set_7seg_digit(0x40) != (uint8_t)0) {
			printf("Error displaying rezimRada\n");
		}

		indexSrednja3 = (uint32_t)(trenutnaBrzina % 10U);
		indexSrednja2 = (uint32_t)((trenutnaBrzina / 10U) % 10U);
		indexSrednja1 = (uint32_t)(trenutnaBrzina / 100U);

		if ((uint8_t)select_7seg_digit((uint8_t)2) != (uint8_t)0) {
			printf("Error selecting display digit 2\n");
		}

		if ((uint8_t)set_7seg_digit(character[indexSrednja1]) != (uint8_t)0) {
			printf("Error displaying digit 2\n");
		}

		if ((uint8_t)select_7seg_digit((uint8_t)3) != (uint8_t)0) {
			printf("Error selecting display digit 3\n");
		}

		if ((uint8_t)set_7seg_digit(character[indexSrednja2]) != (uint8_t)0) {
			printf("Error display digit 3\n");
		}

		if ((uint8_t)select_7seg_digit((uint8_t)4) != (uint8_t)0) {
			printf("Error selecting display digit 4\n");
		}

		if ((uint8_t)set_7seg_digit(character[indexSrednja3]) != (uint8_t)0) {
			printf("Error display digit 4\n");
		}

		if ((uint8_t)select_7seg_digit((uint8_t)5) != (uint8_t)0) {
			printf("Error selecting display digit 5\n");
		}

		if ((uint8_t)set_7seg_digit(0x40) != (uint8_t)0) {
			printf("Error display digit 5\n");
		}

		indexMinimum3 = (uint32_t)(minBrzina % 10U);
		indexMinimum2 = (uint32_t)((minBrzina / 10U) % 10U);
		indexMinimum1 = (uint32_t)(minBrzina / 100U);

		if (flagForDisplayMinMax == (uint8_t)0) {

			if ((uint8_t)select_7seg_digit((uint8_t)6) != (uint8_t)0) {
				printf("Error selecting display digit 6\n");
			}

			if ((uint8_t)set_7seg_digit(character[indexMinimum1]) != (uint8_t)0) {
				printf("Error display digit 6\n");
			}

			if ((uint8_t)select_7seg_digit((uint8_t)7) != (uint8_t)0) {
				printf("Error selecting display digit 7\n");
			}

			if ((uint8_t)set_7seg_digit(character[indexMinimum2]) != (uint8_t)0) {
				printf("Error display digit 7\n");
			}

			if ((uint8_t)select_7seg_digit((uint8_t)8) != (uint8_t)0) {
				printf("Error selecting display digit 8\n");
			}

			if ((uint8_t)set_7seg_digit(character[indexMinimum3]) != (uint8_t)0) {
				printf("Error display digit 8\n");
			}

		}

		indexMaximum3 = (uint32_t)(maxBrzina % 10U);
		indexMaximum2 = (uint32_t)((maxBrzina / 10U) % 10U);
		indexMaximum1 = (uint32_t)(maxBrzina / 100U);

		if (flagForDisplayMinMax == (uint8_t)1) {

			if ((uint8_t)select_7seg_digit((uint8_t)6) != (uint8_t)0) {
				printf("Error selecting display digit 6\n");
			}

			if ((uint8_t)set_7seg_digit(character[indexMaximum1]) != (uint8_t)0) {
				printf("Error display digit 6\n");
			}

			if ((uint8_t)select_7seg_digit((uint8_t)7) != (uint8_t)0) {
				printf("Error selecting display digit 7\n");
			}

			if ((uint8_t)set_7seg_digit(character[indexMaximum2]) != (uint8_t)0) {
				printf("Error display digit 7\n");
			}

			if ((uint8_t)select_7seg_digit((uint8_t)8) != (uint8_t)0) {
				printf("Error selecting display digit 8\n");
			}

			if ((uint8_t)set_7seg_digit(character[indexMaximum3]) != (uint8_t)0) {
				printf("Error display digit 8\n");
			}
		}
	}
}



